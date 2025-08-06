# LinkNet 连接测试指南

## 📋 测试准备

### 1. 生成共享密钥
```bash
# 生成一个安全的预共享密钥
./tun_bridge --generate-psk
# 输出例如: Generated PSK: ULLQMCxD34whD6I2skW8kXwPEpuouPMvgWGQOJej4mDnTHHSg6kELohbbv69H6Av
```

### 2. 环境准备
确保两台测试机器：
- Linux 系统，支持 TUN/TAP
- 已安装编译好的 `tun_bridge`
- 具有 root 权限
- 网络连通（可以互相访问）

## 🚀 建立连接

### 步骤1：启动服务端
```bash
# 在服务端机器（公网IP: 1.2.3.4）
sudo ./tun_bridge \
    --mode server \
    --dev tun0 \
    --local-ip 10.0.0.1 \
    --remote-tun-ip 10.0.0.2 \
    --psk "ULLQMCxD34whD6I2skW8kXwPEpuouPMvgWGQOJej4mDnTHHSg6kELohbbv69H6Av" \
    --port 9000
```

**预期输出：**
```
[INFO] Configuration:
[INFO]   Mode: server
[INFO]   Device: tun0
[INFO]   Port: 9000
[INFO]   Local TUN IP: 10.0.0.1
[INFO]   Remote TUN IP: 10.0.0.2
[INFO]   Encryption: Enabled
[INFO] Crypto manager initialized with PSK
[INFO] TUN interface created: tun0
[INFO] TUN interface configured: tun0 with IP 10.0.0.1
[INFO] Server listening on port 9000
[INFO] Waiting for client connection...
```

### 步骤2：启动客户端
```bash
# 在客户端机器
sudo ./tun_bridge \
    --mode client \
    --dev tun0 \
    --remote-ip 1.2.3.4 \
    --local-ip 10.0.0.2 \
    --remote-tun-ip 10.0.0.1 \
    --psk "ULLQMCxD34whD6I2skW8kXwPEpuouPMvgWGQOJej4mDnTHHSg6kELohbbv69H6Av" \
    --port 9000
```

**预期输出：**
```
[INFO] Configuration:
[INFO]   Mode: client
[INFO]   Device: tun0
[INFO]   Port: 9000
[INFO]   Local TUN IP: 10.0.0.2
[INFO]   Remote TUN IP: 10.0.0.1
[INFO]   Remote Server IP: 1.2.3.4
[INFO]   Encryption: Enabled
[INFO] Crypto manager initialized with PSK
[INFO] TUN interface created: tun0
[INFO] TUN interface configured: tun0 with IP 10.0.0.2
[INFO] Connected to server 1.2.3.4:9000
[INFO] Authentication successful (client)
[INFO] Starting encrypted packet bridge...
```

### 步骤3：服务端确认连接
服务端应该显示：
```
[INFO] Client connected from <client-ip>:port
[INFO] Authentication successful (server)
[INFO] Starting encrypted packet bridge...
```

## 🧪 连接测试

### 1. 基本连通性测试

#### Ping 测试
```bash
# 从客户端 ping 服务端
ping -c 4 10.0.0.1

# 从服务端 ping 客户端  
ping -c 4 10.0.0.2
```

**预期结果：**
```
PING 10.0.0.1 (10.0.0.1) 56(84) bytes of data.
64 bytes from 10.0.0.1: icmp_seq=1 ttl=64 time=10.2 ms
64 bytes from 10.0.0.1: icmp_seq=2 ttl=64 time=8.1 ms
64 bytes from 10.0.0.1: icmp_seq=3 ttl=64 time=9.3 ms
64 bytes from 10.0.0.1: icmp_seq=4 ttl=64 time=7.8 ms

--- 10.0.0.1 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss
```

#### 路由追踪
```bash
# 检查数据包路径
traceroute 10.0.0.1
```

**预期结果：**
```
traceroute to 10.0.0.1 (10.0.0.1), 30 hops max, 60 byte packets
 1  10.0.0.1 (10.0.0.1)  8.234 ms  8.123 ms  7.954 ms
```

### 2. 网络接口检查

#### 查看TUN接口状态
```bash
# 检查TUN接口
ip addr show tun0
ip route show dev tun0

# 检查接口统计
cat /proc/net/dev | grep tun0
```

