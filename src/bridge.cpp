#include "bridge.h"
#include <sys/epoll.h>
#include <unistd.h>

Bridge::Bridge(TunManager* tun, SocketManager* socket, CryptoManager* crypto)
    : tun_manager(tun), socket_manager(socket), crypto_manager(crypto),
      is_authenticated(false), should_stop(false), auth_in_progress(false), packets_processed(0), bytes_transferred(0),
      last_stats_time(std::chrono::high_resolution_clock::now()),
      total_packets_sent(0), total_packets_received(0), total_bytes_sent(0), 
      total_bytes_received(0), dropped_packets(0), auth_failures(0) {
}

Bridge::~Bridge() {
    stop();
}

bool Bridge::initialize(const std::string& mode, const std::string& remote_ip, int port) {
    this->mode = mode;
    this->remote_ip = remote_ip;
    this->port = port;
    
    Logger::log(LogLevel::INFO, "Bridge initialized in " + mode + " mode");
    return true;
}

bool Bridge::start() {
    should_stop = false;
    
    Logger::log(LogLevel::INFO, "Starting Bridge with multi-threading...");
    
    try {
        // Start all threads
        tun_reader_thread = std::thread(&Bridge::tun_reader_loop, this);
        socket_reader_thread = std::thread(&Bridge::socket_reader_loop, this);
        packet_processor_thread = std::thread(&Bridge::packet_processor_loop, this);
        heartbeat_thread = std::thread(&Bridge::heartbeat_loop, this);
        
        Logger::log(LogLevel::INFO, "All threads started successfully");
        
        // Give threads a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Trigger authentication immediately
        if (mode == "client") {
            Logger::log(LogLevel::INFO, "Initiating client authentication...");
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                handle_authentication();
            }).detach();
        }
        
        return true;
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Failed to start threads: " + std::string(e.what()));
        stop();
        return false;
    }
}

void Bridge::stop() {
    Logger::log(LogLevel::INFO, "Stopping Bridge...");
    
    should_stop = true;
    queue_cv.notify_all();
    
    // Join all threads
    if (tun_reader_thread.joinable()) {
        tun_reader_thread.join();
    }
    if (socket_reader_thread.joinable()) {
        socket_reader_thread.join();
    }
    if (packet_processor_thread.joinable()) {
        packet_processor_thread.join();
    }
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }
    
    Logger::log(LogLevel::INFO, "Bridge stopped");
}

void Bridge::tun_reader_loop() {
    Logger::log(LogLevel::INFO, "TUN reader thread started");
    
    char buffer[2048];
    fd_set read_fds;
    struct timeval timeout;
    
    while (!should_stop) {
        FD_ZERO(&read_fds);
        FD_SET(tun_manager->get_fd(), &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms timeout
        
        int result = select(tun_manager->get_fd() + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (result > 0 && FD_ISSET(tun_manager->get_fd(), &read_fds)) {
            ssize_t bytes_read = tun_manager->read_packet(buffer, sizeof(buffer));
            
            if (bytes_read > 0) {
                std::vector<uint8_t> packet_data(buffer, buffer + bytes_read);
                auto packet = std::make_shared<Packet>(packet_data, Packet::TUN_TO_SOCKET);
                
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    packet_queue.push(packet);
                }
                queue_cv.notify_one();
                
                Logger::log(LogLevel::DEBUG, "TUN packet queued: " + std::to_string(bytes_read) + " bytes");
            }
        } else if (result < 0 && errno != EINTR) {
            Logger::log(LogLevel::ERROR, "TUN select error: " + NetworkUtils::get_error_string(errno));
            break;
        }
    }
    
    Logger::log(LogLevel::INFO, "TUN reader thread stopped");
}

void Bridge::socket_reader_loop() {
    Logger::log(LogLevel::INFO, "Socket reader thread started");
    
    char buffer[2048];
    fd_set read_fds;
    struct timeval timeout;
    
    while (!should_stop) {
        int socket_fd = socket_manager->get_socket_fd();
        if (socket_fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms timeout
        
        int result = select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (result > 0 && FD_ISSET(socket_fd, &read_fds)) {
            ssize_t bytes_read = socket_manager->receive_data(buffer, sizeof(buffer));
            
            if (bytes_read > 0) {
                std::vector<uint8_t> packet_data(buffer, buffer + bytes_read);
                auto packet = std::make_shared<Packet>(packet_data, Packet::SOCKET_TO_TUN);
                
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    packet_queue.push(packet);
                }
                queue_cv.notify_one();
                
                Logger::log(LogLevel::DEBUG, "Socket packet queued: " + std::to_string(bytes_read) + " bytes");
            } else if (bytes_read == 0) {
                Logger::log(LogLevel::WARNING, "Socket connection closed by remote");
                break;
            }
        } else if (result < 0 && errno != EINTR) {
            Logger::log(LogLevel::ERROR, "Socket select error: " + NetworkUtils::get_error_string(errno));
            break;
        }
    }
    
    Logger::log(LogLevel::INFO, "Socket reader thread stopped");
}

void Bridge::packet_processor_loop() {
    Logger::log(LogLevel::INFO, "Packet processor thread started");
    
    while (!should_stop) {
        std::shared_ptr<Packet> packet;
        
        // Wait for packet
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return !packet_queue.empty() || should_stop; });
            
            if (should_stop) break;
            
            if (!packet_queue.empty()) {
                packet = packet_queue.front();
                packet_queue.pop();
            } else {
                continue;
            }
        }
        
        // Process packet
        bool success = false;
        if (packet->type == Packet::TUN_TO_SOCKET) {
            success = process_tun_packet(packet->data);
        } else {
            success = process_socket_packet(packet->data);
        }
        
        if (success) {
            packets_processed++;
            update_statistics(packet->data.size());
        }
    }
    
    Logger::log(LogLevel::INFO, "Packet processor thread stopped");
}

