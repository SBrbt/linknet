#!/bin/bash

# TUN Interface Setup Script for LinkNet
# Usage: ./setup_tun.sh <device> <local_ip> <remote_ip> [netmask]

set -e

DEVICE=${1:-tun0}
LOCAL_IP=${2}
REMOTE_IP=${3}
NETMASK=${4:-24}

if [ -z "$LOCAL_IP" ] || [ -z "$REMOTE_IP" ]; then
    echo "Usage: $0 <device> <local_ip> <remote_ip> [netmask]"
    echo "Example: $0 tun0 10.0.0.1 10.0.0.2 24"
    exit 1
fi

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)"
    exit 1
fi

echo "Setting up TUN interface: $DEVICE"
echo "Local IP: $LOCAL_IP/$NETMASK"
echo "Remote IP: $REMOTE_IP"

# Load TUN module if not already loaded
if ! lsmod | grep -q "^tun "; then
    echo "Loading TUN kernel module..."
    modprobe tun
fi

# Check if device already exists and remove it
if ip link show "$DEVICE" &>/dev/null; then
    echo "Device $DEVICE already exists, removing..."
    ip link delete "$DEVICE" 2>/dev/null || true
fi

# The TUN device will be created by the application
# Just ensure we have the proper routing setup

# Enable IP forwarding
echo "Enabling IP forwarding..."
echo 1 > /proc/sys/net/ipv4/ip_forward

# Set up iptables rules for NAT traversal (optional)
if command -v iptables &>/dev/null; then
    echo "Setting up iptables rules..."
    
    # Allow forwarding for the TUN interface
    iptables -A FORWARD -i "$DEVICE" -j ACCEPT 2>/dev/null || true
    iptables -A FORWARD -o "$DEVICE" -j ACCEPT 2>/dev/null || true
    
    # Optional: Set up NAT for internet access through TUN
    # iptables -t nat -A POSTROUTING -s ${LOCAL_IP}/32 -j MASQUERADE
fi

echo "TUN setup completed. The application will create and configure the interface."
echo ""
echo "Next steps:"
echo "1. Start the server: sudo ./tun_bridge --mode server --dev $DEVICE --local-ip $LOCAL_IP --remote-tun-ip $REMOTE_IP --port 9000"
echo "2. Start the client: sudo ./tun_bridge --mode client --dev $DEVICE --remote-ip <server-ip> --local-ip $REMOTE_IP --remote-tun-ip $LOCAL_IP --port 9000"