**预期输出：**
```
3: tun0: <POINTOPOINT,MULTICAST,NOARP,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UNKNOWN group default qlen 500
    link/none 
    inet 10.0.0.2/24 scope global tun0
       valid_lft forever preferred_lft forever

10.0.0.1 dev tun0 scope link
```

### 3. 性能测试

#### 带宽测试（使用 iperf3）
```bash
# 在服务端启动 iperf3 服务
iperf3 -s -B 10.0.0.1

# 在客户端进行测试
iperf3 -c 10.0.0.1 -t 30 -i 5
```

#### 延迟测试
```bash
# 持续ping测试
ping -i 0.1 -c 100 10.0.0.1 | tail -n 3
```

#### 丢包测试
```bash
# 大包ping测试
ping -s 1472 -c 20 10.0.0.1
```

### 4. 加密验证测试

#### 抓包验证加密
```bash
# 在服务端或中间网络设备抓包
sudo tcpdump -i any port 9000 -X

# 同时在客户端发送ping
ping 10.0.0.1
```

**期望结果：** 应该看到加密的数据包，无法识别原始的ICMP内容。

#### 错误密钥测试
```bash
# 使用错误的PSK启动客户端（应该连接失败）
sudo ./tun_bridge --mode client --dev tun1 --remote-ip 1.2.3.4 \
    --local-ip 10.0.0.3 --remote-tun-ip 10.0.0.4 \
    --psk "wrong-password" --port 9000
```

### 5. 网络服务测试

#### HTTP服务测试
```bash
# 在服务端启动简单HTTP服务
python3 -m http.server 8080 --bind 10.0.0.1

# 在客户端访问
curl http://10.0.0.1:8080
wget http://10.0.0.1:8080/
```

#### SSH连接测试
```bash
# 如果服务端开启SSH
ssh user@10.0.0.1

# 或者在客户端开启SSH，从服务端连接
ssh user@10.0.0.2
```

#### 文件传输测试
```bash
# 使用scp传输文件
scp testfile.txt user@10.0.0.1:/tmp/
scp user@10.0.0.1:/tmp/testfile.txt ./received_file.txt

# 使用nc进行简单文件传输测试
# 服务端：
nc -l 10.0.0.1 1234 > received_file.txt
# 客户端：
nc 10.0.0.1 1234 < testfile.txt
```

## 🔧 故障排除

### 1. 连接问题

#### 检查防火墙
```bash
# 检查防火墙状态
sudo ufw status
sudo iptables -L

# 临时关闭防火墙测试
sudo ufw disable
```

#### 检查端口
```bash
# 检查端口是否被占用
netstat -tlnp | grep 9000
ss -tlnp | grep 9000
```

#### 检查网络连通性
```bash
# 测试TCP连接
telnet server-ip 9000
nc -zv server-ip 9000
```

### 2. 认证问题

#### 检查PSK
```bash
# 确保双方使用相同的PSK
echo "your-psk" | md5sum
```

#### 查看认证日志
查看程序输出中的认证相关信息：
- `[INFO] Authentication successful`
- `[WARNING] Authentication failed`

### 3. 性能问题

#### 调整MTU
```bash
# 降低MTU避免分片
sudo ip link set dev tun0 mtu 1400
```

#### 系统优化
```bash
# 增加网络缓冲区
echo 'net.core.rmem_max = 16777216' | sudo tee -a /etc/sysctl.conf
echo 'net.core.wmem_max = 16777216' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

## 📊 监控和日志

### 实时统计
程序会每60秒显示传输统计：
```
=== Bridge Statistics ===
TUN->Socket: 245 packets, 23456 bytes
Socket->TUN: 234 packets, 22134 bytes
Total: 479 packets, 45590 bytes
========================
```

### 系统监控
```bash
# 监控系统资源
top -p $(pgrep tun_bridge)
htop -p $(pgrep tun_bridge)

# 监控网络接口
watch -n 1 'cat /proc/net/dev | grep tun0'
```

### 调试模式
如果需要更详细的日志，可以修改程序中的日志级别或编译debug版本：
```bash
make debug
```

## ✅ 测试清单

- [ ] 基本连通性 (ping)
- [ ] 路由正确性 (traceroute)
- [ ] 接口状态正常
- [ ] 加密工作正常
- [ ] 性能符合预期
- [ ] 网络服务可用
- [ ] 长时间稳定运行
- [ ] 重连机制正常
- [ ] 错误处理正确

完成以上测试后，您的 LinkNet 隧道就可以投入生产使用了！
