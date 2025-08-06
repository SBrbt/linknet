#ifndef TUN_MANAGER_H
#define TUN_MANAGER_H

#include "utils.h"

class TunManager {
private:
    int tun_fd;
    std::string dev_name;
    std::string local_ip;
    std::string netmask;
    bool is_open;

public:
    TunManager();
    ~TunManager();
    
    // Create TUN interface
    bool create_tun(const std::string& dev_name);
    
    // Configure TUN interface with IP
    bool configure_interface(const std::string& local_ip, 
                           const std::string& netmask = "255.255.255.0");
    
    // Read packet from TUN interface
    ssize_t read_packet(char* buffer, size_t buffer_size);
    
    // Write packet to TUN interface
    ssize_t write_packet(const char* buffer, size_t packet_size);
    
    // Get TUN file descriptor
    int get_fd() const { return tun_fd; }
    
    // Get device name
    const std::string& get_device_name() const { return dev_name; }
    
    // Check if TUN is open
    bool is_opened() const { return is_open; }
    
    // Close TUN interface
    void close_tun();
    
private:
    // Execute system command
    bool execute_command(const std::string& command);
};

#endif // TUN_MANAGER_H