void Bridge::heartbeat_loop() {
    Logger::log(LogLevel::INFO, "Heartbeat thread started");
    
    auto last_heartbeat = std::chrono::steady_clock::now();
    auto last_stats = std::chrono::steady_clock::now();
    
    while (!should_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        
        // Send heartbeat every 10 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() >= 10) {
            if (is_authenticated && socket_manager->get_socket_fd() >= 0) {
                // Send heartbeat packet
                std::string heartbeat = "HEARTBEAT";
                socket_manager->send_data(heartbeat.c_str(), heartbeat.length());
                Logger::log(LogLevel::DEBUG, "Heartbeat sent");
            }
            last_heartbeat = now;
        }
        
        // Print stats every 5 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 5) {
            print_performance_stats();
            last_stats = now;
        }
    }
    
    Logger::log(LogLevel::INFO, "Heartbeat thread stopped");
}

bool Bridge::process_tun_packet(const std::vector<uint8_t>& packet) {
    if (!is_authenticated) {
        return false;
    }
    
    try {
        if (crypto_manager) {
            // Encrypt packet - use dynamic buffer size
            // AES-256-CBC requires: IV(16) + encrypted_data + possible padding(16) + tag(16)
            size_t max_encrypted_size = packet.size() + 64; // Extra space for IV, padding, tag
            std::vector<char> encrypted_buffer(max_encrypted_size);
            size_t encrypted_size = max_encrypted_size;
            
            if (!crypto_manager->encrypt_packet(reinterpret_cast<const char*>(packet.data()), 
                                               packet.size(), encrypted_buffer.data(), encrypted_size)) {
                Logger::log(LogLevel::ERROR, "Failed to encrypt TUN packet, size: " + std::to_string(packet.size()));
                return false;
            }
            
            Logger::log(LogLevel::DEBUG, "Encrypted packet: " + std::to_string(packet.size()) + " -> " + std::to_string(encrypted_size) + " bytes");
            
            if (socket_manager->send_data(encrypted_buffer.data(), encrypted_size) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to send encrypted packet to socket");
                return false;
            }
        } else {
            // Send unencrypted
            if (socket_manager->send_data(reinterpret_cast<const char*>(packet.data()), packet.size()) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to send packet to socket");
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Exception in process_tun_packet: " + std::string(e.what()));
        return false;
    }
}

bool Bridge::process_socket_packet(const std::vector<uint8_t>& packet) {
    // Check if this is an authentication packet
    std::string packet_str(packet.begin(), packet.end());
    if (packet_str.find("AUTH_") == 0) {
        return handle_auth_packet(packet);
    }
    
    if (!is_authenticated) {
        Logger::log(LogLevel::WARNING, "Received data packet before authentication complete, dropping");
        return false;
    }
    
    // Check if this is a heartbeat packet
    if (packet_str == "HEARTBEAT") {
        Logger::log(LogLevel::DEBUG, "Heartbeat received");
        return true;
    }
    
    try {
        if (crypto_manager) {
            // Decrypt packet - use dynamic buffer size
            size_t max_decrypted_size = packet.size() + 64; // Extra space for safety
            std::vector<char> decrypted_buffer(max_decrypted_size);
            size_t decrypted_size = max_decrypted_size;
            
            if (!crypto_manager->decrypt_packet(reinterpret_cast<const char*>(packet.data()), 
                                               packet.size(), decrypted_buffer.data(), decrypted_size)) {
                Logger::log(LogLevel::ERROR, "Failed to decrypt socket packet, size: " + std::to_string(packet.size()));
                return false;
            }
            
            Logger::log(LogLevel::DEBUG, "Decrypted packet: " + std::to_string(packet.size()) + " -> " + std::to_string(decrypted_size) + " bytes");
            
            if (tun_manager->write_packet(decrypted_buffer.data(), decrypted_size) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to write decrypted packet to TUN");
                return false;
            }
        } else {
            // Write unencrypted
            if (tun_manager->write_packet(reinterpret_cast<const char*>(packet.data()), packet.size()) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to write packet to TUN");
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Exception in process_socket_packet: " + std::string(e.what()));
        return false;
    }
}

bool Bridge::handle_authentication() {
    if (auth_in_progress.exchange(true)) {
        Logger::log(LogLevel::DEBUG, "Authentication already in progress, skipping");
        return true;
    }
    
    Logger::log(LogLevel::INFO, "Performing authentication handshake...");
    
    bool result = false;
    if (mode == "client") {
        result = send_auth_request();
    } else {
        // Server waits for client auth request, doesn't initiate
        Logger::log(LogLevel::INFO, "Server waiting for client authentication");
        result = true;
    }
    
    if (!result) {
        auth_in_progress = false;
    }
    
    return result;
}

bool Bridge::handle_auth_packet(const std::vector<uint8_t>& packet) {
    std::string packet_str(packet.begin(), packet.end());
    
    if (packet_str == "AUTH_REQUEST") {
        if (mode == "server") {
            Logger::log(LogLevel::INFO, "Received authentication request from client");
            return send_auth_response();
        } else {
            Logger::log(LogLevel::WARNING, "Client received AUTH_REQUEST - protocol error");
            return false;
        }
    } else if (packet_str == "AUTH_OK") {
        if (mode == "client") {
            Logger::log(LogLevel::INFO, "Received authentication success from server");
            is_authenticated = true;
            auth_in_progress = false;
            
            // Set crypto manager authenticated state if encryption is enabled
            if (crypto_manager) {
                crypto_manager->set_authenticated(true);
                Logger::log(LogLevel::INFO, "Crypto manager authenticated");
            }
            
            Logger::log(LogLevel::INFO, "Client authentication successful");
            return true;
        } else {
            Logger::log(LogLevel::WARNING, "Server received AUTH_OK - protocol error");
            return false;
        }
    }
    
    Logger::log(LogLevel::WARNING, "Unknown authentication packet: " + packet_str);
    return false;
}

bool Bridge::send_auth_request() {
    const std::string auth_message = "AUTH_REQUEST";
    if (socket_manager->send_data(auth_message.c_str(), auth_message.length()) <= 0) {
        Logger::log(LogLevel::ERROR, "Failed to send authentication request");
        return false;
    }
    Logger::log(LogLevel::INFO, "Authentication request sent");
    return true;
}

bool Bridge::send_auth_response() {
    const std::string auth_response = "AUTH_OK";
    if (socket_manager->send_data(auth_response.c_str(), auth_response.length()) <= 0) {
        Logger::log(LogLevel::ERROR, "Failed to send authentication response");
        return false;
    }
    
    is_authenticated = true;
    auth_in_progress = false;
    
    // Set crypto manager authenticated state if encryption is enabled
    if (crypto_manager) {
        crypto_manager->set_authenticated(true);
        Logger::log(LogLevel::INFO, "Crypto manager authenticated");
    }
    
    Logger::log(LogLevel::INFO, "Server authentication successful");
    return true;
}

void Bridge::update_statistics(size_t bytes, bool sent) {
    bytes_transferred.fetch_add(bytes);
    
    if (sent) {
        total_packets_sent++;
        total_bytes_sent.fetch_add(bytes);
    } else {
        total_packets_received++;
        total_bytes_received.fetch_add(bytes);
    }
}

void Bridge::print_performance_stats() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);
    
    if (duration.count() > 0) {
        uint64_t packets = packets_processed.load();
        uint64_t bytes = bytes_transferred.load();
        
        double pps = static_cast<double>(packets) / duration.count();
        double mbps = (static_cast<double>(bytes) * 8.0) / (duration.count() * 1024.0 * 1024.0);
        
        Logger::log(LogLevel::INFO, 
            "Performance Stats - Packets: " + std::to_string(packets) + 
            ", PPS: " + std::to_string(pps) + 
            ", Mbps: " + std::to_string(mbps) +
            ", Sent: " + std::to_string(total_packets_sent.load()) +
            ", Received: " + std::to_string(total_packets_received.load()) +
            ", Dropped: " + std::to_string(dropped_packets.load()) +
            ", Auth Failures: " + std::to_string(auth_failures.load()));
        
        // Reset counters for next interval
        packets_processed = 0;
        bytes_transferred = 0;
        last_stats_time = now;
    }
}

bool Bridge::wait_for_connection(int timeout_seconds) {
    auto start = std::chrono::steady_clock::now();
    while (!is_authenticated && !should_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= timeout_seconds) {
            return false;
        }
    }
    return is_authenticated;
}
