#!/bin/bash

# TUN Bridge Systemd Uninstallation Script
# This script removes the tun_bridge systemd service

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}Error: This script must be run as root${NC}" 
   echo "Please run: sudo $0"
   exit 1
fi

# Configuration
BINARY_PATH="/usr/local/bin/tun_bridge"
SERVICE_PATH="/etc/systemd/system"
CONFIG_DIR="/etc/tun-bridge"

echo -e "${BLUE}TUN Bridge Systemd Uninstallation${NC}"
echo "========================================"

# Function to print status
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Stop and disable services
for service in tun-bridge tun-bridge-client; do
    if systemctl is-active --quiet "$service" 2>/dev/null; then
        print_status "Stopping service: $service"
        systemctl stop "$service"
    fi
    
    if systemctl is-enabled --quiet "$service" 2>/dev/null; then
        print_status "Disabling service: $service"
        systemctl disable "$service"
    fi
done

# Remove service files
print_status "Removing systemd service files"
rm -f "$SERVICE_PATH/tun-bridge.service"
rm -f "$SERVICE_PATH/tun-bridge-client.service"

# Remove binary
if [ -f "$BINARY_PATH" ]; then
    print_status "Removing binary: $BINARY_PATH"
    rm -f "$BINARY_PATH"
fi

# Reload systemd
print_status "Reloading systemd daemon"
systemctl daemon-reload

# Ask about config directory
echo
read -p "Remove configuration directory $CONFIG_DIR? [y/N]: " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "Removing configuration directory: $CONFIG_DIR"
    rm -rf "$CONFIG_DIR"
else
    print_warning "Configuration directory preserved: $CONFIG_DIR"
fi

echo
echo -e "${GREEN}Uninstallation completed!${NC}"
