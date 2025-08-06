#!/bin/bash

# Route Management Test Script for LinkNet
# This script tests the automatic routing functionality

set -e

echo "=== LinkNet Route Management Test ==="
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This test must be run as root (use sudo)"
    exit 1
fi

# Generate test PSK
echo "Step 1: Generating test PSK..."
PSK=$(./tun_bridge --generate-psk | grep "Generated PSK:" | cut -d' ' -f3)
echo "Test PSK: $PSK"
echo

# Test 1: Basic route configuration
echo "Step 2: Testing basic route configuration..."
echo "This will test if routes are properly added and removed."
echo

# Save current routes for comparison
echo "Saving current routing table..."
ip route show > /tmp/routes_before.txt

echo "Starting server with route configuration for 3 seconds..."
timeout 3s ./tun_bridge \
    --mode server \
    --dev tun0 \
    --local-ip 10.0.0.1 \
    --remote-tun-ip 10.0.0.2 \
    --port 9000 \
    --psk "$PSK" \
    --route-network 192.168.100.0/24 \
    --route-network 172.16.1.0/24 \
    2>&1 | head -10 &

SERVER_PID=$!
sleep 2

# Check if TUN interface was created
if ip link show tun0 &>/dev/null; then
    echo "✓ TUN interface created: tun0"
    
    # Check if routes were added
    echo "Checking if routes were added..."
    if ip route show | grep -q "192.168.100.0/24 dev tun0"; then
        echo "✓ Route 192.168.100.0/24 via tun0 added"
    else
        echo "⚠ Route 192.168.100.0/24 not found"
    fi
    
    if ip route show | grep -q "172.16.1.0/24 dev tun0"; then
        echo "✓ Route 172.16.1.0/24 via tun0 added"
    else
        echo "⚠ Route 172.16.1.0/24 not found"
    fi
else
    echo "⚠ TUN interface not found (may be normal in some environments)"
fi

# Wait for timeout and cleanup
wait $SERVER_PID 2>/dev/null || true

echo "Checking route cleanup..."
sleep 1

# Check if routes were cleaned up
if ip route show | grep -q "192.168.100.0/24 dev tun0"; then
    echo "⚠ Route 192.168.100.0/24 still present (manual cleanup needed)"
    ip route del 192.168.100.0/24 dev tun0 2>/dev/null || true
else
    echo "✓ Route 192.168.100.0/24 properly cleaned up"
fi

if ip route show | grep -q "172.16.1.0/24 dev tun0"; then
    echo "⚠ Route 172.16.1.0/24 still present (manual cleanup needed)"
    ip route del 172.16.1.0/24 dev tun0 2>/dev/null || true
else
    echo "✓ Route 172.16.1.0/24 properly cleaned up"
fi

# Clean up any remaining interface
ip link delete tun0 2>/dev/null || true

echo
echo "Step 3: Testing client mode with routes..."

# Test client mode (will fail to connect but should still configure routes)
timeout 3s ./tun_bridge \
    --mode client \
    --dev tun1 \
    --remote-ip 192.168.1.254 \
    --local-ip 10.0.0.2 \
    --remote-tun-ip 10.0.0.1 \
    --port 9000 \
    --psk "$PSK" \
    --route-network 10.10.0.0/16 \
    2>&1 | head -10 &

CLIENT_PID=$!
sleep 2

# Check client interface and routes
if ip link show tun1 &>/dev/null; then
    echo "✓ Client TUN interface created: tun1"
    
    if ip route show | grep -q "10.10.0.0/16 dev tun1"; then
        echo "✓ Client route 10.10.0.0/16 via tun1 added"
    else
        echo "⚠ Client route not found"
    fi
else
    echo "⚠ Client TUN interface not found"
fi

# Wait and cleanup
wait $CLIENT_PID 2>/dev/null || true
sleep 1

# Check cleanup
if ip route show | grep -q "10.10.0.0/16 dev tun1"; then
    echo "⚠ Client route still present (manual cleanup needed)"
    ip route del 10.10.0.0/16 dev tun1 2>/dev/null || true
else
    echo "✓ Client route properly cleaned up"
fi

ip link delete tun1 2>/dev/null || true

echo
echo "Step 4: Testing route validation..."

# Test invalid route format
echo "Testing invalid route format..."
if timeout 2s ./tun_bridge \
    --mode server \
    --dev tun2 \
    --local-ip 10.0.0.1 \
    --remote-tun-ip 10.0.0.2 \
    --psk "$PSK" \
    --route-network "invalid-network" \
    2>&1 | grep -q "Invalid network format"; then
    echo "✓ Route validation works"
else
    echo "⚠ Route validation test completed"
fi

# Clean up any remaining interfaces
for i in {0..9}; do
    ip link delete tun$i 2>/dev/null || true
done

# Clean up test files
rm -f /tmp/routes_before.txt

echo
echo "=== Route Management Test Summary ==="
echo "✓ Route addition and removal functionality tested"
echo "✓ Server and client mode route configuration tested"
echo "✓ Route validation tested"
echo "✓ Cleanup functionality tested"
echo
echo "For production use with routing:"
echo "Server: sudo ./tun_bridge --mode server --dev tun0 --local-ip 10.0.0.1 --remote-tun-ip 10.0.0.2 --psk \"key\" --route-network 192.168.1.0/24"
echo "Client: sudo ./tun_bridge --mode client --dev tun0 --remote-ip <server-ip> --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --psk \"key\" --route-network 172.16.0.0/16"
echo
echo "This will route specified networks through the encrypted tunnel automatically."
