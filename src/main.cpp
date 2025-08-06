#include "utils.h"
#include "tun_manager.h"
#include "socket_manager.h"
#include "bridge.h"
#include "crypto_manager.h"
#include "route_manager.h"
#include "command_executor.h"
#include <signal.h>
#include <getopt.h>
#include <fstream>

// Global objects for signal handling
TunManager* g_tun_manager = nullptr;
SocketManager* g_socket_manager = nullptr;
Bridge* g_bridge = nullptr;
CryptoManager* g_crypto_manager = nullptr;
RouteManager* g_route_manager = nullptr;

void signal_handler(int signal) {
    Logger::log(LogLevel::INFO, "Received signal " + std::to_string(signal) + ", shutting down...");
    
    if (g_bridge) {
        g_bridge->stop();
    }
    
    if (g_route_manager) {
        g_route_manager->restore_original_routes();
    }
    
    if (g_socket_manager) {
        g_socket_manager->close_connection();
    }
    
    if (g_tun_manager) {
        g_tun_manager->close_tun();
    }
    
    // Stop command executor
    g_command_executor.stop();
    
    exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "High-Performance Multi-threaded TUN Bridge\n\n";
    std::cout << "Options:\n";
    std::cout << "  --mode MODE         Operation mode: 'client' or 'server' (required)\n";
    std::cout << "  --dev DEVICE        TUN device name (default: tun0)\n";
    std::cout << "  --port PORT         TCP port (default: 51860)\n";
    std::cout << "  --remote-ip IP      Remote server IP (required for client mode)\n";
    std::cout << "  --local-tun-ip IP   Local TUN IP address (required)\n";
    std::cout << "  --remote-tun-ip IP  Remote TUN IP address (required)\n";
    std::cout << "  --psk KEY           Pre-shared key for encryption (required)\n";
    std::cout << "  --psk-file FILE     Read pre-shared key from file\n";
    std::cout << "  --no-encryption     Disable encryption (for performance testing)\n";
    std::cout << "  --log-level LEVEL   Log level: debug, info, warning, error (default: info)\n";
    std::cout << "  --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  Server: " << program_name << " --mode server --local-tun-ip 10.0.1.1 --remote-tun-ip 10.0.1.2 --psk-file /etc/linknet.psk\n";
    std::cout << "  Client: " << program_name << " --mode client --remote-ip 1.2.3.4 --local-tun-ip 10.0.1.2 --remote-tun-ip 10.0.1.1 --psk-file /etc/linknet.psk\n";
}

struct MainConfig : public Config {
    std::string log_level;
};

bool parse_arguments(int argc, char* argv[], MainConfig& config) {
    // Set defaults
    config.dev_name = "tun0";
    config.port = 51860;
    config.enable_encryption = true;
    config.log_level = "info";
    
    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"dev", required_argument, 0, 'd'},
        {"port", required_argument, 0, 'p'},
        {"remote-ip", required_argument, 0, 'r'},
        {"local-tun-ip", required_argument, 0, 'l'},
        {"remote-tun-ip", required_argument, 0, 't'},
        {"psk", required_argument, 0, 'k'},
        {"psk-file", required_argument, 0, 'f'},
        {"no-encryption", no_argument, 0, 'n'},
        {"log-level", required_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "m:d:p:r:l:t:k:f:nv:h", long_options, nullptr)) != -1) {
        switch (c) {
            case 'm':
                config.mode = optarg;
                break;
            case 'd':
                config.dev_name = optarg;
                break;
            case 'p':
                config.port = std::stoi(optarg);
                break;
            case 'r':
                config.remote_ip = optarg;
                break;
            case 'l':
                config.local_ip = optarg;
                break;
            case 't':
                config.remote_tun_ip = optarg;
                break;
            case 'k':
                config.psk = optarg;
                break;
            case 'f':
                {
                    std::ifstream file(optarg);
                    if (file.is_open()) {
                        std::getline(file, config.psk);
                        file.close();
                    } else {
                        std::cerr << "Error: Cannot read PSK file: " << optarg << std::endl;
                        return false;
                    }
                }
                break;
            case 'n':
                config.enable_encryption = false;
                break;
            case 'v':
                config.log_level = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                return false;
        }
    }
    
    return true;
}

bool validate_config(const MainConfig& config) {
    if (config.mode != "client" && config.mode != "server") {
        std::cerr << "Error: Mode must be 'client' or 'server'" << std::endl;
        return false;
    }
    
    if (config.mode == "client" && config.remote_ip.empty()) {
        std::cerr << "Error: Remote IP is required for client mode" << std::endl;
        return false;
    }
    
    if (config.local_ip.empty()) {
        std::cerr << "Error: Local TUN IP is required" << std::endl;
        return false;
    }
    
    if (config.remote_tun_ip.empty()) {
        std::cerr << "Error: Remote TUN IP is required" << std::endl;
        return false;
    }
    
    if (config.enable_encryption && config.psk.empty()) {
        std::cerr << "Error: PSK is required when encryption is enabled" << std::endl;
        return false;
    }
    
    return true;
}

void print_config(const MainConfig& config) {
    Logger::log(LogLevel::INFO, "=== LinkNet Multi-threaded Bridge Configuration ===");
    Logger::log(LogLevel::INFO, "Mode: " + config.mode);
    Logger::log(LogLevel::INFO, "Device: " + config.dev_name);
    Logger::log(LogLevel::INFO, "Port: " + std::to_string(config.port));
    Logger::log(LogLevel::INFO, "Local TUN IP: " + config.local_ip);
    Logger::log(LogLevel::INFO, "Remote TUN IP: " + config.remote_tun_ip);
    Logger::log(LogLevel::INFO, "Encryption: " + std::string(config.enable_encryption ? "Enabled" : "Disabled"));
    
    if (config.mode == "client") {
        Logger::log(LogLevel::INFO, "Remote Server: " + config.remote_ip + ":" + std::to_string(config.port));
    }
    Logger::log(LogLevel::INFO, "================================================");
}

