#!/bin/bash

# LinkNet Performance Test Script
# Tests LinkNet performance with iperf3

set -e

echo "=== LinkNet Performance Test ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (use sudo)"
    exit 1
fi

# Check if iperf3 is installed
if ! command -v iperf3 &> /dev/null; then
    echo "Installing iperf3..."
    apt-get update && apt-get install -y iperf3
fi

# Check if linknet binary exists
if [ ! -f "./linknet" ]; then
    echo "Building LinkNet..."
    make clean && make
fi

# Test parameters
SERVER_TUN_IP="10.0.1.1"
CLIENT_TUN_IP="10.0.1.2" 
PSK_FILE="/tmp/linknet_test.psk"
PORT=51860
IPERF_PORT=5201

# Generate test PSK
echo "test123456789abcdef0123456789abcdef" > "$PSK_FILE"
chmod 600 "$PSK_FILE"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f linknet || true
    pkill -f iperf3 || true
    sleep 2
    ip link delete tun0 2>/dev/null || true
    ip link delete tun1 2>/dev/null || true
    rm -f "$PSK_FILE"
}

# Trap for cleanup on exit
trap cleanup EXIT

echo ""
echo "=== Starting LinkNet Test ==="

cleanup
sleep 1

# Start server
echo "Starting LinkNet server..."
./linknet --mode server \
          --dev tun0 \
          --local-tun-ip $SERVER_TUN_IP \
          --remote-tun-ip $CLIENT_TUN_IP \
          --psk-file "$PSK_FILE" \
          --port $PORT \
          --log-level info &
SERVER_PID=$!
sleep 3

# Start client
echo "Starting LinkNet client..."
./linknet --mode client \
          --dev tun1 \
          --remote-ip 127.0.0.1 \
          --local-tun-ip $CLIENT_TUN_IP \
          --remote-tun-ip $SERVER_TUN_IP \
          --psk-file "$PSK_FILE" \
          --port $PORT \
          --log-level info &
CLIENT_PID=$!
sleep 5

# Check connectivity
echo "Testing tunnel connectivity..."
if ping -c 3 -W 2 $CLIENT_TUN_IP >/dev/null 2>&1; then
    echo "✓ Tunnel connectivity OK"
else
    echo "✗ Tunnel connectivity FAILED"
    kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
    exit 1
fi

# Start iperf3 server
echo "Starting iperf3 server..."
iperf3 -s -B $CLIENT_TUN_IP -p $IPERF_PORT -D
sleep 1

echo ""
echo "=== Performance Tests ==="

# TCP Performance Test
echo ""
echo "--- TCP Performance (10 seconds) ---"
iperf3 -c $CLIENT_TUN_IP -p $IPERF_PORT -t 10 -f M || true

# UDP Performance Test  
echo ""
echo "--- UDP Performance (5 seconds, 100 Mbps) ---"
iperf3 -c $CLIENT_TUN_IP -p $IPERF_PORT -u -b 100M -t 5 -f M || true

# Parallel TCP Test
echo ""
echo "--- Parallel TCP (4 streams, 5 seconds) ---"
iperf3 -c $CLIENT_TUN_IP -p $IPERF_PORT -P 4 -t 5 -f M || true

echo ""
echo "=== Test Summary ==="
echo "LinkNet performance test completed"
echo "- Server: $SERVER_TUN_IP"
echo "- Client: $CLIENT_TUN_IP"  
echo "- Encryption: Enabled (AES-256-GCM)"
echo ""
echo "Results show multi-threaded LinkNet performance"
echo "Actual network performance will depend on:"
echo "- Network bandwidth and latency"
echo "- Hardware capabilities"
echo "- Encryption overhead"
