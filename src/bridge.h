#ifndef BRIDGE_H
#define BRIDGE_H

#include "utils.h"
#include "tun_manager.h"
#include "socket_manager.h"
#include "crypto_manager.h"
#include <chrono>

class Bridge {
private:
    TunManager& tun_manager;
    SocketManager& socket_manager;
    CryptoManager& crypto_manager;
    
    std::atomic<bool> running;
    std::atomic<bool> reconnecting;
    
    std::thread tun_to_socket_thread;
    std::thread socket_to_tun_thread;
    std::thread keepalive_thread;
    std::thread auth_thread;
    
    // Statistics
    std::atomic<uint64_t> tun_to_socket_packets;
    std::atomic<uint64_t> socket_to_tun_packets;
    std::atomic<uint64_t> tun_to_socket_bytes;
    std::atomic<uint64_t> socket_to_tun_bytes;
    
    // Configuration
    Config config;
    
    // Reconnection support
    std::chrono::steady_clock::time_point last_activity;
    std::mutex reconnect_mutex;

public:
    Bridge(TunManager& tun_mgr, SocketManager& socket_mgr, CryptoManager& crypto_mgr, const Config& cfg);
    ~Bridge();
    
    // Start bridging (blocking)
    bool start();
    
    // Stop bridging
    void stop();
    
    // Check if bridge is running
    bool is_running() const { return running.load(); }
    
    // Print statistics
    void print_statistics() const;
    
    // Reset statistics
    void reset_statistics();

private:
    // Main forwarding loops
    void tun_to_socket_loop();
    void socket_to_tun_loop();
    void keepalive_loop();
    void auth_loop();
    
    // Authentication handling
    bool perform_authentication();
    bool handle_auth_packet(const char* buffer, size_t size);
    
    // Reconnection logic (for client mode)
    bool attempt_reconnection();
    
    // Update activity timestamp
    void update_activity();
    
    // Check connection health
    bool is_connection_healthy();
    
    // Send keepalive packet
    bool send_keepalive();
    
    // Handle encrypted packets
    bool handle_encrypted_packet(const char* buffer, size_t size);
    
    // Send encrypted data
    bool send_encrypted_data(const char* data, size_t size);
};

#endif // BRIDGE_H
