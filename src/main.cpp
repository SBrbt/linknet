#include "utils.h"
#include "tun_manager.h"
#include "socket_manager.h"
#include "bridge.h"
#include "crypto_manager.h"
#include "route_manager.h"
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
    
    exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --mode MODE         Operation mode: 'client' or 'server' (required)\n";
    std::cout << "  --dev DEVICE        TUN device name (default: tun0)\n";
    std::cout << "  --port PORT         TCP port (default: 51860)\n";
    std::cout << "  --remote-ip IP      Remote server IP (required for client mode)\n";
    std::cout << "  --local-tun-ip IP   Local TUN IP address (required)\n";
    std::cout << "  --remote-tun-ip IP  Remote TUN IP address (required)\n";
    std::cout << "  --psk KEY           Pre-shared key for encryption (required)\n";
    std::cout << "  --psk-file FILE     Read pre-shared key from file\n";
    std::cout << "  --no-encryption     Disable encryption (not recommended)\n";
    std::cout << "  --generate-psk      Generate a random pre-shared key\n";
    std::cout << "  --enable-route      Route remote-ip through TUN interface\n";
    std::cout << "  --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  Server mode:\n";
    std::cout << "    sudo " << program_name << " --mode server --dev tun0 --port 51860 \\\n";
    std::cout << "                        --local-tun-ip 10.0.1.1 --remote-tun-ip 10.0.1.2 \\\n";
    std::cout << "                        --psk \"your-secret-key-here\" --enable-route\n\n";
    std::cout << "  Client mode:\n";
    std::cout << "    sudo " << program_name << " --mode client --dev tun0 --remote-ip 1.2.3.4 \\\n";
    std::cout << "                        --port 51860 --local-tun-ip 10.0.1.2 --remote-tun-ip 10.0.1.1 \\\n";
    std::cout << "                        --psk \"your-secret-key-here\" --enable-route\n\n";
}

bool parse_arguments(int argc, char* argv[], Config& config) {
    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"dev", required_argument, 0, 'd'},
        {"port", required_argument, 0, 'p'},
        {"remote-ip", required_argument, 0, 'r'},
        {"local-tun-ip", required_argument, 0, 'l'},
        {"remote-tun-ip", required_argument, 0, 't'},
        {"psk", required_argument, 0, 'k'},
        {"psk-file", required_argument, 0, 'f'},
        {"enable-route", no_argument, 0, 'R'},
        {"no-encryption", no_argument, 0, 'n'},
        {"generate-psk", no_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "m:d:p:r:l:t:k:f:Rngh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'm':
                config.mode = optarg;
                break;
            case 'd':
                config.dev_name = optarg;
                break;
            case 'p':
                config.port = std::atoi(optarg);
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
                config.psk_file = optarg;
                break;
            case 'R':
                config.enable_auto_route = true;
                break;
            case 'n':
                config.enable_encryption = false;
                break;
            case 'g':
                std::cout << CryptoManager::generate_psk() << std::endl;
                return false;
            case 'h':
                print_usage(argv[0]);
                return false;
            case '?':
                print_usage(argv[0]);
                return false;
            default:
                break;
        }
    }
    
    return true;
}

bool validate_config(Config& config) {
    // Check mode
    if (config.mode != "client" && config.mode != "server") {
        Logger::log(LogLevel::ERROR, "Mode must be 'client' or 'server'");
        return false;
    }
    
    // Check device name
    if (config.dev_name.empty()) {
        Logger::log(LogLevel::ERROR, "Device name is required");
        return false;
    }
    
    // Check port
    if (config.port <= 0 || config.port > 65535) {
        Logger::log(LogLevel::ERROR, "Port must be between 1 and 65535");
        return false;
    }
    
    // Check local TUN IP
    if (config.local_ip.empty() || !NetworkUtils::is_valid_ip(config.local_ip)) {
        Logger::log(LogLevel::ERROR, "Valid local TUN IP address is required");
        return false;
    }
    
    // Check remote TUN IP
    if (config.remote_tun_ip.empty() || !NetworkUtils::is_valid_ip(config.remote_tun_ip)) {
        Logger::log(LogLevel::ERROR, "Valid remote TUN IP address is required");
        return false;
    }
    
    // Client mode specific checks
    if (config.mode == "client") {
        if (config.remote_ip.empty() || !NetworkUtils::is_valid_ip(config.remote_ip)) {
            Logger::log(LogLevel::ERROR, "Valid remote server IP address is required for client mode");
            return false;
        }
    }
    
    // Encryption checks
    if (config.enable_encryption) {
        if (config.psk.empty() && config.psk_file.empty()) {
            Logger::log(LogLevel::ERROR, "Pre-shared key is required when encryption is enabled");
            Logger::log(LogLevel::ERROR, "Use --psk <key> or --psk-file <file> or --generate-psk");
            return false;
        }
        
        if (!config.psk_file.empty()) {
            std::ifstream psk_file(config.psk_file);
            if (!psk_file.is_open()) {
                Logger::log(LogLevel::ERROR, "Cannot open PSK file: " + config.psk_file);
                return false;
            }
            std::getline(psk_file, config.psk);
            if (config.psk.empty()) {
                Logger::log(LogLevel::ERROR, "PSK file is empty");
                return false;
            }
        }
        
        if (config.psk.length() < 16) {
            Logger::log(LogLevel::ERROR, "Pre-shared key must be at least 16 characters");
            return false;
        }
    }
    
    return true;
}

