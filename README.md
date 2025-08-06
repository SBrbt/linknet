# LinkNet - Secure Point-to-Point Network Bridge

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![OpenSSL](https://img.shields.io/badge/OpenSSL-AES--256--GCM-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

A simple and secure C++ tool that creates encrypted point-to-point network tunnels between two machines. LinkNet uses TUN interfaces and TCP sockets to bypass NAT firewalls and establish direct communication channels.

## ✨ Key Features

- **🔗 Point-to-Point Only**: Connects exactly two endpoints - no multi-client complexity
- **🛡️ NAT Traversal**: Works through NAT firewalls and restrictive networks
- **🔐 Military-Grade Encryption**: AES-256-GCM with HMAC-SHA256 authentication
- **⚡ High Performance**: Multi-threaded packet processing
- **🔄 Reliable**: Auto-reconnection and connection monitoring
- **🔧 Easy Deployment**: Systemd service integration included

## 🏗️ How It Works

```
   Machine A (Server)                    Machine B (Client)
   ┌─────────────────┐                  ┌─────────────────┐
   │   Your Apps     │                  │   Your Apps     │
   └─────────────────┘                  └─────────────────┘
           │                                    │
   ┌─────────────────┐    TCP :51860    ┌─────────────────┐
   │ LinkNet Server  │◄────────────────►│ LinkNet Client  │
   │   TUN: 10.0.1.1 │   (Encrypted)    │   TUN: 10.0.1.2 │
   └─────────────────┘                  └─────────────────┘
   
   Result: Machine A can ping 10.0.1.2, Machine B can ping 10.0.1.1
```

## 🚀 Quick Start

### 1. Build
```bash
git clone <your-repo-url>
cd linknet
make
```

### 2. Generate Encryption Key
```bash
./tun_bridge --generate-psk > tunnel.key
```

### 3. Start Server (Machine with public IP)
```bash
sudo ./tun_bridge --mode server \
    --local-ip 10.0.1.1 \
    --remote-tun-ip 10.0.1.2 \
    --psk-file tunnel.key \
    --enable-route
```

### 4. Start Client (Machine behind NAT)
```bash
# Copy tunnel.key to this machine first
sudo ./tun_bridge --mode client \
    --remote-ip <server-public-ip> \
    --local-ip 10.0.1.2 \
    --remote-tun-ip 10.0.1.1 \
    --psk-file tunnel.key \
    --enable-route
```

### 5. Test
```bash
# From server: ping client
ping 10.0.1.2

# From client: ping server  
ping 10.0.1.1
```

## ⚙️ Configuration Options

### Required Parameters
```bash
--mode client|server     # Operation mode
--local-ip IP           # This machine's tunnel IP
--remote-tun-ip IP      # Other machine's tunnel IP  
--psk-file FILE         # Encryption key file
```

### Client-Only Parameters
```bash
--remote-ip IP          # Server's public IP or hostname
```

### Optional Parameters
```bash
--port PORT             # TCP port (default: 51860)
--dev DEVICE            # TUN device name (default: tun0)
--enable-route          # Auto-add route for tunnel
--no-encryption         # Disable encryption (testing only)
--generate-psk          # Generate new encryption key
--help                  # Show help
```

### Common Use Cases

#### Standard Setup
```bash
# Server (port 51860)
sudo ./tun_bridge --mode server --local-ip 10.0.1.1 --remote-tun-ip 10.0.1.2 --psk-file key --enable-route

# Client  
sudo ./tun_bridge --mode client --remote-ip server.com --local-ip 10.0.1.2 --remote-tun-ip 10.0.1.1 --psk-file key --enable-route
```

#### Firewall-Friendly (port 443)
```bash
# Server (HTTPS port for firewall traversal)
sudo ./tun_bridge --mode server --port 443 --local-ip 10.0.1.1 --remote-tun-ip 10.0.1.2 --psk-file key --enable-route

# Client
sudo ./tun_bridge --mode client --remote-ip server.com --port 443 --local-ip 10.0.1.2 --remote-tun-ip 10.0.1.1 --psk-file key --enable-route
```

## 🔧 Service Installation

### Auto-Install with systemd
```bash
# Install and configure automatically
sudo scripts/install_systemd.sh
scripts/configure_service.sh

# Start services
sudo systemctl start tun-bridge        # Server mode
sudo systemctl start tun-bridge-client # Client mode

# Check status
sudo systemctl status tun-bridge
```

### Manual Service Control
```bash
# Start/stop services
sudo systemctl start|stop|restart tun-bridge
sudo systemctl enable|disable tun-bridge

# View logs
sudo journalctl -u tun-bridge -f
```

## 🔐 Security

LinkNet uses **AES-256-GCM encryption** with **HMAC-SHA256 authentication**. Every packet is encrypted and authenticated.

### Key Management
```bash
# Generate secure key
./tun_bridge --generate-psk > tunnel.key
chmod 600 tunnel.key

# Share key securely with other endpoint
scp tunnel.key user@other-machine:/path/to/tunnel.key
```

### Best Practices
- Store key files with `chmod 600` permissions
- Use unique keys for each tunnel
- Restrict port access with firewall rules
- Monitor logs for connection attempts
4. **Regular Rotation**: Rotate PSKs periodically (recommended monthly)
5. **Monitoring**: Monitor tunnel traffic and connection logs
## �️ Troubleshooting

### Connection Issues
```bash
# Check if TUN module is loaded
lsmod | grep tun

# Verify firewall settings
sudo ufw status | grep 51860

# Check tunnel status
ip addr show tun0
ping -I tun0 <remote-tunnel-ip>
```

### View Logs
```bash
# Real-time logs
sudo journalctl -u tun-bridge -f

# Recent errors
sudo journalctl -u tun-bridge -p err --since "1 hour ago"
```

## 📋 Requirements

- **OS**: Linux with TUN/TAP support
- **Compiler**: GCC 7+ or Clang 5+ (C++17)
- **Dependencies**: OpenSSL 1.1.0+, pthread
- **Permissions**: Root privileges for TUN interface

### Install Dependencies
```bash
# Ubuntu/Debian
sudo apt install build-essential libssl-dev

# CentOS/RHEL/Fedora  
sudo dnf install gcc-c++ openssl-devel

# Arch Linux
sudo pacman -S base-devel openssl
```

### Planned Features
- 🔄 Configuration file support
- 🔄 Performance metrics and statistics API
- 🔄 Docker container support

---

**LinkNet** - Secure, reliable point-to-point network bridging.

*Designed for security and simplicity. Connect private networks through NAT firewalls without compromising on safety.*
