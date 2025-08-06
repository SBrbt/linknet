#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H

#include "utils.h"

class SocketManager {
private:
    int socket_fd;
    int server_fd;  // For server mode
    bool is_server;
    bool is_connected;
    std::string remote_ip;
    int port;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    mutable std::mutex socket_mutex;  // Thread safety
    std::chrono::steady_clock::time_point last_activity;
    int reconnect_attempts;
    static const int MAX_RECONNECT_ATTEMPTS = 5;

public:
    SocketManager();
    ~SocketManager();
    
    // Disable copy constructor and assignment operator
    SocketManager(const SocketManager&) = delete;
    SocketManager& operator=(const SocketManager&) = delete;
    
    // Server mode: start listening
    bool start_server(int port);
    
    // Server mode: accept client connection
    bool accept_connection();
    
    // Client mode: connect to server (with retry logic)
    bool connect_to_server(const std::string& server_ip, int port);
    
    // Reconnect with exponential backoff
    bool reconnect();
    
    // Send data through socket (thread-safe)
    ssize_t send_data(const char* buffer, size_t data_size);
    
    // Receive data from socket (thread-safe)  
    ssize_t receive_data(char* buffer, size_t buffer_size);
    
    // Check connection health
    bool check_connection_health();
    
    // Get socket file descriptor (thread-safe)
    int get_fd() const { 
        std::lock_guard<std::mutex> lock(socket_mutex);
        return socket_fd; 
    }
    int get_socket_fd() const { 
        std::lock_guard<std::mutex> lock(socket_mutex);
        return socket_fd; 
    }
    
    // Check if connected (thread-safe)
    bool is_socket_connected() const { 
        std::lock_guard<std::mutex> lock(socket_mutex);
        return is_connected; 
    }
    
    // Close socket connection (thread-safe)
    void close_connection();
    
    // Configure TCP keepalive
    bool configure_keepalive();
    
    // Get remote endpoint info (thread-safe)
    std::string get_remote_endpoint() const;
    
    // Get connection statistics
    std::chrono::steady_clock::time_point get_last_activity() const {
        std::lock_guard<std::mutex> lock(socket_mutex);
        return last_activity;
    }
    
private:
    // Set socket to non-blocking mode
    bool set_non_blocking(int fd);
    
    // Configure socket options  
    bool configure_socket_options(int fd);
    
    // Update activity timestamp
    void update_activity() {
        last_activity = std::chrono::steady_clock::now();
    }
    
    // Internal connection attempt
    bool attempt_connection();
};

#endif // SOCKET_MANAGER_H
