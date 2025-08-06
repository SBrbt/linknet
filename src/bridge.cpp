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
    Logger::log(LogLevel::INFO, "Starting encrypted packet bridge (single-threaded)...");
    
    // Single-threaded main loop
    main_loop();
    
    Logger::log(LogLevel::INFO, "Bridge stopped");
    return true;
}

void Bridge::stop() {
    if (!running.load()) {
        return;
    }
    
    Logger::log(LogLevel::INFO, "Stopping bridge...");
    running.store(false);
}

void Bridge::main_loop() {
    char tun_buffer[BUFFER_SIZE];
    char socket_buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    time_t last_keepalive = time(nullptr);
    time_t last_auth_attempt = 0;
    bool auth_in_progress = false;
    
    Logger::log(LogLevel::DEBUG, "Single-threaded main loop started");
    
    while (running.load()) {
        FD_ZERO(&read_fds);
        int max_fd = 0;
        
        // Always monitor TUN interface
        if (tun_manager.get_fd() >= 0) {
            FD_SET(tun_manager.get_fd(), &read_fds);
            max_fd = std::max(max_fd, tun_manager.get_fd());
        }
        
        // Monitor socket if connected
        if (socket_manager.is_socket_connected() && socket_manager.get_fd() >= 0) {
            FD_SET(socket_manager.get_fd(), &read_fds);
            max_fd = std::max(max_fd, socket_manager.get_fd());
        }
        
        // Set timeout for regular maintenance tasks
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                Logger::log(LogLevel::ERROR, "Select error in main loop: " + 
                           NetworkUtils::get_error_string(errno));
            }
            continue;
        }
        
        time_t now = time(nullptr);
        
        // Handle authentication (non-blocking)
        if (!crypto_manager.is_authenticated()) {
            if (!auth_in_progress && (now - last_auth_attempt) >= 5) {
                if (config.mode == "client") {
                    Logger::log(LogLevel::DEBUG, "Client starting authentication...");
                    if (attempt_authentication()) {
                        auth_in_progress = true;
                        last_auth_attempt = now;
                    }
                } else {
                    Logger::log(LogLevel::DEBUG, "Server waiting for client authentication");
                    last_auth_attempt = now;
                }
            }
        }
        
        // Handle socket data (authentication or regular data)
        if (activity > 0 && socket_manager.is_socket_connected() && 
            FD_ISSET(socket_manager.get_fd(), &read_fds)) {
            
            ssize_t bytes_read = socket_manager.receive_data(socket_buffer, sizeof(socket_buffer));
            if (bytes_read > 0) {
                if (handle_encrypted_packet(socket_buffer, bytes_read)) {
                    if (auth_in_progress && crypto_manager.is_authenticated()) {
                        Logger::log(LogLevel::INFO, "Authentication successful");
                        auth_in_progress = false;
                    }
                }
                update_activity();
            } else if (bytes_read == 0) {
                Logger::log(LogLevel::INFO, "Socket connection lost");
                auth_in_progress = false;
            }
        }
        
        // Handle TUN data (only if authenticated)
        if (activity > 0 && crypto_manager.is_authenticated() && 
            tun_manager.get_fd() >= 0 && FD_ISSET(tun_manager.get_fd(), &read_fds)) {
            
            ssize_t bytes_read = tun_manager.read_packet(tun_buffer, sizeof(tun_buffer));
            if (bytes_read > 0) {
                if (socket_manager.is_socket_connected()) {
                    if (send_encrypted_data(tun_buffer, bytes_read)) {
                        tun_to_socket_packets.fetch_add(1);
                        tun_to_socket_bytes.fetch_add(bytes_read);
                        update_activity();
                        Logger::log(LogLevel::DEBUG, 
                                   "Forwarded TUN->Socket: " + std::to_string(bytes_read) + " bytes");
                    }
                }
            }
        }
        
        // Handle client reconnection
        if (config.mode == "client" && !socket_manager.is_socket_connected()) {
            if ((now - last_auth_attempt) >= config.reconnect_interval) {
                attempt_reconnection();
                last_auth_attempt = now;
                auth_in_progress = false;
            }
        }
        
        // Handle keepalive
        if (config.enable_keepalive && socket_manager.is_socket_connected() && 
            crypto_manager.is_authenticated()) {
            if ((now - last_keepalive) >= 30) {
                if (!is_connection_healthy()) {
                    Logger::log(LogLevel::DEBUG, "Sending keepalive");
                    send_keepalive();
                }
                last_keepalive = now;
            }
        }
    }
    
    Logger::log(LogLevel::DEBUG, "Main loop stopped");
}

bool Bridge::attempt_authentication() {
    if (config.mode != "client") {
        return false;
    }
    
    if (!socket_manager.is_socket_connected()) {
        return false;
    }
    
    char auth_buffer[BUFFER_SIZE];
    size_t auth_size = sizeof(auth_buffer);
    
    if (!crypto_manager.create_auth_request(auth_buffer, auth_size)) {
        Logger::log(LogLevel::ERROR, "Failed to create authentication request");
        return false;
    }
    
    Logger::log(LogLevel::DEBUG, "Created authentication request, size: " + std::to_string(auth_size));
    
    ssize_t sent = socket_manager.send_data(auth_buffer, auth_size);
    if (sent != (ssize_t)auth_size) {
        Logger::log(LogLevel::ERROR, "Failed to send authentication request");
        return false;
    }
    
    Logger::log(LogLevel::DEBUG, "Authentication request sent, waiting for response");
    return true;
}

bool Bridge::perform_authentication() {
    // In single-threaded version, this is handled by main_loop
    Logger::log(LogLevel::DEBUG, "Authentication handled by main loop");
    return true;
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
    } else if ((type == PacketType::AUTH_RESPONSE || type == PacketType::AUTH_SUCCESS) && config.mode == "client") {
        Logger::log(LogLevel::DEBUG, "Client processing AUTH_RESPONSE/AUTH_SUCCESS");
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
