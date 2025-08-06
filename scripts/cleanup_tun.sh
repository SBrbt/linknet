#!/bin/bash

# TUN Interface Cleanup Script for LinkNet
# Usage: ./cleanup_tun.sh [device]

set -e

DEVICE=${1:-tun0}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)"
    exit 1
fi

echo "Cleaning up TUN interface: $DEVICE"

# Remove TUN device if it exists
if ip link show "$DEVICE" &>/dev/null; then
    echo "Removing TUN device: $DEVICE"
    ip link delete "$DEVICE" 2>/dev/null || true
else
    echo "TUN device $DEVICE not found"
fi

# Clean up routing rules related to the device
echo "Cleaning up routing rules..."
ip route flush dev "$DEVICE" 2>/dev/null || true

# Clean up iptables rules (optional)
if command -v iptables &>/dev/null; then
    echo "Cleaning up iptables rules..."
    
    # Remove forwarding rules
    iptables -D FORWARD -i "$DEVICE" -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -o "$DEVICE" -j ACCEPT 2>/dev/null || true
    
    # Remove NAT rules if they exist
    # iptables -t nat -D POSTROUTING -s 10.0.0.0/24 -j MASQUERADE 2>/dev/null || true
fi

# Kill any running tun_bridge processes
echo "Stopping any running tun_bridge processes..."
pkill -f "tun_bridge.*$DEVICE" 2>/dev/null || true

echo "Cleanup completed for $DEVICE"
