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

// Buffer size for packet processing
#define BUFFER_SIZE 4096
#define MTU_SIZE 1500

// Log levels
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

// Logger utility
class Logger {
public:
    static void log(LogLevel level, const std::string& message) {
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
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto local_tm = *std::localtime(&time_t);
        
        std::cout << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") 
                  << " " << level_str << " " << message << std::endl;
    }
};

// Network utilities
class NetworkUtils {
public:
    static bool is_valid_ip(const std::string& ip) {
        struct sockaddr_in sa;
        return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
    }
    
    static std::string get_error_string(int error_code) {
        return std::string(strerror(error_code));
    }
};

// Configuration structure
struct Config {
    std::string mode;           // "client" or "server"
    std::string dev_name;       // TUN device name (e.g., "tun0")
    std::string remote_ip;      // Remote server IP (client mode)
    int port;                   // TCP port
    std::string local_ip;       // Local TUN IP
    std::string remote_tun_ip;  // Remote TUN IP
    std::string netmask;        // TUN netmask
    bool enable_keepalive;      // TCP keepalive
    int reconnect_interval;     // Reconnection interval in seconds
    
    // Encryption settings
    bool enable_encryption;     // Enable encryption
    std::string psk;           // Pre-shared key
    std::string psk_file;      // PSK file path
    
    // Routing settings
    bool enable_auto_route;     // Enable automatic routing for remote-ip
    std::string default_route_interface;      // Save original default route interface
    
    Config() : port(51860), netmask("255.255.255.0"), 
               enable_keepalive(true), reconnect_interval(5),
               enable_encryption(true), enable_auto_route(false) {}
};

#endif // UTILS_H
