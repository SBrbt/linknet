#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <fstream>
#include <sstream>

// Buffer size for packet processing
#define BUFFER_SIZE 4096
#define MTU_SIZE 1408        // TUN MTU: 1500 - VPN overhead (56 + 20 + 16) = 1408

// Log levels
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

// Logger utility
class Logger {
private:
    static std::mutex log_mutex;
    static LogLevel current_level;
    static bool enable_timestamp;
    static std::string log_file;
    
public:
    static void set_log_level(LogLevel level) {
        current_level = level;
    }
    
    static void set_log_file(const std::string& filename) {
        log_file = filename;
    }
    
    static void enable_timestamps(bool enable) {
        enable_timestamp = enable;
    }
    
    static void log(LogLevel level, const std::string& message) {
        if (level < current_level) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(log_mutex);
        
        const char* level_str = "";
        switch (level) {
            case LogLevel::DEBUG:
                level_str = "[DEBUG]";
                break;
            case LogLevel::INFO:
                level_str = "[INFO]";
                break;
            case LogLevel::WARNING:
                level_str = "[WARNING]";
                break;
            case LogLevel::ERROR:
                level_str = "[ERROR]";
                break;
            default:
                level_str = "[UNKNOWN]";
                break;
        }
        
        std::string log_message;
        
        if (enable_timestamp) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto local_tm = *std::localtime(&time_t);
            
            std::ostringstream oss;
            oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") 
                << " " << level_str << " " << message;
            log_message = oss.str();
        } else {
            log_message = std::string(level_str) + " " + message;
        }
        
        // Always output to console
        std::cout << log_message << std::endl;
        
        // Also write to file if specified
        if (!log_file.empty()) {
            std::ofstream file(log_file, std::ios::app);
            if (file.is_open()) {
                file << log_message << std::endl;
                file.close();
            }
        }
    }
};

// Network utilities
class NetworkUtils {
public:
    static bool is_valid_ip(const std::string& ip) {
        struct sockaddr_in sa;
        return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
    }
    
    static bool is_valid_port(int port) {
        return port > 0 && port <= 65535;
    }
    
    static bool is_valid_cidr(const std::string& cidr) {
        size_t slash_pos = cidr.find('/');
        if (slash_pos == std::string::npos) {
            return is_valid_ip(cidr);
        }
        
        std::string ip = cidr.substr(0, slash_pos);
        std::string prefix = cidr.substr(slash_pos + 1);
        
        if (!is_valid_ip(ip)) {
            return false;
        }
        
        try {
            int prefix_len = std::stoi(prefix);
            return prefix_len >= 0 && prefix_len <= 32;
        } catch (...) {
            return false;
        }
    }
    
    static std::string get_error_string(int error_code) {
        return std::string(strerror(error_code));
    }
    
    static std::string sanitize_string(const std::string& input, size_t max_length = 256) {
        std::string result = input;
        if (result.length() > max_length) {
            result = result.substr(0, max_length);
        }
        
        // Remove potential shell injection characters
        for (char& c : result) {
            if (c == ';' || c == '|' || c == '&' || c == '$' || c == '`') {
                c = '_';
            }
        }
        
        return result;
    }
};

// Configuration structure
struct Config {
    std::string mode;           // "client" or "server"
    std::string dev_name;       // TUN device name (e.g., "tun0")
    std::string remote_ip;      // Remote server IP (client mode)
    int port;                   // TCP port
    std::string local_tun_ip;       // Local TUN IP
    std::string remote_tun_ip;  // Remote TUN IP
    std::string netmask;        // TUN netmask
    int tun_mtu;               // TUN interface MTU
    bool enable_keepalive;      // TCP keepalive
    int reconnect_interval;     // Reconnection interval in seconds
    
    // Encryption settings
    bool enable_encryption;     // Enable encryption
    std::string psk;           // Pre-shared key
    std::string psk_file;      // PSK file path
    
    // Routing settings
    bool enable_auto_route;     // Enable automatic routing for remote-ip
    std::string default_route_interface;      // Save original default route interface
    
    Config() : port(51860), netmask("255.255.255.0"), tun_mtu(1408),
               enable_keepalive(true), reconnect_interval(5),
               enable_encryption(true), enable_auto_route(false) {}
               
    // Validate configuration
    std::vector<std::string> validate() const {
        std::vector<std::string> errors;
        
        if (mode != "client" && mode != "server") {
            errors.push_back("Mode must be 'client' or 'server'");
        }
        
        if (mode == "client" && remote_ip.empty()) {
            errors.push_back("Remote IP is required for client mode");
        }
        
        if (!remote_ip.empty() && !NetworkUtils::is_valid_ip(remote_ip)) {
            errors.push_back("Invalid remote IP address: " + remote_ip);
        }
        
        if (!NetworkUtils::is_valid_port(port)) {
            errors.push_back("Invalid port number: " + std::to_string(port));
        }
        
        if (local_tun_ip.empty() || !NetworkUtils::is_valid_ip(local_tun_ip)) {
            errors.push_back("Invalid local TUN IP: " + local_tun_ip);
        }
        
        if (remote_tun_ip.empty() || !NetworkUtils::is_valid_ip(remote_tun_ip)) {
            errors.push_back("Invalid remote TUN IP: " + remote_tun_ip);
        }
        
        if (enable_encryption && psk.empty() && psk_file.empty()) {
            errors.push_back("PSK or PSK file is required when encryption is enabled");
        }
        
        if (reconnect_interval < 1 || reconnect_interval > 300) {
            errors.push_back("Reconnect interval must be between 1 and 300 seconds");
        }
        
        if (dev_name.length() > 15) { // IFNAMSIZ - 1
            errors.push_back("Device name too long (max 15 characters)");
        }
        
        if (tun_mtu < 576 || tun_mtu > 1408) {
            errors.push_back("TUN MTU must be between 576 and 1408 bytes");
        }
        
        return errors;
    }
};

#endif // UTILS_H
