#ifndef BRIDGE_H
#define BRIDGE_H

#include "utils.h"
#include "tun_manager.h"
#include "socket_manager.h"
#include "crypto_manager.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>

// Packet structure for queue
struct Packet {
    std::vector<uint8_t> data;
    enum Type { TUN_TO_SOCKET, SOCKET_TO_TUN } type;
    
    Packet(const std::vector<uint8_t>& d, Type t) : data(d), type(t) {}
};

class Bridge {
private:
    // Components
    TunManager* tun_manager;
    SocketManager* socket_manager;
    CryptoManager* crypto_manager;
    
    // Threading
    std::thread tun_reader_thread;
    std::thread socket_reader_thread;
    std::thread packet_processor_thread;
    std::thread heartbeat_thread;
    
    // Packet queues with locks
    std::queue<std::shared_ptr<Packet>> packet_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    
    // Authentication state
    std::atomic<bool> is_authenticated;
    std::atomic<bool> should_stop;
    std::atomic<uint64_t> packets_processed;
    std::atomic<uint64_t> bytes_transferred;
    
    // Performance monitoring
    std::chrono::high_resolution_clock::time_point last_stats_time;
    std::mutex stats_mutex;
    
    // Connection management
    std::string mode;
    std::string remote_ip;
    int port;
    
    // Threading functions
    void tun_reader_loop();
    void socket_reader_loop();
    void packet_processor_loop();
    void heartbeat_loop();
    
    // Packet processing
    bool process_tun_packet(const std::vector<uint8_t>& packet);
    bool process_socket_packet(const std::vector<uint8_t>& packet);
    
    // Authentication
    bool handle_authentication();
    bool send_auth_request();
    bool send_auth_response();
    
    // Performance monitoring
    void update_statistics(size_t bytes);
    void print_performance_stats();

public:
    Bridge(TunManager* tun, SocketManager* socket, CryptoManager* crypto);
    ~Bridge();
    
    // Control functions
    bool initialize(const std::string& mode, const std::string& remote_ip = "", int port = 51860);
    bool start();
    void stop();
    
    // Status functions
    bool is_running() const { return !should_stop; }
    bool is_connected() const { return is_authenticated; }
    
    // Performance stats
    uint64_t get_packets_processed() const { return packets_processed; }
    uint64_t get_bytes_transferred() const { return bytes_transferred; }
    
    // Connection management
    bool wait_for_connection(int timeout_seconds = 30);
};

#endif // BRIDGE_H
