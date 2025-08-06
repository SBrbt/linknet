#!/bin/bash

# LinkNet Interactive Installation Script
# This script will guide you through installing LinkNet as a systemd service

set -e

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="/etc"
SERVICE_DIR="/etc/systemd/system"
BINARY_NAME="linknet"
SERVICE_NAME="linknet"
PSK_FILE="/etc/linknet.psk"

echo "=== LinkNet Installation Script ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

# Check if binary exists
if [ ! -f "./${BINARY_NAME}" ]; then
    echo "Error: ${BINARY_NAME} not found in current directory"
    echo "Please run 'make' first to build the project"
    exit 1
fi

echo "Welcome to LinkNet installation!"
echo ""
echo "This script will:"
echo "1. Install the tun_bridge binary to ${INSTALL_DIR}"
echo "2. Create a systemd service"
echo "3. Generate configuration files"
echo ""

read -p "Continue with installation? (y/N): " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Installation cancelled"
    exit 0
fi

# Step 1: Install binary
echo ""
echo "=== Installing Binary ==="
cp "${BINARY_NAME}" "${INSTALL_DIR}/"
chmod +x "${INSTALL_DIR}/${BINARY_NAME}"
echo "✓ Binary installed to ${INSTALL_DIR}/${BINARY_NAME}"

# Step 2: Choose installation type
echo ""
echo "=== Configuration Setup ==="
echo "Choose installation type:"
echo "1) Server mode"
echo "2) Client mode"
echo "3) Manual configuration (create config template only)"
echo ""

while true; do
    read -p "Select option (1-3): " choice
    case $choice in
        1|2|3) break;;
        *) echo "Please enter 1, 2, or 3";;
    esac
done

# Generate PSK
echo ""
echo "=== Security Setup ==="
PSK=$(openssl rand -hex 32)
echo "Generated secure pre-shared key"

# Save PSK to file
echo "${PSK}" > "${PSK_FILE}"
chmod 600 "${PSK_FILE}"
echo "✓ PSK saved to ${PSK_FILE} (secure permissions)"
echo ""

case $choice in
    1)
        echo "=== Server Configuration ==="
        read -p "Enter server port (default: 51860): " PORT
        PORT=${PORT:-51860}
        
        read -p "Enter local TUN IP (default: 10.0.1.1): " LOCAL_IP
        LOCAL_IP=${LOCAL_IP:-10.0.1.1}
        
        read -p "Enter remote TUN IP (default: 10.0.1.2): " REMOTE_IP
        REMOTE_IP=${REMOTE_IP:-10.0.1.2}
        
        # Create server config
        cat > "${CONFIG_DIR}/${SERVICE_NAME}.conf" << EOF
# LinkNet Server Configuration
MODE=server
PORT=${PORT}
LOCAL_TUN_IP=${LOCAL_IP}
REMOTE_TUN_IP=${REMOTE_IP}
PSK_FILE=${PSK_FILE}
DEVICE=tun0
LOG_LEVEL=info
EOF
        
        # Create server service
        cat > "${SERVICE_DIR}/${SERVICE_NAME}.service" << EOF
[Unit]
Description=LinkNet TUN Bridge Server
After=network.target

[Service]
Type=simple
User=root
ExecStart=${INSTALL_DIR}/${BINARY_NAME} --mode server --port ${PORT} --local-tun-ip ${LOCAL_IP} --remote-tun-ip ${REMOTE_IP} --psk-file ${PSK_FILE}
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
        
        echo "✓ Server configuration created"
        ;;
        
    2)
        echo "=== Client Configuration ==="
        read -p "Enter server IP address: " SERVER_IP
        if [ -z "$SERVER_IP" ]; then
            echo "Error: Server IP is required for client mode"
            exit 1
        fi
        
        read -p "Enter server port (default: 51860): " PORT
        PORT=${PORT:-51860}
        
        read -p "Enter local TUN IP (default: 10.0.1.2): " LOCAL_IP
        LOCAL_IP=${LOCAL_IP:-10.0.1.2}
        
        read -p "Enter remote TUN IP (default: 10.0.1.1): " REMOTE_IP
        REMOTE_IP=${REMOTE_IP:-10.0.1.1}
        
        # Create client config
        cat > "${CONFIG_DIR}/${SERVICE_NAME}.conf" << EOF
