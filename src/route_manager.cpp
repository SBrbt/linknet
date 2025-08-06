#include "route_manager.h"
#include <sstream>
#include <fstream>
#include <regex>

RouteManager::RouteManager() : routes_configured(false) {
}

RouteManager::~RouteManager() {
    restore_original_routes();
}

bool RouteManager::initialize(const std::string& tun_dev, const std::string& gateway_ip) {
    tun_device = tun_dev;
    tun_gateway_ip = gateway_ip;
    
    // Get current default route info
    if (!get_default_route_info()) {
        Logger::log(LogLevel::WARNING, "Could not determine original default route");
    }
    
    Logger::log(LogLevel::INFO, "Route manager initialized for device: " + tun_device);
    return true;
}

bool RouteManager::add_tun_route(const std::string& network_cidr) {
    if (!is_valid_cidr(network_cidr)) {
        Logger::log(LogLevel::ERROR, "Invalid network CIDR: " + network_cidr);
        return false;
    }
    
    // Check if route already exists
    if (route_exists(network_cidr)) {
        Logger::log(LogLevel::INFO, "Route already exists: " + network_cidr);
        return true;
    }
    
    // Check if a broader route already covers this destination
    if (has_covering_route(network_cidr)) {
        Logger::log(LogLevel::INFO, "Route for " + network_cidr + " is already covered by a broader route on " + tun_device);
        return true;
    }
    
    // Backup existing route for this network if it exists
    backup_route(network_cidr);
    
    // Add route through TUN interface
    std::string cmd = "ip route add " + network_cidr + " via " + tun_gateway_ip + " dev " + tun_device;
    
    if (execute_command(cmd)) {
        RouteEntry entry(network_cidr, tun_gateway_ip, tun_device);
        added_routes.push_back(entry);
        Logger::log(LogLevel::INFO, "Added route: " + network_cidr + " via " + tun_device);
        return true;
    } else {
        Logger::log(LogLevel::ERROR, "Failed to add route: " + network_cidr);
        return false;
    }
}

bool RouteManager::add_tun_routes(const std::vector<std::string>& networks) {
    bool all_success = true;
    
    // Save current routes before making changes
    save_original_routes(networks);
    
    for (const auto& network : networks) {
        if (!add_tun_route(network)) {
            all_success = false;
        }
    }
    
    if (all_success) {
        routes_configured = true;
        Logger::log(LogLevel::INFO, "All routes configured successfully");
    }
    
    return all_success;
}

bool RouteManager::remove_tun_route(const std::string& network_cidr) {
    std::string cmd = "ip route del " + network_cidr + " via " + tun_gateway_ip + " dev " + tun_device;
    
    if (execute_command(cmd)) {
        // Remove from added_routes list
        added_routes.erase(
            std::remove_if(added_routes.begin(), added_routes.end(),
                [&network_cidr](const RouteEntry& entry) {
                    return entry.network == network_cidr;
                }),
            added_routes.end()
        );
        
        Logger::log(LogLevel::INFO, "Removed route: " + network_cidr);
        return true;
    } else {
        Logger::log(LogLevel::WARNING, "Failed to remove route (may not exist): " + network_cidr);
        return false;
    }
}

bool RouteManager::save_original_routes(const std::vector<std::string>& networks) {
    original_routes.clear();
    
    // Get current routing table
    std::string output = execute_command_with_output("ip route show");
    if (output.empty()) {
        Logger::log(LogLevel::WARNING, "Could not get routing table");
        return false;
    }
    
    // Parse and save routes for networks we're about to modify
    std::vector<RouteEntry> current_routes = parse_route_table(output);
    
    for (const auto& network : networks) {
        std::string network_addr;
        int prefix;
        if (!parse_cidr(network, network_addr, prefix)) {
            continue;
        }
        
        for (const auto& route : current_routes) {
            if (route.network.find(network_addr) != std::string::npos) {
                original_routes.push_back(route);
                Logger::log(LogLevel::DEBUG, "Backed up route: " + route.network + " via " + route.gateway);
            }
        }
    }
    
    return true;
}

