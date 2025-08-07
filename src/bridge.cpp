#include "bridge.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

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
        
        // Send encrypted keepalive every 10 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() >= 10) {
            if (is_authenticated && socket_manager->get_socket_fd() >= 0 && crypto_manager) {
                // Create proper encrypted keepalive packet using CryptoManager
                char keepalive_data[] = "KEEPALIVE";
                char wrapped_buffer[128];
                size_t wrapped_size = sizeof(wrapped_buffer);
                
                if (crypto_manager->wrap_data_packet(keepalive_data, strlen(keepalive_data), 
                                                    wrapped_buffer, wrapped_size)) {
                    socket_manager->send_data(wrapped_buffer, wrapped_size);
                    Logger::log(LogLevel::DEBUG, "Encrypted keepalive sent");
                } else {
                    Logger::log(LogLevel::WARNING, "Failed to create encrypted keepalive packet");
                }
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
            // Use CryptoManager's proper wrap_data_packet (includes HMAC verification)
            size_t max_wrapped_size = packet.size() + 128; // Extra space for header, IV, padding, HMAC
            std::vector<char> wrapped_buffer(max_wrapped_size);
            size_t wrapped_size = max_wrapped_size;
            
            if (!crypto_manager->wrap_data_packet(reinterpret_cast<const char*>(packet.data()), 
                                                 packet.size(), wrapped_buffer.data(), wrapped_size)) {
                Logger::log(LogLevel::ERROR, "Failed to wrap TUN packet, size: " + std::to_string(packet.size()));
                return false;
            }
            
            Logger::log(LogLevel::DEBUG, "Wrapped packet: " + std::to_string(packet.size()) + " -> " + std::to_string(wrapped_size) + " bytes");
            
            if (socket_manager->send_data(wrapped_buffer.data(), wrapped_size) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to send wrapped packet to socket");
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
    // Check if this is an authentication packet (check packet structure properly)
    if (packet.size() >= sizeof(EncryptedHeader)) {
        uint8_t packet_type = packet[0];
        // Check for CryptoManager authentication packet types and validate size
        if ((packet_type == 0x01 || packet_type == 0x02 || packet_type == 0x03 || packet_type == 0x04)) {
            // Additional validation: check if this looks like a real auth packet
            const EncryptedHeader* header = reinterpret_cast<const EncryptedHeader*>(packet.data());
            uint32_t data_length = ntohl(header->data_length);
            
            // Validate packet structure
            if (packet.size() == sizeof(EncryptedHeader) + data_length) {
                Logger::log(LogLevel::DEBUG, "Detected auth packet type: 0x" + std::to_string(packet_type) + 
                           ", size: " + std::to_string(packet.size()));
                return handle_auth_packet(packet);
            } else {
                Logger::log(LogLevel::DEBUG, "Invalid auth packet structure, treating as data packet");
            }
        }
    }
    
    if (!is_authenticated) {
        Logger::log(LogLevel::WARNING, "Received data packet before authentication complete, dropping");
        return false;
    }
    
    // Check if this is a keepalive packet (properly encrypted)
    if (packet.size() >= 1 && packet[0] == 0x20) {
        Logger::log(LogLevel::DEBUG, "Encrypted keepalive received");
        return true;
    }
    
    try {
        if (crypto_manager) {
            // Use CryptoManager's proper unwrap_data_packet (includes HMAC verification)
            size_t max_unwrapped_size = packet.size() + 64; // Extra space for safety
            std::vector<char> unwrapped_buffer(max_unwrapped_size);
            size_t unwrapped_size = max_unwrapped_size;
            
            if (!crypto_manager->unwrap_data_packet(reinterpret_cast<const char*>(packet.data()), 
                                                   packet.size(), unwrapped_buffer.data(), unwrapped_size)) {
                Logger::log(LogLevel::ERROR, "Failed to unwrap socket packet, size: " + std::to_string(packet.size()) + " (HMAC verification failed or PSK mismatch)");
                return false;
            }
            
            Logger::log(LogLevel::DEBUG, "Unwrapped packet: " + std::to_string(packet.size()) + " -> " + std::to_string(unwrapped_size) + " bytes");
            
            if (tun_manager->write_packet(unwrapped_buffer.data(), unwrapped_size) <= 0) {
                Logger::log(LogLevel::WARNING, "Failed to write unwrapped packet to TUN");
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
    
    Logger::log(LogLevel::INFO, "Performing PSK-based authentication handshake...");
    
    bool result = false;
    if (mode == "client") {
        result = send_auth_request();
    } else {
        // Server waits for client auth request, doesn't initiate
        Logger::log(LogLevel::INFO, "Server waiting for client PSK authentication");
        result = true;
    }
    
    if (!result) {
        auth_in_progress = false;
    }
    
    return result;
}

bool Bridge::handle_auth_packet(const std::vector<uint8_t>& packet) {
    // Use CryptoManager's proper authentication protocol instead of simple string matching
    if (!crypto_manager) {
        Logger::log(LogLevel::WARNING, "No crypto manager available for authentication");
        return false;
    }
    
    Logger::log(LogLevel::DEBUG, "Processing auth packet, size: " + std::to_string(packet.size()) + ", type: 0x" + 
                std::to_string(packet.size() > 0 ? (int)packet[0] : -1));
    
    if (mode == "server") {
        // Server handles authentication request from client
        char response_buffer[512];
        size_t response_size = sizeof(response_buffer);
        
        if (crypto_manager->handle_auth_request(reinterpret_cast<const char*>(packet.data()), 
                                               packet.size(), response_buffer, response_size)) {
            // Send authentication response
            if (socket_manager->send_data(response_buffer, response_size) > 0) {
                is_authenticated = true;
                auth_in_progress = false;
                Logger::log(LogLevel::INFO, "Server PSK authentication successful - client verified");
                return true;
            } else {
                Logger::log(LogLevel::ERROR, "Failed to send authentication response");
                auth_failures++;
                return false;
            }
        } else {
            Logger::log(LogLevel::WARNING, "Client authentication failed - PSK mismatch or invalid request");
            auth_failures++;
            return false;
        }
    } else if (mode == "client") {
        // Client handles authentication response from server
        if (crypto_manager->handle_auth_response(reinterpret_cast<const char*>(packet.data()), packet.size())) {
            is_authenticated = true;
            auth_in_progress = false;
            Logger::log(LogLevel::INFO, "Client PSK authentication successful - server verified");
            return true;
        } else {
            Logger::log(LogLevel::WARNING, "Server authentication failed - PSK mismatch or invalid response");
            auth_failures++;
            return false;
        }
    }
    
    Logger::log(LogLevel::WARNING, "Unknown authentication packet or mode");
    return false;
}

bool Bridge::send_auth_request() {
    if (!crypto_manager) {
        Logger::log(LogLevel::ERROR, "No crypto manager available for authentication");
        return false;
    }
    
    // Use CryptoManager's proper PSK-based authentication
    char auth_buffer[512];
    size_t auth_size = sizeof(auth_buffer);
    
    if (crypto_manager->create_auth_request(auth_buffer, auth_size)) {
        if (socket_manager->send_data(auth_buffer, auth_size) > 0) {
            Logger::log(LogLevel::INFO, "PSK-based authentication request sent");
            return true;
        } else {
            Logger::log(LogLevel::ERROR, "Failed to send PSK authentication request");
            return false;
        }
    } else {
        Logger::log(LogLevel::ERROR, "Failed to create PSK authentication request");
        return false;
    }
}

bool Bridge::send_auth_response() {
    // This method is no longer used - authentication responses are handled 
    // directly in handle_auth_packet() using CryptoManager
    Logger::log(LogLevel::WARNING, "Deprecated send_auth_response() called - using CryptoManager instead");
    return false;
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
