#!/bin/bash

# LinkNet Connection Test Script
# This script helps test the tunnel connection after it's established

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    case $1 in
        "success") echo -e "${GREEN}✓ $2${NC}" ;;
        "error") echo -e "${RED}✗ $2${NC}" ;;
        "warning") echo -e "${YELLOW}⚠ $2${NC}" ;;
        "info") echo -e "${BLUE}ℹ $2${NC}" ;;
    esac
}

echo "=== LinkNet Connection Test Suite ==="
echo

# Check if we're testing client or server side
read -p "Are you testing from the client side? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    REMOTE_IP="10.0.0.1"
    LOCAL_IP="10.0.0.2"
    SIDE="client"
else
    REMOTE_IP="10.0.0.2"
    LOCAL_IP="10.0.0.1"
    SIDE="server"
fi

echo "Testing from $SIDE side: $LOCAL_IP -> $REMOTE_IP"
echo

# Test 1: Check TUN interface exists
echo "Test 1: TUN Interface Check"
if ip link show tun0 &>/dev/null; then
    print_status "success" "TUN interface tun0 exists"
    
    # Check if it has the right IP
    if ip addr show tun0 | grep -q "$LOCAL_IP"; then
        print_status "success" "TUN interface has correct IP: $LOCAL_IP"
    else
        print_status "error" "TUN interface IP mismatch"
        ip addr show tun0
    fi
else
    print_status "error" "TUN interface tun0 not found"
    echo "Make sure tun_bridge is running"
    exit 1
fi
echo

# Test 2: Check tun_bridge process
echo "Test 2: Process Check"
if pgrep -f "tun_bridge.*tun0" &>/dev/null; then
    print_status "success" "tun_bridge process is running"
    print_status "info" "Process: $(pgrep -f tun_bridge | head -1)"
else
    print_status "error" "tun_bridge process not found"
    exit 1
fi
echo

# Test 3: Basic connectivity
echo "Test 3: Basic Connectivity"
print_status "info" "Testing ping to $REMOTE_IP..."

if ping -c 3 -W 5 "$REMOTE_IP" &>/dev/null; then
    print_status "success" "Ping successful"
    
    # Get ping statistics
    PING_RESULT=$(ping -c 5 -W 3 "$REMOTE_IP" 2>/dev/null | tail -n 2)
    print_status "info" "Ping statistics:"
    echo "$PING_RESULT"
else
    print_status "error" "Ping failed"
    print_status "warning" "Trying with timeout..."
    timeout 10s ping -c 3 "$REMOTE_IP" || print_status "error" "Ping completely failed"
fi
echo

# Test 4: Route verification
echo "Test 4: Route Verification"
if ip route get "$REMOTE_IP" | grep -q "dev tun0"; then
    print_status "success" "Route through tun0 exists"
    print_status "info" "Route: $(ip route get $REMOTE_IP)"
else
    print_status "warning" "Route not explicitly through tun0"
    print_status "info" "Current route: $(ip route get $REMOTE_IP)"
fi
echo

# Test 5: Port connectivity (if we can determine the remote TCP port)
echo "Test 5: Advanced Connectivity Tests"

# Test traceroute
print_status "info" "Testing traceroute..."
if command -v traceroute &>/dev/null; then
    TRACE_RESULT=$(timeout 10s traceroute -n "$REMOTE_IP" 2>/dev/null | head -n 3)
    if echo "$TRACE_RESULT" | grep -q "$REMOTE_IP"; then
        print_status "success" "Traceroute shows direct path"
    else
        print_status "warning" "Traceroute result unclear"
    fi
    echo "$TRACE_RESULT"
else
    print_status "warning" "traceroute not available"
fi
echo

# Test 6: Large packet test
echo "Test 6: Large Packet Test"
print_status "info" "Testing with large packets (1400 bytes)..."
if ping -c 3 -s 1400 -W 5 "$REMOTE_IP" &>/dev/null; then
    print_status "success" "Large packet test passed"
else
    print_status "warning" "Large packet test failed (MTU issue?)"
    print_status "info" "Trying smaller packets (1200 bytes)..."
    if ping -c 3 -s 1200 -W 5 "$REMOTE_IP" &>/dev/null; then
        print_status "warning" "1200 byte packets work - consider lowering MTU"
    fi
fi
echo

