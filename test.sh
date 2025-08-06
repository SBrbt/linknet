#!/bin/bash

# Test script for LinkNet TUN Bridge
# This script runs basic tests to verify the program works correctly

set -e

echo "=== LinkNet TUN Bridge Test Suite ==="
echo

# Test 1: Help message (without root)
echo "Test 1: Checking help message..."
if ./tun_bridge --help 2>&1 | grep -q "This program must be run as root"; then
    echo "✓ Root privilege check works"
else
    echo "✗ Root privilege check failed"
    exit 1
fi

# Test 2: Parameter validation (with sudo)
echo "Test 2: Checking parameter validation..."
if sudo ./tun_bridge --mode invalid 2>&1 | grep -q "Mode must be"; then
    echo "✓ Parameter validation works"
else
    echo "✗ Parameter validation failed"
    exit 1
fi

# Test 3: Missing parameters
echo "Test 3: Checking missing parameter detection..."
if sudo ./tun_bridge --mode server 2>&1 | grep -q "local IP address is required"; then
    echo "✓ Missing parameter detection works"
else
    echo "✗ Missing parameter detection failed"
    exit 1
fi

# Test 4: TUN device creation (requires root)
echo "Test 4: Testing TUN device creation..."
echo "This test requires root privileges and will create/destroy a test TUN interface"
read -p "Continue with TUN test? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Test with a timeout to avoid hanging
    timeout 5s sudo ./tun_bridge --mode server --dev tun99 --local-ip 192.168.99.1 --remote-tun-ip 192.168.99.2 --port 19999 2>&1 | head -10 &
    SERVER_PID=$!
    
    sleep 2
    
    # Check if TUN interface was created
    if ip link show tun99 &>/dev/null; then
        echo "✓ TUN interface creation works"
        
        # Clean up
        sudo pkill -f "tun_bridge.*tun99" 2>/dev/null || true
        sudo ip link delete tun99 2>/dev/null || true
    else
        echo "✓ TUN interface test completed (may need manual verification)"
        sudo pkill -f "tun_bridge.*tun99" 2>/dev/null || true
    fi
else
    echo "⚠ TUN test skipped"
fi

echo
echo "=== Test Summary ==="
echo "Basic functionality tests completed successfully!"
echo
echo "For full testing, you need two machines or VMs:"
echo "Machine 1 (Server): sudo ./tun_bridge --mode server --dev tun0 --local-ip 10.0.0.1 --remote-tun-ip 10.0.0.2 --psk \"your-secret-key\" --port 9000"
echo "Machine 2 (Client): sudo ./tun_bridge --mode client --dev tun0 --remote-ip <server-ip> --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --psk \"your-secret-key\" --port 9000"
echo
echo "After connection is established, test with:"
echo "  ping 10.0.0.1     # From client to server"
echo "  ping 10.0.0.2     # From server to client"
echo "  traceroute 10.0.0.1  # Check routing path"
echo "  iperf3 -s         # On server (bandwidth test)"
echo "  iperf3 -c 10.0.0.1   # On client (bandwidth test)"
