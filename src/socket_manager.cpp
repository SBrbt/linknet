#include "socket_manager.h"
#include <netinet/tcp.h>

SocketManager::SocketManager() 
    : socket_fd(-1), server_fd(-1), is_server(false), is_connected(false), port(0) {
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));
}

SocketManager::~SocketManager() {
    close_connection();
}

bool SocketManager::start_server(int port) {
    this->port = port;
    this->is_server = true;
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create server socket: " + 
                   NetworkUtils::get_error_string(errno));
        return false;
    }
    
    // Configure socket options
    if (!configure_socket_options(server_fd)) {
        close(server_fd);
        server_fd = -1;
        return false;
    }
    
    // Bind to address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to bind server socket: " + 
                   NetworkUtils::get_error_string(errno));
        close(server_fd);
        server_fd = -1;
        return false;
    }
    
    // Start listening
    if (listen(server_fd, 1) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to listen on server socket: " + 
                   NetworkUtils::get_error_string(errno));
        close(server_fd);
        server_fd = -1;
        return false;
    }
    
    Logger::log(LogLevel::INFO, "Server listening on port " + std::to_string(port));
    return true;
}

bool SocketManager::accept_connection() {
    if (!is_server || server_fd < 0) {
        Logger::log(LogLevel::ERROR, "Server not started");
        return false;
    }
    
    socklen_t client_len = sizeof(client_addr);
    socket_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (socket_fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to accept connection: " + 
                   NetworkUtils::get_error_string(errno));
        return false;
    }
    
    // Configure the accepted socket
    configure_socket_options(socket_fd);
    configure_keepalive();
    
    is_connected = true;
    remote_ip = std::string(inet_ntoa(client_addr.sin_addr));
    
    Logger::log(LogLevel::INFO, "Client connected from " + get_remote_endpoint());
    return true;
}

bool SocketManager::connect_to_server(const std::string& server_ip, int port) {
    this->remote_ip = server_ip;
    this->port = port;
    this->is_server = false;
    
    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create client socket: " + 
                   NetworkUtils::get_error_string(errno));
        return false;
    }
    
    // Configure socket options
    if (!configure_socket_options(socket_fd)) {
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    // Set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        Logger::log(LogLevel::ERROR, "Invalid server IP address: " + server_ip);
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    // Connect to server
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to connect to server: " + 
                   NetworkUtils::get_error_string(errno));
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    configure_keepalive();
    is_connected = true;
    
    Logger::log(LogLevel::INFO, "Connected to server " + get_remote_endpoint());
    return true;
}

ssize_t SocketManager::send_data(const char* buffer, size_t data_size) {
    if (!is_connected || socket_fd < 0) {
        return -1;
    }
    
    ssize_t bytes_sent = send(socket_fd, buffer, data_size, MSG_NOSIGNAL);
    if (bytes_sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::log(LogLevel::ERROR, "Failed to send data: " + 
                       NetworkUtils::get_error_string(errno));
            is_connected = false;
        }
        return -1;
    }
    
    return bytes_sent;
}

ssize_t SocketManager::receive_data(char* buffer, size_t buffer_size) {
    if (!is_connected || socket_fd < 0) {
        return -1;
    }
    
    ssize_t bytes_received = recv(socket_fd, buffer, buffer_size, 0);
    if (bytes_received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::log(LogLevel::ERROR, "Failed to receive data: " + 
                       NetworkUtils::get_error_string(errno));
            is_connected = false;
        }
        return -1;
    } else if (bytes_received == 0) {
        Logger::log(LogLevel::INFO, "Connection closed by peer");
        is_connected = false;
        return 0;
    }
    
    return bytes_received;
}

void SocketManager::close_connection() {
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
    
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    
    is_connected = false;
    
    if (is_server) {
        Logger::log(LogLevel::INFO, "Server socket closed");
    } else {
        Logger::log(LogLevel::INFO, "Client connection closed");
    }
}

bool SocketManager::configure_keepalive() {
    if (socket_fd < 0) {
        return false;
    }
    
    int enable = 1;
    int idle = 60;      // Start keepalive after 60 seconds
    int interval = 10;  // Send keepalive every 10 seconds
    int count = 3;      // Give up after 3 failed attempts
    
    // Enable TCP keepalive
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) < 0) {
        Logger::log(LogLevel::WARNING, "Failed to enable TCP keepalive");
        return false;
    }
    
    // Configure keepalive parameters
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
    
    Logger::log(LogLevel::DEBUG, "TCP keepalive configured");
    return true;
}

std::string SocketManager::get_remote_endpoint() const {
    if (is_server && is_connected) {
        return std::string(inet_ntoa(client_addr.sin_addr)) + ":" + 
               std::to_string(ntohs(client_addr.sin_port));
    } else if (!is_server) {
        return remote_ip + ":" + std::to_string(port);
    }
    return "unknown";
}

bool SocketManager::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

bool SocketManager::configure_socket_options(int fd) {
    int enable = 1;
    
    // Enable address reuse
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        Logger::log(LogLevel::WARNING, "Failed to set SO_REUSEADDR");
    }
    
    // Disable Nagle's algorithm for lower latency
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0) {
        Logger::log(LogLevel::WARNING, "Failed to set TCP_NODELAY");
    }
    
    return true;
}
