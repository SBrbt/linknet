#!/bin/bash

# LinkNet Demo Script
# This script demonstrates how to set up a point-to-point TUN bridge

set -e

echo "=== LinkNet TUN Bridge Demo ==="
echo
echo "This demo will show you how to use LinkNet for creating a virtual network bridge."
echo "For a real test, you need two separate machines."
echo

# Configuration
DEV_NAME="tun0"
SERVER_IP="10.0.0.1"
CLIENT_IP="10.0.0.2"
PORT="9000"

echo "Demo Configuration:"
echo "  TUN Device: $DEV_NAME"
echo "  Server TUN IP: $SERVER_IP"
echo "  Client TUN IP: $CLIENT_IP"
echo "  TCP Port: $PORT"
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This demo must be run as root (use sudo)"
    exit 1
fi

echo "Step 1: Setting up environment..."
# Load TUN module
modprobe tun 2>/dev/null || true

# Enable IP forwarding
echo 1 > /proc/sys/net/ipv4/ip_forward

echo "Step 2: Demo modes available:"
echo "  1) Server mode demo"
echo "  2) Client mode demo"
echo "  3) Local loopback test"
echo

read -p "Select demo mode (1-3): " -n 1 -r
echo

case $REPLY in
    1)
        echo "Starting server mode demo..."
        echo "Command: ./tun_bridge --mode server --dev $DEV_NAME --local-ip $SERVER_IP --remote-tun-ip $CLIENT_IP --port $PORT"
        echo
        echo "The server will wait for a client connection."
        echo "On another machine, run:"
        echo "  sudo ./tun_bridge --mode client --dev $DEV_NAME --remote-ip $(hostname -I | awk '{print $1}') --local-ip $CLIENT_IP --remote-tun-ip $SERVER_IP --port $PORT"
        echo
        echo "Press Ctrl+C to stop the server."
        echo
        ./tun_bridge --mode server --dev $DEV_NAME --local-ip $SERVER_IP --remote-tun-ip $CLIENT_IP --port $PORT
        ;;
    2)
        echo "Client mode demo..."
        echo
        read -p "Enter server IP address: " SERVER_ADDR
        echo
        echo "Starting client mode..."
        echo "Command: ./tun_bridge --mode client --dev $DEV_NAME --remote-ip $SERVER_ADDR --local-ip $CLIENT_IP --remote-tun-ip $SERVER_IP --port $PORT"
        echo
        echo "Press Ctrl+C to stop the client."
        echo
        ./tun_bridge --mode client --dev $DEV_NAME --remote-ip "$SERVER_ADDR" --local-ip $CLIENT_IP --remote-tun-ip $SERVER_IP --port $PORT
        ;;
    3)
        echo "Local loopback test..."
        echo "This will create a TUN interface and test it locally."
        echo
        
        # Start server in background
        echo "Starting server in background..."
        ./tun_bridge --mode server --dev tun1 --local-ip 10.1.0.1 --remote-tun-ip 10.1.0.2 --port 19001 &
        SERVER_PID=$!
        
        sleep 3
        
        # Start client in background
        echo "Starting client..."
        ./tun_bridge --mode client --dev tun2 --remote-ip 127.0.0.1 --local-ip 10.1.0.2 --remote-tun-ip 10.1.0.1 --port 19001 &
        CLIENT_PID=$!
        
        sleep 3
        
        echo "Testing connectivity..."
        
        # Test ping from client to server
        if ping -c 3 -W 2 10.1.0.1 >/dev/null 2>&1; then
            echo "✓ Connectivity test successful!"
        else
            echo "✗ Connectivity test failed"
        fi
        
        # Cleanup
        echo "Cleaning up..."
        kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
        ip link delete tun1 2>/dev/null || true
        ip link delete tun2 2>/dev/null || true
        
        echo "Local test completed."
        ;;
    *)
        echo "Invalid selection."
        exit 1
        ;;
esac
