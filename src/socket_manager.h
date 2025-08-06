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

public:
    SocketManager();
    ~SocketManager();
    
    // Server mode: start listening
    bool start_server(int port);
    
    // Server mode: accept client connection
    bool accept_connection();
    
    // Client mode: connect to server
    bool connect_to_server(const std::string& server_ip, int port);
    
    // Send data through socket
    ssize_t send_data(const char* buffer, size_t data_size);
    
    // Receive data from socket
    ssize_t receive_data(char* buffer, size_t buffer_size);
    
    // Get socket file descriptor
    int get_fd() const { return socket_fd; }
    
    // Check if connected
    bool is_socket_connected() const { return is_connected; }
    
    // Close socket connection
    void close_connection();
    
    // Configure TCP keepalive
    bool configure_keepalive();
    
    // Get remote endpoint info
    std::string get_remote_endpoint() const;
    
private:
    // Set socket to non-blocking mode
    bool set_non_blocking(int fd);
    
    // Configure socket options
    bool configure_socket_options(int fd);
};

#endif // SOCKET_MANAGER_H
