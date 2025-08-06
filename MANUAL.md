# LinkNet 用户手册

## 概述

LinkNet 是一个基于 C++ 开发的 TUN 网络桥接工具，支持 NAT 穿越，能在两个 Linux 设备之间建立虚拟专用网络连接。

## 功能特性

- ✅ **TUN 接口管理**: 自动创建和配置 TUN 虚拟网络接口
- ✅ **TCP 桥接**: 通过 TCP 连接转发 IP 数据包
- ✅ **NAT 穿越**: 支持客户端主动连接，穿越 NAT 设备
- ✅ **双向转发**: 支持双向 IP 层数据包转发
- ✅ **连接保活**: 自动重连和心跳检测
- ✅ **多线程处理**: 并发处理读写操作
- ✅ **统计信息**: 实时显示传输统计

## 系统要求

- Linux 操作系统（内核支持 TUN/TAP）
- C++17 兼容编译器（g++ 7.0+）
- Root 权限（创建 TUN 接口需要）
- TCP 网络连接

## 安装说明

### 1. 克隆或下载项目
```bash
git clone <repository-url> linknet
cd linknet
```

### 2. 检查依赖
```bash
make deps
```

### 3. 编译项目
```bash
make
```

### 4. 安装到系统（可选）
```bash
sudo make install
```

## 基本用法

### 命令行参数

```bash
sudo ./tun_bridge [OPTIONS]
```

**必需参数：**
- `--mode MODE`: 运行模式，`server` 或 `client`
- `--local-ip IP`: 本地 TUN 接口 IP 地址
- `--remote-tun-ip IP`: 远程 TUN 接口 IP 地址

**可选参数：**
- `--dev DEVICE`: TUN 设备名称（默认：tun0）
- `--port PORT`: TCP 端口（默认：9000）
- `--remote-ip IP`: 远程服务器 IP（客户端模式必需）

### 服务端模式

```bash
sudo ./tun_bridge \
    --mode server \
    --dev tun0 \
    --local-ip 10.0.0.1 \
    --remote-tun-ip 10.0.0.2 \
    --port 9000
```

### 客户端模式

```bash
sudo ./tun_bridge \
    --mode client \
    --dev tun0 \
    --remote-ip 192.168.1.100 \
    --local-ip 10.0.0.2 \
    --remote-tun-ip 10.0.0.1 \
    --port 9000
```

## 使用场景

### 1. 基本点对点连接

**场景**: 两台 Linux 机器需要建立虚拟专用连接

**服务端（公网 IP: 1.2.3.4）:**
```bash
sudo ./tun_bridge --mode server --dev tun0 --local-ip 10.0.0.1 --remote-tun-ip 10.0.0.2 --port 9000
```

**客户端:**
```bash
sudo ./tun_bridge --mode client --dev tun0 --remote-ip 1.2.3.4 --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --port 9000
```

**测试连接:**
```bash
# 从客户端 ping 服务端
ping 10.0.0.1

# 从服务端 ping 客户端
ping 10.0.0.2
```

### 2. NAT 穿越场景

**场景**: 客户端在 NAT 后面，无法直接被访问

由于使用 TCP 且客户端主动连接，自然支持 NAT 穿越。客户端建立连接后，双向通信都通过这个 TCP 连接进行。

### 3. 网络扩展

可以配置路由规则，将整个子网通过 TUN 接口路由：

```bash
# 在服务端添加客户端网络路由
sudo ip route add 192.168.2.0/24 via 10.0.0.2 dev tun0

# 在客户端添加服务端网络路由
sudo ip route add 192.168.1.0/24 via 10.0.0.1 dev tun0
```

## 自动化脚本

### 设置脚本
```bash
sudo ./scripts/setup_tun.sh tun0 10.0.0.1 10.0.0.2
```

### 清理脚本
```bash
sudo ./scripts/cleanup_tun.sh tun0
```

## 测试验证

### 运行测试套件
```bash
./test.sh
```

### 运行演示
```bash
sudo ./demo.sh
```

### 手动测试连接

1. **启动服务端**
```bash
sudo ./tun_bridge --mode server --dev tun0 --local-ip 10.0.0.1 --remote-tun-ip 10.0.0.2 --port 9000
```

2. **启动客户端**（在另一台机器）
```bash
sudo ./tun_bridge --mode client --dev tun0 --remote-ip <server-ip> --local-ip 10.0.0.2 --remote-tun-ip 10.0.0.1 --port 9000
```

3. **测试连通性**
```bash
# 客户端测试
ping 10.0.0.1
traceroute 10.0.0.1

# 服务端测试
ping 10.0.0.2
traceroute 10.0.0.2
```

## 故障排除

### 常见问题

**1. "Permission denied" 错误**
- 确保使用 `sudo` 运行程序
- 检查 `/dev/net/tun` 设备是否存在

**2. "Address already in use" 错误**
- 检查端口是否已被占用：`netstat -tlnp | grep 9000`
- 使用不同端口或停止占用进程

**3. TUN 接口创建失败**
```bash
# 加载 TUN 模块
sudo modprobe tun

# 检查内核支持
ls -l /dev/net/tun
```

**4. 连接超时**
- 检查防火墙设置
- 确认服务端 IP 和端口正确
- 验证网络连通性

### 调试技巧

**1. 查看网络接口**
```bash
ip addr show tun0
ip route show dev tun0
```

**2. 监控网络流量**
```bash
# 监控 TUN 接口
sudo tcpdump -i tun0

# 监控 TCP 连接
sudo tcpdump -i any port 9000
```

**3. 查看系统日志**
```bash
dmesg | grep tun
journalctl -f
```

## 安全考虑

1. **权限**: 程序需要 root 权限，请确保来源可信
2. **网络**: TCP 连接未加密，敏感环境请考虑 VPN 外层加密
3. **防火墙**: 适当配置防火墙规则，限制访问源
4. **监控**: 建议监控连接状态和数据传输

## 性能优化

1. **MTU 设置**: 调整 TUN 接口 MTU 避免分片
```bash
sudo ip link set dev tun0 mtu 1400
```

2. **TCP 优化**: 程序已启用 TCP_NODELAY 和 keepalive

3. **系统调优**:
```bash
# 增加网络缓冲区
echo 'net.core.rmem_max = 16777216' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 16777216' >> /etc/sysctl.conf
```

## 扩展开发

项目结构清晰，支持扩展：

- `src/tun_manager.*`: TUN 接口管理
- `src/socket_manager.*`: TCP 连接管理  
- `src/bridge.*`: 数据桥接逻辑
- `src/main.cpp`: 主程序入口

可以添加的功能：
- 加密传输
- 数据压缩
- UDP 支持
- 多客户端支持
- Web 管理界面

## 许可证

本项目遵循开源许可证，具体请查看 LICENSE 文件。
