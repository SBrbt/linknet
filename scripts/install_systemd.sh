#!/bin/bash

# TUN Bridge Systemd Installation Script
# This script installs the tun_bridge service as a systemd service

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

echo -e "${BLUE}TUN Bridge Systemd Installation${NC}"
echo "======================================"

# Function to print status
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if binary exists
if [ ! -f "./tun_bridge" ]; then
    print_error "tun_bridge binary not found in current directory"
    echo "Please run 'make' to build the project first"
    exit 1
fi

# Install binary
print_status "Installing binary to $BINARY_PATH"
cp ./tun_bridge "$BINARY_PATH"
chmod +x "$BINARY_PATH"

# Create config directory
print_status "Creating configuration directory: $CONFIG_DIR"
mkdir -p "$CONFIG_DIR"

# Install service files
print_status "Installing systemd service files"
cp tun-bridge.service "$SERVICE_PATH/"
cp tun-bridge-client.service "$SERVICE_PATH/"

# Reload systemd
print_status "Reloading systemd daemon"
systemctl daemon-reload

# Create example configuration files
print_status "Creating example configuration files"

cat > "$CONFIG_DIR/server.conf" << EOF
# TUN Bridge Server Configuration
# Copy this file and modify as needed

# Network settings
LOCAL_IP="10.0.1.1"
REMOTE_IP="10.0.1.2"
NETMASK="255.255.255.0"

# Server settings
PORT="8080"

# Routes (comma-separated CIDR blocks)
ROUTES="192.168.1.0/24,10.0.0.0/8"

# PSK file path (optional, for encryption)
PSK_FILE="/etc/tun-bridge/bridge.psk"

# Log level (DEBUG, INFO, WARNING, ERROR)
LOG_LEVEL="INFO"
EOF

cat > "$CONFIG_DIR/client.conf" << EOF
# TUN Bridge Client Configuration
# Copy this file and modify as needed

# Network settings
LOCAL_IP="10.0.1.2"
REMOTE_IP="10.0.1.1"
NETMASK="255.255.255.0"

# Server connection
SERVER_HOST="SERVER_IP_HERE"
PORT="8080"

# Routes (comma-separated CIDR blocks)
ROUTES="192.168.2.0/24,172.16.0.0/12"

# PSK file path (optional, for encryption)
PSK_FILE="/etc/tun-bridge/bridge.psk"

# Log level (DEBUG, INFO, WARNING, ERROR)
LOG_LEVEL="INFO"
EOF

# Generate sample PSK
print_status "Generating sample PSK file"
if [ -f "./tun_bridge" ]; then
    "$BINARY_PATH" --generate-psk > "$CONFIG_DIR/bridge.psk.example"
    chmod 600 "$CONFIG_DIR/bridge.psk.example"
    print_warning "Sample PSK generated at $CONFIG_DIR/bridge.psk.example"
    print_warning "Copy this to bridge.psk and use the same key on both server and client"
fi

# Set permissions
chmod 644 "$CONFIG_DIR"/*.conf
chmod 600 "$CONFIG_DIR"/*.psk* 2>/dev/null || true

echo
echo -e "${GREEN}Installation completed successfully!${NC}"
echo
echo "Next steps:"
echo "1. Edit configuration files in $CONFIG_DIR"
echo "2. For server mode:"
echo "   - Edit $CONFIG_DIR/server.conf"
echo "   - Modify $SERVICE_PATH/tun-bridge.service if needed"
echo "   - Enable: systemctl enable tun-bridge"
echo "   - Start:  systemctl start tun-bridge"
echo
echo "3. For client mode:"
echo "   - Edit $CONFIG_DIR/client.conf"
echo "   - Modify $SERVICE_PATH/tun-bridge-client.service"
echo "   - Set SERVER_IP in the service file"
echo "   - Enable: systemctl enable tun-bridge-client"
echo "   - Start:  systemctl start tun-bridge-client"
echo
echo "4. For encryption:"
echo "   - Copy $CONFIG_DIR/bridge.psk.example to $CONFIG_DIR/bridge.psk"
echo "   - Use the same PSK file on both server and client"
echo
echo "Service management commands:"
echo "  Status:  systemctl status tun-bridge"
echo "  Logs:    journalctl -u tun-bridge -f"
echo "  Stop:    systemctl stop tun-bridge"
echo "  Restart: systemctl restart tun-bridge"
echo
print_warning "Remember to configure firewall rules for the TUN interface and service port"
