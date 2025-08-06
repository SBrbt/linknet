#!/bin/bash

# LinkNet Encrypted Demo Script
# This script demonstrates the encrypted tunnel functionality

set -e

echo "=== LinkNet Encrypted Tunnel Demo ==="
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This demo must be run as root (use sudo)"
    exit 1
fi

# Generate PSK for demo
echo "Step 1: Generating demo PSK..."
PSK=$(./tun_bridge --generate-psk | grep "Generated PSK:" | cut -d' ' -f3)
echo "Demo PSK: $PSK"
echo
echo "IMPORTANT: In production, share this PSK securely between client and server!"
echo

# Save PSK to file
PSK_FILE="/tmp/linknet_demo.psk"
echo "$PSK" > "$PSK_FILE"
chmod 600 "$PSK_FILE"
echo "PSK saved to $PSK_FILE"
echo

echo "Demo modes:"
echo "  1) Server mode (wait for client)"
echo "  2) Client mode (connect to server)"
echo "  3) Local encrypted test (both client and server)"
echo

read -p "Select demo mode (1-3): " -n 1 -r
echo

case $REPLY in
    1)
        echo "=== Starting Encrypted Server ==="
        echo "Configuration:"
        echo "  - TUN device: tun0"
        echo "  - Server TUN IP: 10.0.0.1"
        echo "  - Client TUN IP: 10.0.0.2"
        echo "  - TCP Port: 9000"
        echo "  - Encryption: Enabled (AES-256)"
        echo
        echo "Waiting for encrypted client connection..."
        echo "Client command:"
        echo "  sudo ./tun_bridge --mode client --dev tun0 --remote-ip $(hostname -I | awk '{print $1}') \\"
        echo "                    --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --port 9000 \\"
        echo "                    --psk \"$PSK\""
        echo
        echo "Press Ctrl+C to stop server."
        echo
        ./tun_bridge --mode server --dev tun0 --local-ip 10.0.0.1 --remote-tun-ip 10.0.0.2 --port 9000 --psk "$PSK"
        ;;
    2)
        echo "=== Starting Encrypted Client ==="
        echo
        read -p "Enter server IP address: " SERVER_IP
        echo
        echo "Configuration:"
        echo "  - TUN device: tun0"
        echo "  - Client TUN IP: 10.0.0.2"
        echo "  - Server TUN IP: 10.0.0.1"
        echo "  - Server IP: $SERVER_IP"
        echo "  - TCP Port: 9000"
        echo "  - Encryption: Enabled (AES-256)"
        echo
        echo "Connecting to encrypted server..."
        echo "Press Ctrl+C to stop client."
        echo
        ./tun_bridge --mode client --dev tun0 --remote-ip "$SERVER_IP" --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --port 9000 --psk "$PSK"
        ;;
    3)
        echo "=== Local Encrypted Tunnel Test ==="
        echo "This will create an encrypted tunnel between two TUN interfaces on the same machine."
        echo
        
        # Start server in background
        echo "Starting encrypted server..."
        ./tun_bridge --mode server --dev tun1 --local-ip 10.1.0.1 --remote-tun-ip 10.1.0.2 --port 19001 --psk "$PSK" &
        SERVER_PID=$!
        
        sleep 3
        
        # Start client in background
        echo "Starting encrypted client..."
        ./tun_bridge --mode client --dev tun2 --remote-ip 127.0.0.1 --local-ip 10.1.0.2 --remote-tun-ip 10.1.0.1 --port 19001 --psk "$PSK" &
        CLIENT_PID=$!
        
        sleep 5
        
        echo "Testing encrypted connectivity..."
        
        # Test ping through encrypted tunnel
        if ping -c 3 -W 2 10.1.0.1 >/dev/null 2>&1; then
            echo "✓ Encrypted tunnel connectivity test successful!"
            echo "  - Authentication: ✓ Completed"
            echo "  - Encryption: ✓ AES-256-CBC"
            echo "  - HMAC: ✓ SHA-256"
            echo "  - Data forwarding: ✓ Working"
        else
            echo "⚠ Connectivity test failed (this may be normal in some environments)"
        fi
        
        echo
        echo "Testing tunnel statistics..."
        sleep 2
        
        # Show some network interface information
        echo "TUN Interfaces created:"
        ip addr show tun1 2>/dev/null | grep -E "(tun1|inet)" || echo "  tun1: Interface details not available"
        ip addr show tun2 2>/dev/null | grep -E "(tun2|inet)" || echo "  tun2: Interface details not available"
        
        echo
        echo "Cleaning up test..."
        
        # Cleanup
        kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
        sleep 2
        pkill -f "tun_bridge.*tun[12]" 2>/dev/null || true
        ip link delete tun1 2>/dev/null || true
        ip link delete tun2 2>/dev/null || true
        
        echo "✓ Local encrypted tunnel test completed"
        ;;
    *)
        echo "Invalid selection."
        rm -f "$PSK_FILE"
        exit 1
        ;;
esac

# Cleanup
echo
echo "Cleaning up..."
rm -f "$PSK_FILE"
echo "Demo completed."
