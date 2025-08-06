#!/bin/bash

# LinkNet Uninstall Script
# This script will remove LinkNet from your system

set -e

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="/etc"
SERVICE_DIR="/etc/systemd/system"
BINARY_NAME="linknet"
SERVICE_NAME="linknet"
PSK_FILE="/etc/linknet.psk"

echo "=== LinkNet Uninstall Script ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

echo "This will remove LinkNet from your system:"
echo "- Stop and remove systemd service"
echo "- Remove binary from ${INSTALL_DIR}"
echo "- Remove configuration files"
echo "- Remove PSK file"
echo ""

read -p "Continue with uninstall? (y/N): " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Uninstall cancelled"
    exit 0
fi

echo ""
echo "=== Removing Service ==="

# Stop and disable service if it exists
if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
    echo "Stopping ${SERVICE_NAME} service..."
    systemctl stop "${SERVICE_NAME}"
    echo "✓ Service stopped"
fi

if systemctl is-enabled --quiet "${SERVICE_NAME}" 2>/dev/null; then
    echo "Disabling ${SERVICE_NAME} service..."
    systemctl disable "${SERVICE_NAME}"
    echo "✓ Service disabled"
fi

# Remove service file
if [ -f "${SERVICE_DIR}/${SERVICE_NAME}.service" ]; then
    rm -f "${SERVICE_DIR}/${SERVICE_NAME}.service"
    echo "✓ Service file removed"
fi

# Reload systemd
systemctl daemon-reload
echo "✓ Systemd reloaded"

echo ""
echo "=== Removing Binary ==="

# Remove binary
if [ -f "${INSTALL_DIR}/${BINARY_NAME}" ]; then
    rm -f "${INSTALL_DIR}/${BINARY_NAME}"
    echo "✓ Binary removed from ${INSTALL_DIR}"
else
    echo "Binary not found (already removed?)"
fi

echo ""
echo "=== Removing Configuration ==="

# Ask about configuration files
REMOVE_CONFIG=false
if [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf" ] || [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" ]; then
    read -p "Remove configuration files? (y/N): " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        REMOVE_CONFIG=true
    fi
fi

if [ "$REMOVE_CONFIG" = true ]; then
    # Remove config files
    if [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf" ]; then
        rm -f "${CONFIG_DIR}/${SERVICE_NAME}.conf"
        echo "✓ Configuration file removed"
    fi
    
    if [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" ]; then
        rm -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template"
        echo "✓ Configuration template removed"
    fi
    
    # Remove PSK file
    if [ -f "${PSK_FILE}" ]; then
        rm -f "${PSK_FILE}"
        echo "✓ PSK file removed"
    fi
else
    echo "Configuration files preserved"
    echo "PSK file preserved at ${PSK_FILE}"
fi

echo ""
echo "=== Cleanup Network Interfaces ==="

# Clean up any remaining TUN interfaces
TUN_INTERFACES=$(ip link show type tun 2>/dev/null | grep -E "tun[0-9]+" | awk -F: '{print $2}' | tr -d ' ' || true)

if [ ! -z "$TUN_INTERFACES" ]; then
    echo "Found TUN interfaces: $TUN_INTERFACES"
    read -p "Remove TUN interfaces? (y/N): " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        for iface in $TUN_INTERFACES; do
            if ip link show "$iface" > /dev/null 2>&1; then
                ip link delete "$iface" 2>/dev/null || true
                echo "✓ Removed TUN interface: $iface"
            fi
        done
    fi
else
    echo "No TUN interfaces found"
fi

echo ""
echo "=== Uninstall Complete ==="
echo ""
echo "LinkNet has been removed from your system."
echo ""

if [ "$REMOVE_CONFIG" = false ] && ([ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf" ] || [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" ]); then
    echo "Configuration files were preserved:"
    [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf" ] && echo "  - ${CONFIG_DIR}/${SERVICE_NAME}.conf"
    [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" ] && echo "  - ${CONFIG_DIR}/${SERVICE_NAME}.conf.template"
    echo ""
    echo "Remove them manually if desired:"
    [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf" ] && echo "  sudo rm ${CONFIG_DIR}/${SERVICE_NAME}.conf"
    [ -f "${CONFIG_DIR}/${SERVICE_NAME}.conf.template" ] && echo "  sudo rm ${CONFIG_DIR}/${SERVICE_NAME}.conf.template"
    echo ""
fi

echo "Thank you for using LinkNet!"
