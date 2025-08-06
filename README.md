# LinkNet - TUN Network Bridge with NAT Traversal

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![OpenSSL](https://img.shields.io/badge/OpenSSL-AES--256--GCM-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

A robust C++ implementation of a point-to-point virtual network bridge that enables secure communication through NAT firewalls using TUN interfaces and TCP sockets.

## 🌟 Features

- **🔗 Network Bridging**: Seamless TUN interface to TCP socket bridging
- **🛡️ NAT Traversal**: Bypass NAT firewalls and connect private networks  
- **🔐 Strong Encryption**: AES-256-GCM encryption with HMAC-SHA256 authentication
- **⚡ High Performance**: Multi-threaded packet processing for optimal throughput
- **🔄 Auto-Recovery**: Connection keepalive with automatic reconnection
- **📡 Dual Mode**: Both client and server operation modes
- **🛣️ Smart Routing**: Automatic routing table management
- **🔧 Service Integration**: Complete systemd service support
- **📊 Monitoring**: Comprehensive logging and status reporting

## 🏗️ Architecture

```
┌─────────────────┐       ┌──────────────┐      ┌─────────────────┐
│   Application   │       │    LinkNet   │      │   Application   │
│   (Client)      │◄─────►│    Bridge    │◄────►│      (Server)   │
└─────────────────┘       └──────────────┘      └─────────────────┘
         │                       │                       │
    ┌────▼────┐             ┌────▼────┐             ┌────▼────┐
    │  TUN0   │             │   TCP   │             │  TUN0   │
    │10.0.1.2 │◄───────────►│  :8080  │◄───────────►│10.0.1.1 │
    └─────────┘             └─────────┘             └─────────┘
         │                       │                       │
    ┌────▼────┐             ┌────▼────┐             ┌────▼────┐
    │ Private │             │   NAT   │             │ Private │
    │Network A│             │Internet │             │Network B│
    └─────────┘             └─────────┘             └─────────┘
```

## 🚀 Quick Start

### Prerequisites Check
```bash
# Check system dependencies
make deps
```

### Building
```bash
# Clone and build
git clone <repository-url>
cd linknet
make

# Verify build
./tun_bridge --help
```

### Basic Usage

#### 1. Server Setup (Public IP: 203.0.113.10)
```bash
# Generate shared encryption key
./tun_bridge --generate-psk > bridge.psk

# Start server
sudo ./tun_bridge --server \
    --port 8080 \
    --ip 10.0.1.1 \
    --remote-ip 10.0.1.2 \
    --psk-file bridge.psk \
    --routes "192.168.1.0/24,10.0.0.0/8"
```

#### 2. Client Setup (Behind NAT)
```bash
# Copy the same bridge.psk file to client
scp bridge.psk user@client-host:

# Start client
sudo ./tun_bridge --client \
    --host 203.0.113.10 \
    --port 8080 \
    --ip 10.0.1.2 \
    --remote-ip 10.0.1.1 \
    --psk-file bridge.psk \
    --routes "192.168.2.0/24"
```

#### 3. Test Connection
```bash
# From server: ping client tunnel IP
ping 10.0.1.2

# From client: ping server tunnel IP  
ping 10.0.1.1

# Test routing through tunnel
ping 192.168.2.100  # From server to client's network
ping 192.168.1.100  # From client to server's network
```

## 🛠️ Advanced Configuration

### Command Line Options
```bash
./tun_bridge [OPTIONS]

Core Options:
  --server                 Run in server mode
  --client                 Run in client mode
  --host <IP>             Server IP address (client mode)
  --port <PORT>           TCP port (default: 8080)
  --ip <IP>               Local tunnel IP address
  --remote-ip <IP>        Remote tunnel IP address

Security Options:
  --psk-file <FILE>       Pre-shared key file for encryption
  --generate-psk          Generate and output a new PSK

Routing Options:
  --routes <ROUTES>       Comma-separated CIDR blocks to route
  --netmask <MASK>        Network mask (default: 255.255.255.0)

Advanced Options:
  --dev <NAME>            TUN device name (auto-generated if not specified)
  --keepalive <SEC>       Keepalive interval in seconds (default: 30)
  --help                  Show help message
```

### Configuration Examples

#### Site-to-Site VPN
```bash
# Site A (192.168.1.0/24) - Server
sudo ./tun_bridge --server \
    --port 8080 \
    --ip 10.0.1.1 \
    --remote-ip 10.0.1.2 \
    --routes "192.168.1.0/24" \
    --psk-file /etc/tunnel.psk

# Site B (192.168.2.0/24) - Client  
sudo ./tun_bridge --client \
    --host site-a.example.com \
    --port 8080 \
    --ip 10.0.1.2 \
    --remote-ip 10.0.1.1 \
    --routes "192.168.2.0/24" \
    --psk-file /etc/tunnel.psk
```

#### Remote Access VPN
```bash
# VPN Server (Corporate Network)
sudo ./tun_bridge --server \
    --port 443 \
    --ip 10.8.0.1 \
    --remote-ip 10.8.0.2 \
    --routes "10.0.0.0/8,172.16.0.0/12" \
    --psk-file /etc/vpn.psk

# Remote Client
sudo ./tun_bridge --client \
    --host vpn.company.com \
    --port 443 \
    --ip 10.8.0.2 \
    --remote-ip 10.8.0.1 \
    --routes "0.0.0.0/0" \
    --psk-file /etc/vpn.psk
```

## 🔧 Systemd Service Installation

### Quick Installation
```bash
# Install as systemd service
sudo ./install_systemd.sh

# Configure service interactively
./configure_service.sh

# Uninstall systemd service
sudo ./uninstall_systemd.sh
```

### Manual Service Management
```bash
# Enable and start server service
sudo systemctl enable tun-bridge
sudo systemctl start tun-bridge

# Enable and start client service (edit server IP first)
sudo systemctl enable tun-bridge-client
sudo systemctl start tun-bridge-client

# Check service status
sudo systemctl status tun-bridge
sudo journalctl -u tun-bridge -f

# Service control
sudo systemctl stop tun-bridge
sudo systemctl restart tun-bridge
```

### Service Configuration Files
After installation, configuration files are located at:

- **Service Files**: `/etc/systemd/system/`
  - `tun-bridge.service` - Server mode service
  - `tun-bridge-client.service` - Client mode service

- **Configuration Templates**: `/etc/tun-bridge/`
  - `server.conf` - Server configuration template
  - `client.conf` - Client configuration template
  - `bridge.psk.example` - Example encryption key

### Custom Service Configuration
```bash
# Edit service parameters
sudo systemctl edit tun-bridge

# Add custom environment variables
[Service]
Environment="TUN_BRIDGE_LOG_LEVEL=DEBUG"
Environment="TUN_BRIDGE_EXTRA_ROUTES=10.10.0.0/16"
```

## 🔐 Security Features

### Encryption
- **Algorithm**: AES-256-GCM (Galois/Counter Mode)
- **Authentication**: HMAC-SHA256
- **Key Exchange**: Pre-shared key (PSK)
- **Perfect Forward Secrecy**: Session-based nonces

### Key Management
```bash
# Generate a secure PSK (256-bit)
./tun_bridge --generate-psk > /etc/tunnel.psk
chmod 600 /etc/tunnel.psk

# Verify PSK format
head -1 /etc/tunnel.psk | wc -c  # Should output 65 (64 hex chars + newline)
```

### Security Best Practices
1. **PSK Protection**: Store PSK files with restrictive permissions (600)
2. **Network Isolation**: Use dedicated tunnel IP ranges
3. **Firewall Rules**: Restrict tunnel access to required services only
4. **Regular Rotation**: Rotate PSKs periodically
5. **Monitoring**: Monitor tunnel traffic and connection logs

## 📊 Monitoring & Troubleshooting

### Log Analysis
```bash
# Real-time logs
sudo journalctl -u tun-bridge -f

# Error logs only
sudo journalctl -u tun-bridge -p err

# Logs since last boot
sudo journalctl -u tun-bridge -b

# Export logs
sudo journalctl -u tun-bridge --since "1 hour ago" > tunnel.log
```

### Network Debugging
```bash
# Check tunnel interface
ip addr show | grep tun

# Check routing table
ip route show | grep tun

# Monitor tunnel traffic
sudo tcpdump -i tun0 -n

# Test connectivity
ping -I tun0 <remote-ip>
traceroute -i tun0 <remote-ip>
```

### Performance Monitoring
```bash
# Network statistics
cat /proc/net/dev | grep tun

# Process statistics
ps aux | grep tun_bridge

# System resource usage
top -p $(pgrep tun_bridge)
```

### Common Issues

#### Connection Problems
```bash
# Check if TUN module is loaded
lsmod | grep tun

# Verify TUN device availability
ls -la /dev/net/tun

# Check firewall rules
sudo iptables -L -n | grep <port>
sudo ufw status | grep <port>
```

#### Permission Issues
```bash
# Ensure proper capabilities
sudo setcap cap_net_admin+ep ./tun_bridge

# Check SELinux/AppArmor restrictions
sudo ausearch -m avc -ts recent | grep tun_bridge
```

## 📁 Project Structure

```
linknet/
├── src/                          # Source code directory
│   ├── main.cpp                  # Main program entry point
│   ├── bridge.{cpp,h}            # Core bridging logic
│   ├── tun_manager.{cpp,h}       # TUN interface management
│   ├── socket_manager.{cpp,h}    # TCP socket handling
│   ├── crypto_manager.{cpp,h}    # Encryption and authentication
│   ├── route_manager.{cpp,h}     # Routing table management
│   └── utils.h                   # Common utilities and logging
├── install_systemd.sh            # Systemd service installer
├── uninstall_systemd.sh          # Systemd service uninstaller
├── configure_service.sh          # Interactive service configurator
├── tun-bridge.service            # Server systemd service template
├── tun-bridge-client.service     # Client systemd service template
├── Makefile                      # Build configuration
└── README.md                     # This documentation
```

### Code Architecture

#### Core Components
- **TunManager**: Handles TUN interface creation, configuration, and packet I/O
- **SocketManager**: Manages TCP client/server connections with keepalive
- **Bridge**: Coordinates packet forwarding between TUN and socket
- **CryptoManager**: Provides encryption, decryption, and authentication
- **RouteManager**: Manages system routing table modifications

#### Data Flow
```
TUN Interface ←→ TunManager ←→ Bridge ←→ SocketManager ←→ TCP Socket
                                ↕
                          CryptoManager (encryption/decryption)
                                ↕
                          RouteManager (routing setup/cleanup)
```

## 🔧 Development

### Building from Source
```bash
# Development build with debug symbols
make CXXFLAGS="-std=c++17 -Wall -Wextra -g -pthread"

# Release build with optimizations
make CXXFLAGS="-std=c++17 -Wall -Wextra -O3 -pthread -DNDEBUG"

# Static linking (for distribution)
make LDFLAGS="-static -lssl -lcrypto -lpthread"
```

### Testing
```bash
# Dependency check
make deps

# Build and basic test
make && echo "Build successful"

# Generate test PSK
./tun_bridge --generate-psk > test.psk

# Local loopback test
sudo ./tun_bridge --server --port 9999 --ip 192.168.100.1 --remote-ip 192.168.100.2 &
sudo ./tun_bridge --client --host 127.0.0.1 --port 9999 --ip 192.168.100.2 --remote-ip 192.168.100.1
```

### Contributing
1. Fork the repository
2. Create a feature branch: `git checkout -b feature-name`
3. Make your changes with proper testing
4. Commit with clear messages: `git commit -m "Add feature: description"`
5. Push to your fork: `git push origin feature-name`
6. Submit a pull request

## 📋 Requirements

### System Requirements
- **OS**: Linux kernel 3.10+ with TUN/TAP support
- **Architecture**: x86_64, ARM64, ARM32
- **Memory**: 10MB minimum, 50MB recommended
- **CPU**: Any modern processor (very low CPU usage)

### Software Dependencies
- **Compiler**: GCC 7+ or Clang 5+ (C++17 support)
- **Libraries**: 
  - OpenSSL 1.1.0+ (`libssl-dev` on Ubuntu/Debian)
  - pthread (usually included with glibc)
- **System**: 
  - systemd (for service installation)
  - iproute2 (`ip` command for routing)

### Installation Commands
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential libssl-dev iproute2

# CentOS/RHEL/Fedora
sudo yum install gcc-c++ openssl-devel iproute
# or
sudo dnf install gcc-c++ openssl-devel iproute

# Arch Linux
sudo pacman -S base-devel openssl iproute2
```

### Permissions
- **Root privileges** required for:
  - TUN interface creation and configuration
  - Routing table modifications
  - Binding to privileged ports (<1024)
- **Alternative**: Use capabilities instead of full root
  ```bash
  sudo setcap cap_net_admin+ep ./tun_bridge
  ```

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🤝 Support & Community

- **Issues**: Report bugs and request features on GitHub Issues
- **Documentation**: Additional docs available in the `/docs` directory
- **Wiki**: Community-maintained guides and examples
- **Security**: Report security vulnerabilities privately via email

## 🔄 Changelog

### v1.0.0 (Current)
- ✅ Core TUN bridging functionality
- ✅ AES-256-GCM encryption
- ✅ Automatic routing management
- ✅ Systemd service integration
- ✅ Comprehensive logging and monitoring

### Planned Features
- 🔄 Web-based management interface
- 🔄 Multi-client server support
- 🔄 Configuration file support
- 🔄 Performance metrics API
- 🔄 Docker container support

---

**LinkNet** - Secure, reliable network bridging made simple.
