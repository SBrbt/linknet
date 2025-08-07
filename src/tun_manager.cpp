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
                                    const std::string& netmask,
                                    int mtu) {
    if (!is_open) {
        Logger::log(LogLevel::ERROR, "TUN interface not open");
        return false;
    }
    
    this->local_ip = local_ip;
    this->netmask = netmask;
    
    // Batch configuration commands for better performance
    std::vector<std::string> config_commands;
    
    // Set proper MTU for VPN (account for encryption overhead)
    config_commands.push_back("ip link set " + dev_name + " mtu " + std::to_string(mtu));
    
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

ssize_t TunManager::read_packet(char* buffer, size_t buffer_size, int timeout_ms) {
    std::lock_guard<std::mutex> lock(tun_mutex);
    
    if (!is_open || tun_fd < 0) {
        return -1;
    }
    
    // Use select for timeout if specified
    if (timeout_ms >= 0) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(tun_fd, &read_fds);
        
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        
        int result = select(tun_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (result <= 0) {
            return result; // timeout or error
        }
    }
    
    ssize_t bytes_read = read(tun_fd, buffer, buffer_size);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::log(LogLevel::ERROR, "Failed to read from TUN: " + 
                       NetworkUtils::get_error_string(errno));
        }
        return -1;
    }
    
    // Validate packet
    if (bytes_read > 0 && !validate_packet(buffer, bytes_read)) {
        Logger::log(LogLevel::WARNING, "Invalid packet received from TUN, dropping");
        return -1;
    }
    
    return bytes_read;
}

ssize_t TunManager::write_packet(const char* buffer, size_t packet_size) {
    std::lock_guard<std::mutex> lock(tun_mutex);
    
    if (!is_open || tun_fd < 0) {
        return -1;
    }
    
    // Validate packet before writing
    if (!validate_packet(buffer, packet_size)) {
        Logger::log(LogLevel::WARNING, "Invalid packet format, refusing to write to TUN");
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
    std::lock_guard<std::mutex> lock(tun_mutex);
    
    if (is_open && tun_fd >= 0) {
        close(tun_fd);
        tun_fd = -1;
        is_open = false;
        
        Logger::log(LogLevel::INFO, "TUN interface closed: " + dev_name);
        
        // TUN interface usually disappears automatically when fd is closed,
        // but try to delete it anyway, suppress error output
        if (!dev_name.empty()) {
            std::string cmd = "ip link delete " + dev_name + " 2>/dev/null || true";
            if (!execute_command(cmd)) {
                Logger::log(LogLevel::DEBUG, "TUN interface " + dev_name + " was already removed");
            }
        }
    }
}

bool TunManager::validate_packet(const char* buffer, size_t size) const {
    if (!buffer || size == 0) {
        return false;
    }
    
    // Basic IP packet validation
    if (size < 20) { // Minimum IP header size
        return false;
    }
    
    // Check IP version (should be 4 or 6)
    uint8_t version = (buffer[0] >> 4) & 0x0F;
    if (version != 4 && version != 6) {
        return false;
    }
    
    // Check packet size limits
    if (size > MTU_SIZE) {
        Logger::log(LogLevel::WARNING, "Packet size exceeds MTU: " + std::to_string(size));
        return false;
    }
    
    return true;
}

bool TunManager::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        Logger::log(LogLevel::ERROR, "Failed to get fd flags: " + NetworkUtils::get_error_string(errno));
        return false;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to set non-blocking: " + NetworkUtils::get_error_string(errno));
        return false;
    }
    
    return true;
}

bool TunManager::execute_command(const std::string& command) {
    Logger::log(LogLevel::DEBUG, "Executing: " + command);
    
    // Use global command executor for better performance
    int result = g_command_executor.execute_command(command);
    
    // Don't log warnings for link delete commands as they might fail normally
    if (result != 0 && command.find("ip link delete") == std::string::npos) {
        Logger::log(LogLevel::WARNING, "Command failed with exit code " + 
                   std::to_string(result) + ": " + command);
    }
    
    return (result == 0);
}