void print_config(const Config& config) {
    Logger::log(LogLevel::INFO, "Configuration:");
    Logger::log(LogLevel::INFO, "  Mode: " + config.mode);
    Logger::log(LogLevel::INFO, "  Device: " + config.dev_name);
    Logger::log(LogLevel::INFO, "  Port: " + std::to_string(config.port));
    Logger::log(LogLevel::INFO, "  Local TUN IP: " + config.local_ip);
    Logger::log(LogLevel::INFO, "  Remote TUN IP: " + config.remote_tun_ip);
    Logger::log(LogLevel::INFO, "  Encryption: " + std::string(config.enable_encryption ? "Enabled" : "Disabled"));
    Logger::log(LogLevel::INFO, "  Auto-routing: " + std::string(config.enable_auto_route ? "Enabled" : "Disabled"));
    if (config.mode == "client") {
        Logger::log(LogLevel::INFO, "  Remote Server IP: " + config.remote_ip);
    }
    if (config.enable_encryption && !config.psk.empty()) {
        Logger::log(LogLevel::INFO, "  PSK Length: " + std::to_string(config.psk.length()) + " characters");
    }
    if (config.enable_auto_route) {
        Logger::log(LogLevel::INFO, "  Will route remote-ip (" + config.remote_tun_ip + ") through TUN interface");
    }
}

int main(int argc, char* argv[]) {
    // Set default configuration
    Config config;
    config.dev_name = "tun0";
    
    // Parse command line arguments
    if (!parse_arguments(argc, argv, config)) {
        return 1;
    }
    
    // Check if running as root (only for actual operation, not help/psk generation)
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
        Logger::log(LogLevel::WARNING, "Running without encryption - not recommended for production");
    }
    
    // Set up network connection based on mode
    bool connection_ready = false;
    
    if (config.mode == "server") {
        // Server mode: start listening
        if (!socket_manager.start_server(config.port)) {
            Logger::log(LogLevel::ERROR, "Failed to start server");
            return 1;
        }
        
        Logger::log(LogLevel::INFO, "Waiting for client connection...");
        if (!socket_manager.accept_connection()) {
            Logger::log(LogLevel::ERROR, "Failed to accept client connection");
            return 1;
        }
        
        connection_ready = true;
        
    } else if (config.mode == "client") {
        // Client mode: connect to server
        if (!socket_manager.connect_to_server(config.remote_ip, config.port)) {
            Logger::log(LogLevel::WARNING, "Initial connection failed, will retry in bridge");
        } else {
            connection_ready = true;
        }
    }
    
    if (connection_ready) {
        Logger::log(LogLevel::INFO, "Network connection established");
        
        // Set up routing if enabled
        if (config.enable_auto_route) {
            Logger::log(LogLevel::INFO, "Setting up automatic routing for remote IP...");
            
            if (!route_manager.initialize(config.dev_name, config.remote_tun_ip)) {
                Logger::log(LogLevel::ERROR, "Failed to initialize route manager");
                return 1;
            }
            
            // Create a single route for the remote TUN IP
            std::vector<std::string> route_target = {config.remote_tun_ip + "/32"};
            
            if (!route_manager.add_tun_routes(route_target)) {
                Logger::log(LogLevel::WARNING, "Failed to configure route for remote IP");
            } else {
                Logger::log(LogLevel::INFO, "Route configured successfully for " + config.remote_tun_ip);
            }
            
            // Print current routing table for verification
            route_manager.print_routes();
        }
    }
    
    // Create and start bridge
    Bridge bridge(tun_manager, socket_manager, crypto_manager, config);
    g_bridge = &bridge;
    
    Logger::log(LogLevel::INFO, "Starting TUN bridge...");
    
    // Start a statistics thread
    std::thread stats_thread([&bridge]() {
        while (bridge.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            if (bridge.is_running()) {
                bridge.print_statistics();
            }
        }
    });
    
    // Start the bridge (this will block)
    bridge.start();
    
    // Wait for statistics thread
    if (stats_thread.joinable()) {
        stats_thread.join();
    }
    
    // Final statistics
    bridge.print_statistics();
    
    // Restore routes
    if (config.enable_auto_route) {
        Logger::log(LogLevel::INFO, "Restoring original routes...");
        route_manager.restore_original_routes();
    }
    
    Logger::log(LogLevel::INFO, "Program terminated cleanly");
    return 0;
}
