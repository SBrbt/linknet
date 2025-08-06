# LinkNet - High-Performance Multi-threaded TUN Bridge

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![OpenSSL](https://img.shields.io/badge/OpenSSL-AES--256--GCM-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

A high-performance, secure C++ network bridge that creates encrypted point-to-point tunnels between two machines using TUN interfaces. Designed for simplicity, security, and performance.

## ✨ Key Features

- **🔗 Point-to-Point Tunneling**: Secure connection between exactly two endpoints
- **🛡️ NAT Traversal**: Works through NAT firewalls and restrictive networks  
- **🔐 Strong Encryption**: AES-256-GCM with HMAC-SHA256 authentication
- **⚡ Multi-threaded Performance**: Optimized async packet processing (100+ Gbps capability)
- **🔄 Reliable Operation**: Auto-reconnection and health monitoring
- **🎯 Simple Deployment**: Interactive installation with systemd integration

## 🏗️ Architecture

```
   Machine A (Server)                    Machine B (Client)
   ┌─────────────────┐                  ┌─────────────────┐
   │   Applications  │                  │   Applications  │
   └─────────────────┘                  └─────────────────┘
           │                                    │
   ┌─────────────────┐    TCP :51860    ┌─────────────────┐
   │ LinkNet Server  │◄────────────────►│ LinkNet Client  │
   │   TUN: 10.0.1.1 │   (Encrypted)    │   TUN: 10.0.1.2 │
   └─────────────────┘                  └─────────────────┘
   
   Multi-threaded Design:
   • TUN Reader Thread    • Socket Reader Thread
   • Packet Processor     • Heartbeat Monitor
```

## 🚀 Quick Start

### 1. Build
```bash
git clone https://github.com/SBrbt/linknet.git
cd linknet
make deps  # Check dependencies
make       # Build project
```

### 2. Interactive Installation
```bash
sudo ./scripts/install.sh
# The interactive installer will guide you through:
# - Server or client configuration
# - Network settings (IPs, ports)
# - Automatic systemd service setup
# - Secure PSK file generation
```

### 3. Manual Usage (Optional)
```bash
# Generate a secure PSK file
openssl rand -hex 32 | sudo tee /etc/linknet.psk
sudo chmod 600 /etc/linknet.psk

# Server mode
sudo ./linknet --mode server \
    --local-tun-ip 10.0.1.1 \
    --remote-tun-ip 10.0.1.2 \
    --psk-file /etc/linknet.psk \
    --port 51860

# Client mode  
sudo ./linknet --mode client \
    --remote-ip <server-public-ip> \
    --local-tun-ip 10.0.1.2 \
    --remote-tun-ip 10.0.1.1 \
    --psk-file /etc/linknet.psk \
    --port 51860
```

### 4. Test Connection
```bash
# From server: ping client
ping 10.0.1.2

# From client: ping server  
ping 10.0.1.1
```

### 5. Performance Testing
```bash
sudo ./test_performance.sh  # Built-in iperf3 performance test
```

## ⚙️ Configuration Options

### Required Parameters
```bash
--mode client|server     # Operation mode
--local-tun-ip IP        # Local tunnel endpoint IP
--remote-tun-ip IP       # Remote tunnel endpoint IP  
--psk-file FILE          # Pre-shared key file (recommended)
```

### Optional Parameters
```bash
--remote-ip IP           # Server IP (required for client)
--port PORT              # TCP port (default: 51860)
--dev DEVICE             # TUN device name (default: tun0)
--psk KEY                # PSK string (less secure than file)
--no-encryption          # Disable encryption (testing only)
--log-level LEVEL        # debug|info|warning|error (default: info)
--performance-mode       # Enable optimizations
```

## 🔧 Service Management

After installation with the interactive script:

```bash
# Service control
sudo systemctl start linknet
sudo systemctl stop linknet
sudo systemctl restart linknet
sudo systemctl status linknet

# Enable auto-start
sudo systemctl enable linknet

# View logs
sudo journalctl -u linknet -f
```

## 🔐 Security

### Encryption
- **Algorithm**: AES-256-GCM with HMAC-SHA256
- **Key Management**: Secure PSK-based authentication
- **File Security**: PSK files created with 600 permissions

### PSK Management
```bash
# Generate new PSK
openssl rand -hex 32 | sudo tee /etc/linknet.psk
sudo chmod 600 /etc/linknet.psk

# Share PSK securely between nodes
sudo scp /etc/linknet.psk user@remote-node:/etc/linknet.psk
```

## ⚡ Performance

### Benchmarks
- **Local Loopback**: >100 Gbps throughput
- **Network Limited**: Actual performance depends on network bandwidth/latency
- **Low Latency**: Optimized multi-threaded packet processing

### Performance Testing
```bash
# Built-in test with iperf3
sudo ./test_performance.sh

# Manual testing
# Terminal 1 (server):
iperf3 -s -B 10.0.1.1

# Terminal 2 (client):
iperf3 -c 10.0.1.1 -B 10.0.1.2
```

## 🛠️ Development

### Build System
```bash
make              # Build release version
make clean        # Clean build artifacts
make deps         # Check dependencies
```

### Code Structure
```
src/
├── main.cpp              # Entry point and configuration
├── bridge.h/cpp          # Multi-threaded packet bridge
├── tun_manager.h/cpp     # TUN interface management
├── socket_manager.h/cpp  # TCP socket handling
├── crypto_manager.h/cpp  # Encryption and authentication
├── route_manager.h/cpp   # Network route management
├── command_executor.h/cpp # Async command execution
└── utils.h               # Logging and utilities

scripts/
├── install.sh            # Interactive installation
└── uninstall.sh          # Complete removal
```

## 🐛 Troubleshooting

### Common Issues

**Permission Denied**
- Solution: Run with `sudo` (required for TUN interface creation)

**Port Already in Use**
- Check: `sudo netstat -tlnp | grep :51860`
- Solution: Kill conflicting process or use different port

**Authentication Timeout**
- Check: PSK files match between client and server
- Verify: Network connectivity on specified port

**No Route to Host**
- Check: Firewall settings allow the specified port
- Verify: Server is reachable from client

### Debug Commands
```bash
# Service status
sudo systemctl status linknet

# Detailed logs
sudo journalctl -u linknet -f --no-pager

# Network connectivity
ping 10.0.1.1  # Client to server TUN IP
ping 10.0.1.2  # Server to client TUN IP

# Interface status
ip addr show tun0
ip route show dev tun0
```

### Log Levels
- **debug**: Detailed packet information
- **info**: General operational info (default)
- **warning**: Non-fatal issues
- **error**: Error conditions

## 📦 Uninstallation

```bash
sudo ./scripts/uninstall.sh
# This will:
# - Stop and disable the service
# - Remove binary from /usr/local/bin
# - Optionally remove configuration files
# - Optionally remove PSK files
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🆘 Support

- **Issues**: [GitHub Issues](https://github.com/SBrbt/linknet/issues)
- **Documentation**: This README and inline code comments
- **Debugging**: Use `sudo journalctl -u linknet -f` for real-time logs

---

*Built with ❤️ for secure, high-performance networking*