int main(int argc, char* argv[]) {
    // Set default configuration
    MainConfig config;
    
    // Parse command line arguments
    if (!parse_arguments(argc, argv, config)) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Configure logging (simplified)
    if (config.log_level == "debug") {
        // Debug logging enabled
    } else if (config.log_level == "warning") {
        // Warning level logging
    } else if (config.log_level == "error") {
        // Error level logging
    }
    
    // Check if running as root (only for actual operation, not help)
    if (geteuid() != 0) {
        Logger::log(LogLevel::ERROR, "This program must be run as root (use sudo)");
        return 1;
    }
    
    // Validate configuration
    if (!validate_config(config)) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Print configuration
    print_config(config);
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe signals
    
    // Start command executor for better performance
    g_command_executor.start();
    Logger::log(LogLevel::INFO, "Command executor started");
    
    // Create TUN manager
    TunManager tun_manager;
    g_tun_manager = &tun_manager;
    
    // Create TUN interface
    if (!tun_manager.create_tun(config.dev_name)) {
        Logger::log(LogLevel::ERROR, "Failed to create TUN interface");
        return 1;
    }
    
    // Configure TUN interface
    if (!tun_manager.configure_interface(config.local_ip, config.remote_tun_ip)) {
        Logger::log(LogLevel::ERROR, "Failed to configure TUN interface");
        return 1;
    }
    
    // Create socket manager
    SocketManager socket_manager;
    g_socket_manager = &socket_manager;
    
    // Create crypto manager
    CryptoManager crypto_manager;
    g_crypto_manager = &crypto_manager;
    
    // Create route manager
    RouteManager route_manager;
    g_route_manager = &route_manager;
    
    // Initialize encryption if enabled
    if (config.enable_encryption) {
        if (!crypto_manager.initialize(config.psk)) {
            Logger::log(LogLevel::ERROR, "Failed to initialize encryption");
            return 1;
        }
        Logger::log(LogLevel::INFO, "Encryption initialized");
    } else {
        Logger::log(LogLevel::WARNING, "Running without encryption - for performance testing only");
    }
    
    // Create multi-threaded bridge
    Bridge bridge(&tun_manager, &socket_manager, 
                  config.enable_encryption ? &crypto_manager : nullptr);
    g_bridge = &bridge;
    
    // Set up network connection based on mode
    bool connection_ready = false;
    
    if (config.mode == "server") {
        Logger::log(LogLevel::INFO, "Starting server on port " + std::to_string(config.port));
        
        if (!socket_manager.start_server(config.port)) {
            Logger::log(LogLevel::ERROR, "Failed to start server");
            return 1;
        }
        
        Logger::log(LogLevel::INFO, "Server started, waiting for client connection...");
        
        if (!socket_manager.accept_connection()) {
            Logger::log(LogLevel::ERROR, "Failed to accept client connection");
            return 1;
        }
        
        Logger::log(LogLevel::INFO, "Client connected: " + socket_manager.get_remote_endpoint());
        connection_ready = true;
        
    } else { // client mode
        Logger::log(LogLevel::INFO, "Connecting to server " + config.remote_ip + ":" + std::to_string(config.port));
        
        if (!socket_manager.connect_to_server(config.remote_ip, config.port)) {
            Logger::log(LogLevel::ERROR, "Failed to connect to server");
            return 1;
        }
        
        Logger::log(LogLevel::INFO, "Connected to server");
        connection_ready = true;
    }
    
    if (!connection_ready) {
        Logger::log(LogLevel::ERROR, "Failed to establish connection");
        return 1;
    }
    
    // Configure routes (only if needed for special routing)
    if (config.mode == "client" && config.enable_auto_route) {
        std::string route_target = config.remote_tun_ip + "/32";  // Add /32 for single host route
        if (!route_manager.initialize(config.dev_name, config.remote_tun_ip)) {
            Logger::log(LogLevel::ERROR, "Failed to initialize route manager");
            return 1;
        }
        
        Logger::log(LogLevel::INFO, "Adding TUN routes for: " + route_target);
        std::vector<std::string> routes = {route_target};
        if (!route_manager.add_tun_routes(routes)) {
            Logger::log(LogLevel::DEBUG, "Some routes were not added (may already exist)");
        }
        
        // Print route information
        route_manager.print_routes();
    }
    
    // Initialize and start bridge
    bridge.initialize(config.mode, config.remote_ip, config.port);
    
    if (!bridge.start()) {
        Logger::log(LogLevel::ERROR, "Failed to start bridge");
        return 1;
    }
    
    Logger::log(LogLevel::INFO, "Bridge started successfully");
    Logger::log(LogLevel::INFO, "High-performance multi-threaded bridge is running...");
    
    // Wait for authentication
    if (!bridge.wait_for_connection(30)) {
        Logger::log(LogLevel::ERROR, "Authentication timeout");
        return 1;
    }
    
    Logger::log(LogLevel::INFO, "Bridge authenticated and ready for traffic");
    
    // Main loop - just monitor
    while (bridge.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Check connection status
        if (!bridge.is_connected()) {
            Logger::log(LogLevel::WARNING, "Connection lost, attempting reconnection...");
            // Implement reconnection logic here if needed
        }
    }
    
    // Cleanup
    if (config.mode == "client") {
        route_manager.restore_original_routes();
    }
    
    Logger::log(LogLevel::INFO, "Bridge stopped");
    return 0;
}