bool RouteManager::restore_original_routes() {
    if (!routes_configured) {
        return true;
    }
    
    Logger::log(LogLevel::INFO, "Restoring original routes...");
    
    // Remove all routes we added
    for (const auto& route : added_routes) {
        remove_tun_route(route.network);
    }
    added_routes.clear();
    
    // Restore original routes
    for (const auto& route : original_routes) {
        std::string cmd = "ip route add " + route.network;
        if (!route.gateway.empty() && route.gateway != "0.0.0.0") {
            cmd += " via " + route.gateway;
        }
        if (!route.interface.empty()) {
            cmd += " dev " + route.interface;
        }
        if (route.metric > 0) {
            cmd += " metric " + std::to_string(route.metric);
        }
        
        execute_command(cmd);
        Logger::log(LogLevel::DEBUG, "Restored route: " + route.network);
    }
    
    original_routes.clear();
    routes_configured = false;
    
    Logger::log(LogLevel::INFO, "Route restoration completed");
    return true;
}

bool RouteManager::setup_default_route_via_tun() {
    if (!get_default_route_info()) {
        Logger::log(LogLevel::ERROR, "Cannot setup default route: no original route info");
        return false;
    }
    
    // Add specific routes for commonly used networks to avoid routing loops
    std::vector<std::string> preserve_networks = {
        "0.0.0.0/1",    // First half of internet
        "128.0.0.0/1"   // Second half of internet
    };
    
    return add_tun_routes(preserve_networks);
}

bool RouteManager::restore_default_route() {
    if (original_default_gateway.empty() || original_default_interface.empty()) {
        Logger::log(LogLevel::WARNING, "No original default route to restore");
        return false;
    }
    
    std::string cmd = "ip route add default via " + original_default_gateway + " dev " + original_default_interface;
    
    if (execute_command(cmd)) {
        Logger::log(LogLevel::INFO, "Restored default route via " + original_default_interface);
        return true;
    }
    
    return false;
}

std::vector<RouteEntry> RouteManager::get_routing_table() {
    std::string output = execute_command_with_output("ip route show");
    return parse_route_table(output);
}

bool RouteManager::is_valid_cidr(const std::string& cidr) {
    std::regex cidr_regex(R"(^(\d{1,3}\.){3}\d{1,3}/\d{1,2}$)");
    return std::regex_match(cidr, cidr_regex);
}

bool RouteManager::parse_cidr(const std::string& cidr, std::string& network, int& prefix) {
    size_t slash_pos = cidr.find('/');
    if (slash_pos == std::string::npos) {
        return false;
    }
    
    network = cidr.substr(0, slash_pos);
    try {
        prefix = std::stoi(cidr.substr(slash_pos + 1));
        return prefix >= 0 && prefix <= 32;
    } catch (const std::exception&) {
        return false;
    }
}

bool RouteManager::ip_in_network(const std::string& ip, const std::string& network, int prefix) {
    // Convert IP addresses to 32-bit integers for comparison
    auto ip_to_int = [](const std::string& ip) -> uint32_t {
        std::istringstream iss(ip);
        std::string octet;
        uint32_t result = 0;
        
        for (int i = 0; i < 4; i++) {
            if (!std::getline(iss, octet, '.')) return 0;
            try {
                int val = std::stoi(octet);
                if (val < 0 || val > 255) return 0;
                result = (result << 8) | val;
            } catch (...) {
                return 0;
            }
        }
        return result;
    };
    
    uint32_t ip_int = ip_to_int(ip);
    uint32_t network_int = ip_to_int(network);
    
    // Create network mask
    uint32_t mask = (prefix == 0) ? 0 : (0xFFFFFFFF << (32 - prefix));
    
    // Check if IP is in the network
    return (ip_int & mask) == (network_int & mask);
}

