#include "tun_manager.h"
#include <cstdlib>

TunManager::TunManager() : tun_fd(-1), is_open(false) {
}

TunManager::~TunManager() {
    close_tun();
}

bool TunManager::create_tun(const std::string& dev_name) {
    struct ifreq ifr;
    int fd;
    
    // Open TUN device
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to open /dev/net/tun: " + 
                   NetworkUtils::get_error_string(errno));
        return false;
    }
    
    // Configure TUN interface
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;  // TUN device, no packet info
    
    if (!dev_name.empty()) {
        strncpy(ifr.ifr_name, dev_name.c_str(), IFNAMSIZ - 1);
    }
    
    // Create the interface
    if (ioctl(fd, TUNSETIFF, (void*)&ifr) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create TUN interface: " + 
                   NetworkUtils::get_error_string(errno));
        close(fd);
        return false;
    }
    
    this->tun_fd = fd;
    this->dev_name = std::string(ifr.ifr_name);
    this->is_open = true;
    
    Logger::log(LogLevel::INFO, "TUN interface created: " + this->dev_name);
    return true;
}

bool TunManager::configure_interface(const std::string& local_ip,
                                    const std::string& remote_ip, 
                                    const std::string& netmask) {
    if (!is_open) {
        Logger::log(LogLevel::ERROR, "TUN interface not open");
        return false;
    }
    
    this->local_ip = local_ip;
    this->netmask = netmask;
    
    // Batch configuration commands for better performance
    std::vector<std::string> config_commands;
    
    // Bring interface up
    config_commands.push_back("ip link set " + dev_name + " up");
    
    // Set IP address
    config_commands.push_back("ip addr add " + local_ip + "/32 dev " + dev_name);
    
    // Add route to remote peer (point-to-point)
    if (!remote_ip.empty()) {
        config_commands.push_back("ip route add " + remote_ip + "/32 dev " + dev_name);
    }
    
    // Execute all commands in batch
    if (!g_command_executor.execute_batch(config_commands)) {
        Logger::log(LogLevel::ERROR, "Failed to configure interface (some commands failed)");
        return false;
    }
    
    Logger::log(LogLevel::INFO, "TUN interface configured: " + dev_name + 
               " with IP " + local_ip);
    return true;
}

ssize_t TunManager::read_packet(char* buffer, size_t buffer_size) {
    if (!is_open) {
        return -1;
    }
    
    ssize_t bytes_read = read(tun_fd, buffer, buffer_size);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::log(LogLevel::ERROR, "Failed to read from TUN: " + 
                       NetworkUtils::get_error_string(errno));
        }
        return -1;
    }
    
    return bytes_read;
}

ssize_t TunManager::write_packet(const char* buffer, size_t packet_size) {
    if (!is_open) {
        return -1;
    }
    
    ssize_t bytes_written = write(tun_fd, buffer, packet_size);
    if (bytes_written < 0) {
        Logger::log(LogLevel::ERROR, "Failed to write to TUN: " + 
                   NetworkUtils::get_error_string(errno));
        return -1;
    }
    
    return bytes_written;
}

void TunManager::close_tun() {
    if (is_open && tun_fd >= 0) {
        close(tun_fd);
        tun_fd = -1;
        is_open = false;
        
        // Remove interface (it should disappear automatically, but let's be sure)
        std::string cmd = "ip link delete " + dev_name + " 2>/dev/null";
        execute_command(cmd);
        
        Logger::log(LogLevel::INFO, "TUN interface closed: " + dev_name);
    }
}

bool TunManager::execute_command(const std::string& command) {
    Logger::log(LogLevel::DEBUG, "Executing: " + command);
    
    // Use global command executor for better performance
    int result = g_command_executor.execute_command(command);
    
    return (result == 0);
}