# LinkNet Client Configuration
MODE=client
REMOTE_IP=${SERVER_IP}
PORT=${PORT}
LOCAL_TUN_IP=${LOCAL_IP}
REMOTE_TUN_IP=${REMOTE_IP}
PSK_FILE=${PSK_FILE}
DEVICE=tun0
LOG_LEVEL=info
EOF
        
        # Create client service
        cat > "${SERVICE_DIR}/${SERVICE_NAME}.service" << EOF
[Unit]
Description=LinkNet TUN Bridge Client
After=network.target

[Service]
Type=simple
User=root
ExecStart=${INSTALL_DIR}/${BINARY_NAME} --mode client --remote-ip ${SERVER_IP} --port ${PORT} --local-tun-ip ${LOCAL_IP} --remote-tun-ip ${REMOTE_IP} --psk-file ${PSK_FILE}
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
        
        echo "✓ Client configuration created"
        ;;
        
    3)
        # Create template config
        cat > "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" << EOF
# LinkNet Configuration Template
# Copy this file to ${SERVICE_NAME}.conf and customize

# Mode: server or client
MODE=server

# Network settings
PORT=51860
LOCAL_TUN_IP=10.0.1.1
REMOTE_TUN_IP=10.0.1.2

# For client mode, specify server IP
#REMOTE_IP=1.2.3.4

# Security (PSK file path)
PSK_FILE=${PSK_FILE}

# Optional settings
DEVICE=tun0
LOG_LEVEL=info
#PERFORMANCE_MODE=true
#NO_ENCRYPTION=false
EOF
        
        echo "✓ Configuration template created at ${CONFIG_DIR}/${SERVICE_NAME}.conf.template"
        echo "✓ Please copy and customize it to ${CONFIG_DIR}/${SERVICE_NAME}.conf"
        echo "✓ Then manually create systemd service file"
        ;;
esac

if [ $choice -ne 3 ]; then
    # Step 3: Install and enable service
    echo ""
    echo "=== Service Installation ==="
    systemctl daemon-reload
    echo "✓ Systemd service installed"
    
    read -p "Enable service to start on boot? (Y/n): " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        systemctl enable "${SERVICE_NAME}"
        echo "✓ Service enabled for auto-start"
    fi
    
    read -p "Start service now? (Y/n): " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        systemctl start "${SERVICE_NAME}"
        echo "✓ Service started"
        
        sleep 2
        if systemctl is-active --quiet "${SERVICE_NAME}"; then
            echo "✓ Service is running successfully"
        else
            echo "⚠ Service may have failed to start. Check with: journalctl -u ${SERVICE_NAME}"
        fi
    fi
fi

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Configuration file: ${CONFIG_DIR}/${SERVICE_NAME}.conf"
echo "PSK file: ${PSK_FILE}"
if [ $choice -ne 3 ]; then
    echo "Service file: ${SERVICE_DIR}/${SERVICE_NAME}.service"
    echo ""
    echo "Useful commands:"
    echo "  Start service:    sudo systemctl start ${SERVICE_NAME}"
    echo "  Stop service:     sudo systemctl stop ${SERVICE_NAME}"
    echo "  Check status:     sudo systemctl status ${SERVICE_NAME}"
    echo "  View logs:        sudo journalctl -u ${SERVICE_NAME} -f"
    echo "  Disable service:  sudo systemctl disable ${SERVICE_NAME}"
fi
echo ""
echo "To uninstall, run: sudo ./scripts/uninstall.sh"
echo ""

# Show PSK info
echo "IMPORTANT: Your pre-shared key is stored securely in ${PSK_FILE}"
echo "Share this file securely with the remote peer for authentication."
echo "You can view the PSK with: sudo cat ${PSK_FILE}"
echo "You can also generate a new PSK with: openssl rand -hex 32 | sudo tee ${PSK_FILE}"