bool RouteManager::route_exists(const std::string& network_cidr) {
    std::string cmd = "ip route show " + network_cidr;
    std::string output = execute_command_with_output(cmd);
    return !output.empty() && output.find(network_cidr) != std::string::npos;
}

bool RouteManager::has_covering_route(const std::string& network_cidr) {
    // Parse the target network
    std::string target_network;
    int target_prefix;
    if (!parse_cidr(network_cidr, target_network, target_prefix)) {
        return false;
    }
    
    // Get current routing table for the TUN device
    std::string cmd = "ip route show dev " + tun_device;
    std::string output = execute_command_with_output(cmd);
    
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        // Parse each route line
        std::istringstream line_iss(line);
        std::string route_network;
        if (line_iss >> route_network) {
            // Check if this route covers our target
            std::string existing_network;
            int existing_prefix;
            if (parse_cidr(route_network, existing_network, existing_prefix)) {
                // A route covers our target if:
                // 1. It has a smaller or equal prefix (broader or equal coverage)
                // 2. The target IP falls within the existing network range
                if (existing_prefix <= target_prefix && 
                    ip_in_network(target_network, existing_network, existing_prefix)) {
                    Logger::log(LogLevel::DEBUG, "Found covering route: " + route_network + " covers " + network_cidr);
                    return true;
                }
            }
        }
    }
    
    return false;
}

void RouteManager::print_routes() {
    std::vector<RouteEntry> routes = get_routing_table();
    std::cout << "\n=== Current Routing Table ===" << std::endl;
    for (const auto& route : routes) {
        std::cout << route.network << " via " << route.gateway 
                  << " dev " << route.interface;
        if (route.metric > 0) {
            std::cout << " metric " << route.metric;
        }
        std::cout << std::endl;
    }
    std::cout << "==============================\n" << std::endl;
}

std::string RouteManager::execute_command_with_output(const std::string& command) {
    Logger::log(LogLevel::DEBUG, "Executing: " + command);
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    pclose(pipe);
    return result;
}

bool RouteManager::execute_command(const std::string& command) {
    Logger::log(LogLevel::DEBUG, "Executing: " + command);
    int result = system(command.c_str());
    return (result == 0);
}

std::vector<RouteEntry> RouteManager::parse_route_table(const std::string& output) {
    std::vector<RouteEntry> routes;
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        RouteEntry entry;
        std::istringstream line_iss(line);
        std::string token;
        
        // Parse route line (simplified parsing)
        if (line_iss >> entry.network) {
            while (line_iss >> token) {
                if (token == "via" && line_iss >> entry.gateway) {
                    continue;
                } else if (token == "dev" && line_iss >> entry.interface) {
                    continue;
                } else if (token == "metric" && line_iss >> entry.metric) {
                    continue;
                }
            }
            routes.push_back(entry);
        }
    }
    
    return routes;
}

bool RouteManager::get_default_route_info() {
    std::string output = execute_command_with_output("ip route show default");
    if (output.empty()) {
        return false;
    }
    
    std::istringstream iss(output);
    std::string token;
    
    while (iss >> token) {
        if (token == "via" && iss >> original_default_gateway) {
            continue;
        } else if (token == "dev" && iss >> original_default_interface) {
            continue;
        }
    }
    
    return !original_default_gateway.empty() && !original_default_interface.empty();
}

bool RouteManager::backup_route(const std::string& network_cidr) {
    std::string output = execute_command_with_output("ip route show " + network_cidr);
    if (!output.empty()) {
        std::vector<RouteEntry> existing_routes = parse_route_table(output);
        for (const auto& route : existing_routes) {
            if (route.network == network_cidr) {
                original_routes.push_back(route);
                Logger::log(LogLevel::DEBUG, "Backed up existing route: " + network_cidr);
                return true;
            }
        }
    }
    return false;
}
