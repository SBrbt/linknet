#ifndef ROUTE_MANAGER_H
#define ROUTE_MANAGER_H

#include "utils.h"
#include <vector>
#include <map>

struct RouteEntry {
    std::string network;
    std::string gateway;
    std::string interface;
    int metric;
    
    RouteEntry() : metric(0) {}
    RouteEntry(const std::string& net, const std::string& gw, const std::string& iface, int m = 0)
        : network(net), gateway(gw), interface(iface), metric(m) {}
};

class RouteManager {
private:
    std::string tun_device;
    std::string tun_gateway_ip;
    std::vector<RouteEntry> original_routes;
    std::vector<RouteEntry> added_routes;
    std::string original_default_gateway;
    std::string original_default_interface;
    bool routes_configured;

public:
    RouteManager();
    ~RouteManager();
    
    // Initialize with TUN device info
    bool initialize(const std::string& tun_dev, const std::string& gateway_ip);
    
    // Add route for specific network through TUN
    bool add_tun_route(const std::string& network_cidr);
    
    // Add multiple networks
    bool add_tun_routes(const std::vector<std::string>& networks);
    
    // Remove specific route
    bool remove_tun_route(const std::string& network_cidr);
    
    // Save current routing table
    bool save_original_routes(const std::vector<std::string>& networks);
    
    // Restore original routing table
    bool restore_original_routes();
    
    // Set up default route through TUN (for full VPN mode)
    bool setup_default_route_via_tun();
    
    // Restore original default route
    bool restore_default_route();
    
    // Get current routing table
    std::vector<RouteEntry> get_routing_table();
    
    // Validate network CIDR format
    static bool is_valid_cidr(const std::string& cidr);
    
    // Parse network address and prefix
    static bool parse_cidr(const std::string& cidr, std::string& network, int& prefix);
    
    // Check if route exists
    bool route_exists(const std::string& network_cidr);
    
    // Print current routes (for debugging)
    void print_routes();

private:
    // Execute system command and get output
    std::string execute_command_with_output(const std::string& command);
    
    // Execute system command (return success/failure)
    bool execute_command(const std::string& command);
    
    // Parse route table output
    std::vector<RouteEntry> parse_route_table(const std::string& output);
    
    // Get default route information
    bool get_default_route_info();
    
    // Backup specific route
    bool backup_route(const std::string& network_cidr);
};

#endif // ROUTE_MANAGER_H
