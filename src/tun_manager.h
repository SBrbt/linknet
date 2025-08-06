#ifndef TUN_MANAGER_H
#define TUN_MANAGER_H

#include "utils.h"
#include "command_executor.h"

class TunManager {
private:
    int tun_fd;
    std::string dev_name;
    std::string local_ip;
    std::string netmask;
    bool is_open;
    mutable std::mutex tun_mutex;  // Thread safety

public:
    TunManager();
    ~TunManager();
    
    // Disable copy constructor and assignment operator
    TunManager(const TunManager&) = delete;
    TunManager& operator=(const TunManager&) = delete;
    
    // Create TUN interface
    bool create_tun(const std::string& dev_name);
    
    // Configure TUN interface with IP
    bool configure_interface(const std::string& local_ip,
                            const std::string& remote_ip,
                           const std::string& netmask = "255.255.255.0");
    
    // Read packet from TUN interface with timeout
    ssize_t read_packet(char* buffer, size_t buffer_size, int timeout_ms = -1);
    
    // Write packet to TUN interface (thread-safe)
    ssize_t write_packet(const char* buffer, size_t packet_size);
    
    // Get TUN file descriptor (thread-safe)
    int get_fd() const { 
        std::lock_guard<std::mutex> lock(tun_mutex);
        return tun_fd; 
    }
    
    // Get device name (thread-safe)
    const std::string& get_device_name() const { 
        std::lock_guard<std::mutex> lock(tun_mutex);
        return dev_name; 
    }
    
    // Check if TUN is open (thread-safe)
    bool is_opened() const { 
        std::lock_guard<std::mutex> lock(tun_mutex);
        return is_open; 
    }
    
    // Close TUN interface (thread-safe)
    void close_tun();
    
    // Validate packet before processing
    bool validate_packet(const char* buffer, size_t size) const;
    
private:
    // Execute system command (with better error handling)
    bool execute_command(const std::string& command);
    
    // Set file descriptor to non-blocking mode
    bool set_non_blocking(int fd);
};

#endif // TUN_MANAGER_H
