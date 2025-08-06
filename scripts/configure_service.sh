#!/bin/bash

# TUN Bridge Service Configuration Generator
# This script generates customized systemd service files

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}TUN Bridge Service Configuration Generator${NC}"
echo "=============================================="

# Function to print status
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Default values
MODE=""
LOCAL_IP=""
REMOTE_IP=""
PORT="51860"
PSK_FILE=""
ENABLE_ROUTE="no"

# Interactive configuration
echo "This script will help you generate a customized systemd service file."
echo

# Mode selection
while [[ "$MODE" != "server" && "$MODE" != "client" ]]; do
    read -p "Run as [server/client]: " MODE
    MODE=$(echo "$MODE" | tr '[:upper:]' '[:lower:]')
done

# Network configuration
read -p "Local TUN IP address (e.g., 10.0.1.1): " LOCAL_IP
read -p "Remote TUN IP address (e.g., 10.0.1.2): " REMOTE_IP

if [ "$MODE" = "server" ]; then
    read -p "Listen port [$PORT]: " INPUT_PORT
    PORT=${INPUT_PORT:-$PORT}
else
    read -p "Server host/IP: " SERVER_HOST
    read -p "Server port [$PORT]: " INPUT_PORT
    PORT=${INPUT_PORT:-$PORT}
fi

read -p "Enable routing for remote-ip? [y/N]: " ENABLE_ROUTE
read -p "PSK file path (optional, for encryption): " PSK_FILE

# Generate service file
SERVICE_NAME="tun-bridge"
if [ "$MODE" = "client" ]; then
    SERVICE_NAME="tun-bridge-client"
fi

SERVICE_FILE="${SERVICE_NAME}.service"

print_status "Generating $SERVICE_FILE"

cat > "$SERVICE_FILE" << EOF
[Unit]
Description=TUN Network Bridge $([ "$MODE" = "server" ] && echo "Server" || echo "Client") Service
After=network.target
Wants=network.target

[Service]
Type=simple
User=root
Group=root
EOF

# Build ExecStart command
EXEC_START="/usr/local/bin/tun_bridge --mode $MODE"

if [ "$MODE" = "client" ]; then
    EXEC_START="$EXEC_START --remote-ip $SERVER_HOST"
fi

EXEC_START="$EXEC_START --port $PORT --dev tun0 --local-tun-ip $LOCAL_IP --remote-tun-ip $REMOTE_IP"

if [[ "$ENABLE_ROUTE" =~ ^[Yy] ]]; then
    EXEC_START="$EXEC_START --enable-route"
fi

if [ -n "$PSK_FILE" ]; then
    EXEC_START="$EXEC_START --psk-file \"$PSK_FILE\""
fi

cat >> "$SERVICE_FILE" << EOF
ExecStart=$EXEC_START
ExecStop=/bin/kill -TERM \$MAINPID
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=$SERVICE_NAME

# Security settings
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/dev/net/tun /proc/sys/net
AmbientCapabilities=CAP_NET_ADMIN CAP_NET_RAW

[Install]
WantedBy=multi-user.target
EOF

print_status "Service file generated: $SERVICE_FILE"
echo
echo "To install this service:"
echo "  sudo cp $SERVICE_FILE /etc/systemd/system/"
echo "  sudo systemctl daemon-reload"
echo "  sudo systemctl enable $SERVICE_NAME"
echo "  sudo systemctl start $SERVICE_NAME"
echo
echo "To check status:"
echo "  sudo systemctl status $SERVICE_NAME"
echo "  sudo journalctl -u $SERVICE_NAME -f"