# Test 7: Bandwidth test (if iperf3 is available)
echo "Test 7: Performance Test"
if command -v iperf3 &>/dev/null; then
    print_status "info" "iperf3 available - you can run bandwidth tests"
    echo "To test bandwidth:"
    if [ "$SIDE" = "server" ]; then
        echo "  Run: iperf3 -s -B $LOCAL_IP"
        echo "  Then on client: iperf3 -c $LOCAL_IP -t 10"
    else
        echo "  On server run: iperf3 -s -B $REMOTE_IP"
        echo "  Then run: iperf3 -c $REMOTE_IP -t 10"
    fi
else
    print_status "warning" "iperf3 not available for bandwidth testing"
    print_status "info" "Install with: sudo apt install iperf3"
fi
echo

# Test 8: Service connectivity test
echo "Test 8: Service Connectivity Test"
print_status "info" "Testing if we can connect to common services..."

# Test SSH port
if timeout 3s nc -z "$REMOTE_IP" 22 2>/dev/null; then
    print_status "success" "SSH service (port 22) is reachable"
else
    print_status "info" "SSH service not running or not accessible"
fi

# Test HTTP port
if timeout 3s nc -z "$REMOTE_IP" 80 2>/dev/null; then
    print_status "success" "HTTP service (port 80) is reachable"
else
    print_status "info" "HTTP service not running on port 80"
fi

# Test HTTPS port
if timeout 3s nc -z "$REMOTE_IP" 443 2>/dev/null; then
    print_status "success" "HTTPS service (port 443) is reachable"
else
    print_status "info" "HTTPS service not running on port 443"
fi
echo

# Test 9: Continuous connectivity test
echo "Test 9: Stability Test"
read -p "Run continuous ping test for 30 seconds? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "info" "Running 30-second continuous ping test..."
    
    # Run ping for 30 seconds with 0.5 second intervals
    PING_OUTPUT=$(timeout 30s ping -i 0.5 "$REMOTE_IP" 2>/dev/null | tail -n 3)
    
    if echo "$PING_OUTPUT" | grep -q "0% packet loss"; then
        print_status "success" "Stability test passed - no packet loss"
    else
        print_status "warning" "Some packet loss detected"
    fi
    
    echo "$PING_OUTPUT"
else
    print_status "info" "Stability test skipped"
fi
echo

# Test 10: Interface statistics
echo "Test 10: Interface Statistics"
print_status "info" "TUN interface statistics:"

# Get interface statistics
if [ -f "/proc/net/dev" ]; then
    TUN_STATS=$(cat /proc/net/dev | grep tun0)
    if [ -n "$TUN_STATS" ]; then
        # Parse RX and TX bytes
        RX_BYTES=$(echo $TUN_STATS | awk '{print $2}')
        TX_BYTES=$(echo $TUN_STATS | awk '{print $10}')
        RX_PACKETS=$(echo $TUN_STATS | awk '{print $3}')
        TX_PACKETS=$(echo $TUN_STATS | awk '{print $11}')
        
        print_status "info" "RX: $RX_PACKETS packets, $RX_BYTES bytes"
        print_status "info" "TX: $TX_PACKETS packets, $TX_BYTES bytes"
        
        if [ "$RX_BYTES" -gt 0 ] && [ "$TX_BYTES" -gt 0 ]; then
            print_status "success" "Bidirectional traffic confirmed"
        else
            print_status "warning" "Limited or no traffic detected"
        fi
    fi
fi
echo

# Summary
echo "=== Test Summary ==="
echo "Connection testing completed from $SIDE side ($LOCAL_IP)"
echo
echo "📋 Quick connectivity commands:"
echo "  ping $REMOTE_IP                    # Basic connectivity"
echo "  traceroute $REMOTE_IP              # Route verification"  
echo "  nc -zv $REMOTE_IP 22               # Test SSH access"
echo "  iperf3 -c $REMOTE_IP -t 10         # Bandwidth test"
echo "  watch -n 1 'cat /proc/net/dev | grep tun0'  # Monitor traffic"
echo
echo "🔧 Troubleshooting commands:"
echo "  ip addr show tun0                  # Check interface"
echo "  ip route show dev tun0             # Check routes"
echo "  sudo tcpdump -i tun0               # Monitor packets"
echo "  pgrep -f tun_bridge                # Check process"
echo
print_status "info" "For detailed testing guide, see TESTING.md"
