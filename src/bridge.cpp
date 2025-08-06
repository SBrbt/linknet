#include "bridge.h"
#include <sys/select.h>

Bridge::Bridge(TunManager& tun_mgr, SocketManager& socket_mgr, CryptoManager& crypto_mgr, const Config& cfg)
    : tun_manager(tun_mgr), socket_manager(socket_mgr), crypto_manager(crypto_mgr),
      running(false), reconnecting(false),
      tun_to_socket_packets(0), socket_to_tun_packets(0),
      tun_to_socket_bytes(0), socket_to_tun_bytes(0), config(cfg) {
    
    update_activity();
}

Bridge::~Bridge() {
    stop();
}

bool Bridge::start() {
    if (running.load()) {
        Logger::log(LogLevel::WARNING, "Bridge already running");
        return false;
    }
    
    running.store(true);
    Logger::log(LogLevel::INFO, "Starting encrypted packet bridge...");
    
    // Start authentication thread
    auth_thread = std::thread(&Bridge::auth_loop, this);
    
    // Start forwarding threads
    tun_to_socket_thread = std::thread(&Bridge::tun_to_socket_loop, this);
    socket_to_tun_thread = std::thread(&Bridge::socket_to_tun_loop, this);
    
    if (config.enable_keepalive) {
        keepalive_thread = std::thread(&Bridge::keepalive_loop, this);
    }
    
    // Wait for threads to complete
    if (auth_thread.joinable()) {
        auth_thread.join();
    }
    if (tun_to_socket_thread.joinable()) {
        tun_to_socket_thread.join();
    }
    if (socket_to_tun_thread.joinable()) {
        socket_to_tun_thread.join();
    }
    if (keepalive_thread.joinable()) {
        keepalive_thread.join();
    }
    
    Logger::log(LogLevel::INFO, "Bridge stopped");
    return true;
}

void Bridge::stop() {
    if (!running.load()) {
        return;
    }
    
    Logger::log(LogLevel::INFO, "Stopping bridge...");
    running.store(false);
    
    // Wait for threads to finish
    if (auth_thread.joinable()) {
        auth_thread.join();
    }
    if (tun_to_socket_thread.joinable()) {
        tun_to_socket_thread.join();
    }
    if (socket_to_tun_thread.joinable()) {
        socket_to_tun_thread.join();
    }
    if (keepalive_thread.joinable()) {
        keepalive_thread.join();
    }
}

void Bridge::tun_to_socket_loop() {
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    
    Logger::log(LogLevel::DEBUG, "TUN to Socket forwarding thread started");
    
    while (running.load()) {
        // Use select with timeout for clean shutdown
        FD_ZERO(&read_fds);
        FD_SET(tun_manager.get_fd(), &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(tun_manager.get_fd() + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                Logger::log(LogLevel::ERROR, "Select error in TUN to Socket loop: " + 
                           NetworkUtils::get_error_string(errno));
            }
            continue;
        }
        
        if (activity == 0) {
            // Timeout, continue to check running flag
            continue;
        }
        
        if (!FD_ISSET(tun_manager.get_fd(), &read_fds)) {
            continue;
        }
        
        // Read packet from TUN
        ssize_t bytes_read = tun_manager.read_packet(buffer, sizeof(buffer));
        if (bytes_read < 0) {
            continue;
        }
        
        if (bytes_read == 0) {
            continue;
        }
        
        // Check if socket is connected and authenticated
        if (!socket_manager.is_socket_connected() || !crypto_manager.is_authenticated()) {
            if (config.mode == "client") {
                // Try to reconnect
                if (!attempt_reconnection()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            } else {
                // Server mode, wait for connection and authentication
                continue;
            }
        }
        
        // Send encrypted packet through socket
        ssize_t bytes_sent = send_encrypted_data(buffer, bytes_read) ? bytes_read : -1;
        if (bytes_sent > 0) {
            tun_to_socket_packets.fetch_add(1);
            tun_to_socket_bytes.fetch_add(bytes_sent);
            update_activity();
            
            Logger::log(LogLevel::DEBUG, 
                       "Forwarded packet TUN->Socket: " + std::to_string(bytes_sent) + " bytes");
        }
    }
    
    Logger::log(LogLevel::DEBUG, "TUN to Socket forwarding thread stopped");
}

void Bridge::socket_to_tun_loop() {
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    
    Logger::log(LogLevel::DEBUG, "Socket to TUN forwarding thread started");
    
    while (running.load()) {
        // Check if socket is connected
        if (!socket_manager.is_socket_connected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Use select with timeout for clean shutdown
        FD_ZERO(&read_fds);
        FD_SET(socket_manager.get_fd(), &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(socket_manager.get_fd() + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                Logger::log(LogLevel::ERROR, "Select error in Socket to TUN loop: " + 
                           NetworkUtils::get_error_string(errno));
            }
            continue;
        }
        
        if (activity == 0) {
            // Timeout, continue to check running flag
            continue;
        }
        
        if (!FD_ISSET(socket_manager.get_fd(), &read_fds)) {
            continue;
        }
        
        // Read data from socket
        ssize_t bytes_read = socket_manager.receive_data(buffer, sizeof(buffer));
        if (bytes_read < 0) {
            Logger::log(LogLevel::DEBUG, "Socket receive error");
            continue;
        }

        if (bytes_read == 0) {
            // Connection closed
            Logger::log(LogLevel::INFO, "Socket connection closed");
            continue;
        }

        Logger::log(LogLevel::DEBUG, "Received " + std::to_string(bytes_read) + " bytes from socket");
        
        // Handle encrypted packet (includes auth, data, keepalive)
        handle_encrypted_packet(buffer, bytes_read);
        update_activity();
    }
    
    Logger::log(LogLevel::DEBUG, "Socket to TUN forwarding thread stopped");
}

void Bridge::keepalive_loop() {
    Logger::log(LogLevel::DEBUG, "Keepalive thread started");
    
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        if (!running.load()) {
            break;
        }
        
        if (socket_manager.is_socket_connected()) {
            if (!is_connection_healthy()) {
                Logger::log(LogLevel::WARNING, "Connection appears unhealthy, sending keepalive");
                send_keepalive();
            }
        }
    }
    
    Logger::log(LogLevel::DEBUG, "Keepalive thread stopped");
}

void Bridge::auth_loop() {
    Logger::log(LogLevel::DEBUG, "Authentication thread started");
    
    while (running.load()) {
        if (socket_manager.is_socket_connected() && !crypto_manager.is_authenticated()) {
            if (!perform_authentication()) {
                Logger::log(LogLevel::WARNING, "Authentication failed, retrying in 5 seconds");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        } else if (crypto_manager.needs_reauth()) {
            Logger::log(LogLevel::INFO, "Re-authentication required");
            if (!perform_authentication()) {
                Logger::log(LogLevel::WARNING, "Re-authentication failed");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    Logger::log(LogLevel::DEBUG, "Authentication thread stopped");
}

bool Bridge::perform_authentication() {
    if (config.mode == "client") {
        Logger::log(LogLevel::DEBUG, "Client starting authentication...");
        
        // Client initiates authentication
        char auth_buffer[BUFFER_SIZE];
        size_t auth_size = sizeof(auth_buffer);
        
        if (!crypto_manager.create_auth_request(auth_buffer, auth_size)) {
            Logger::log(LogLevel::ERROR, "Failed to create authentication request");
            return false;
        }
        
        Logger::log(LogLevel::DEBUG, "Created authentication request, size: " + std::to_string(auth_size));
        
        ssize_t sent = socket_manager.send_data(auth_buffer, auth_size);
        if (sent != (ssize_t)auth_size) {
            Logger::log(LogLevel::ERROR, "Failed to send authentication request, sent: " + 
                       std::to_string(sent) + ", expected: " + std::to_string(auth_size));
            Logger::log(LogLevel::DEBUG, std::string("Socket connection status: ") + 
                       (socket_manager.is_socket_connected() ? "connected" : "disconnected"));
            return false;
        }
        
        Logger::log(LogLevel::DEBUG, "Authentication request sent successfully");
        
        // Wait for response
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(socket_manager.get_fd(), &read_fds);
        timeout.tv_sec = 10; // 10 second timeout
        timeout.tv_usec = 0;
        
        Logger::log(LogLevel::DEBUG, "Waiting for authentication response...");
        int activity = select(socket_manager.get_fd() + 1, &read_fds, NULL, NULL, &timeout);
        if (activity <= 0) {
            if (activity == 0) {
                Logger::log(LogLevel::ERROR, "Authentication timeout (10 seconds)");
            } else {
                Logger::log(LogLevel::ERROR, "Authentication select error: " + 
                           NetworkUtils::get_error_string(errno));
            }
            return false;
        }
        
        Logger::log(LogLevel::DEBUG, "Data available for authentication response");
        
        char response_buffer[BUFFER_SIZE];
        ssize_t received = socket_manager.receive_data(response_buffer, sizeof(response_buffer));
        if (received <= 0) {
            Logger::log(LogLevel::ERROR, "Failed to receive authentication response, received: " + 
                       std::to_string(received));
            return false;
        }
        
        Logger::log(LogLevel::DEBUG, "Received authentication response, size: " + std::to_string(received));
        
        bool auth_result = crypto_manager.handle_auth_response(response_buffer, received);
        Logger::log(LogLevel::DEBUG, std::string("Authentication result: ") + (auth_result ? "success" : "failed"));
        
        return auth_result;
    }
    
    Logger::log(LogLevel::DEBUG, "Server mode: waiting for client authentication");
    return true; // Server waits for client to initiate
}

bool Bridge::handle_auth_packet(const char* buffer, size_t size) {
    if (size < sizeof(EncryptedHeader)) {
        Logger::log(LogLevel::DEBUG, "Auth packet too small: " + std::to_string(size));
        return false;
    }
    
    const EncryptedHeader* header = (const EncryptedHeader*)buffer;
    PacketType type = (PacketType)header->packet_type;
    
    Logger::log(LogLevel::DEBUG, "Handling auth packet, type: " + std::to_string((int)type) + 
               ", mode: " + config.mode);
    
    if (type == PacketType::AUTH_REQUEST && config.mode == "server") {
        Logger::log(LogLevel::DEBUG, "Server processing AUTH_REQUEST");
        char response[BUFFER_SIZE];
        size_t response_size = sizeof(response);
        
        if (crypto_manager.handle_auth_request(buffer, size, response, response_size)) {
            Logger::log(LogLevel::DEBUG, "Auth request processed, sending response, size: " + 
                       std::to_string(response_size));
            ssize_t sent = socket_manager.send_data(response, response_size);
            Logger::log(LogLevel::DEBUG, "Auth response sent: " + std::to_string(sent) + " bytes");
            return sent == (ssize_t)response_size;
        } else {
            Logger::log(LogLevel::ERROR, "Failed to process auth request");
        }
    } else if (type == PacketType::AUTH_RESPONSE && config.mode == "client") {
        Logger::log(LogLevel::DEBUG, "Client processing AUTH_RESPONSE");
        return crypto_manager.handle_auth_response(buffer, size);
    } else {
        Logger::log(LogLevel::DEBUG, "Auth packet type mismatch - type: " + std::to_string((int)type) + 
                   ", mode: " + config.mode);
    }
    
    return false;
}

bool Bridge::attempt_reconnection() {
    if (reconnecting.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(reconnect_mutex);
    if (reconnecting.load()) {
        return false;
    }
    
    reconnecting.store(true);
    
    Logger::log(LogLevel::INFO, "Attempting to reconnect to server...");
    
    socket_manager.close_connection();
    
    bool success = socket_manager.connect_to_server(config.remote_ip, config.port);
    if (success) {
        Logger::log(LogLevel::INFO, "Reconnection successful");
        update_activity();
    } else {
        Logger::log(LogLevel::WARNING, "Reconnection failed, will retry later");
        std::this_thread::sleep_for(std::chrono::seconds(config.reconnect_interval));
    }
    
    reconnecting.store(false);
    return success;
}

void Bridge::update_activity() {
    last_activity = std::chrono::steady_clock::now();
}

bool Bridge::is_connection_healthy() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity);
    return duration.count() < 120; // Consider unhealthy if no activity for 2 minutes
}

bool Bridge::send_keepalive() {
    if (!socket_manager.is_socket_connected() || !crypto_manager.is_authenticated()) {
        return false;
    }
    
    // Create keepalive packet
    unsigned char keepalive_data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    return send_encrypted_data((const char*)keepalive_data, sizeof(keepalive_data));
}

bool Bridge::handle_encrypted_packet(const char* buffer, size_t size) {
    if (size < sizeof(EncryptedHeader)) {
        Logger::log(LogLevel::DEBUG, "Packet too small for EncryptedHeader: " + std::to_string(size));
        return false;
    }
    
    const EncryptedHeader* header = (const EncryptedHeader*)buffer;
    PacketType type = (PacketType)header->packet_type;
    
    Logger::log(LogLevel::DEBUG, "Received packet type: " + std::to_string((int)type) + 
               ", size: " + std::to_string(size));
    
    switch (type) {
        case PacketType::AUTH_REQUEST:
        case PacketType::AUTH_RESPONSE:
        case PacketType::AUTH_SUCCESS:
        case PacketType::AUTH_FAILED:
            Logger::log(LogLevel::DEBUG, "Processing auth packet");
            return handle_auth_packet(buffer, size);
            
        case PacketType::DATA_PACKET: {
            if (!crypto_manager.is_authenticated()) {
                Logger::log(LogLevel::WARNING, "Received data packet before authentication");
                return false;
            }
            
            char decrypted_data[BUFFER_SIZE];
            size_t decrypted_size = sizeof(decrypted_data);
            
            if (crypto_manager.unwrap_data_packet(buffer, size, decrypted_data, decrypted_size)) {
                // Check if it's a keepalive packet
                if (decrypted_size == 4 && 
                    memcmp(decrypted_data, "\xDE\xAD\xBE\xEF", 4) == 0) {
                    Logger::log(LogLevel::DEBUG, "Keepalive packet received");
                    return true;
                }
                
                // Write decrypted packet to TUN
                ssize_t bytes_written = tun_manager.write_packet(decrypted_data, decrypted_size);
                if (bytes_written > 0) {
                    socket_to_tun_packets.fetch_add(1);
                    socket_to_tun_bytes.fetch_add(bytes_written);
                    
                    Logger::log(LogLevel::DEBUG, 
                               "Forwarded encrypted packet Socket->TUN: " + 
                               std::to_string(bytes_written) + " bytes");
                    return true;
                }
            }
            break;
        }
        
        case PacketType::KEEPALIVE:
            Logger::log(LogLevel::DEBUG, "Keepalive packet received");
            return true;
            
        default:
            Logger::log(LogLevel::WARNING, "Unknown packet type: " + 
                       std::to_string((int)type));
            break;
    }
    
    return false;
}

bool Bridge::send_encrypted_data(const char* data, size_t size) {
    if (!crypto_manager.is_authenticated()) {
        return false;
    }
    
    char wrapped_data[BUFFER_SIZE];
    size_t wrapped_size = sizeof(wrapped_data);
    
    if (crypto_manager.wrap_data_packet(data, size, wrapped_data, wrapped_size)) {
        ssize_t sent = socket_manager.send_data(wrapped_data, wrapped_size);
        return sent == (ssize_t)wrapped_size;
    }
    
    return false;
}

void Bridge::print_statistics() const {
    std::cout << "\n=== Bridge Statistics ===" << std::endl;
    std::cout << "TUN->Socket: " << tun_to_socket_packets.load() << " packets, " 
              << tun_to_socket_bytes.load() << " bytes" << std::endl;
    std::cout << "Socket->TUN: " << socket_to_tun_packets.load() << " packets, " 
              << socket_to_tun_bytes.load() << " bytes" << std::endl;
    std::cout << "Total: " << (tun_to_socket_packets.load() + socket_to_tun_packets.load()) 
              << " packets, " << (tun_to_socket_bytes.load() + socket_to_tun_bytes.load()) 
              << " bytes" << std::endl;
    std::cout << "========================\n" << std::endl;
}

void Bridge::reset_statistics() {
    tun_to_socket_packets.store(0);
    socket_to_tun_packets.store(0);
    tun_to_socket_bytes.store(0);
    socket_to_tun_bytes.store(0);
}
