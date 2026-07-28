# Task-12: 树莓派部署方案

> **状态：🔴 待开始** | **优先级：🥉 高** | **预计耗时：4h**
>
> **适用环境**：任意 OS（纯文本/Dockerfile/systemd unit 文件，不需 Ubuntu 20.04）
> **硬件依赖**：无（Docker build 可在 CI 中验证，systemd 可语法检查）
> **ROS 依赖**：仅 Docker 内（通过 CI 构建容器镜像验证）

---

## 前置条件

- 了解 Docker + Docker Compose 基础
- 了解 systemd unit 文件格式
- 了解本项目 PLATFORM.md §三 的物理部署映射
- **不需要**：真实树莓派硬件、Ubuntu 20.04 桌面环境

---

## 目标

为两台树莓派5（**无人机边缘** + **车机边缘**）构建一套 **可复现、可 CI 验证** 的部署方案。两台 Pi 共用 Docker 镜像，通过环境变量区分角色：

| 角色 | 环境变量 | 连接的硬件 | 运行的 ROS 节点 |
|------|----------|-----------|----------------|
| **无人机树莓派5** | `ROLE=drone` | Pixhawk 6C (USB `/dev/ttyACM0`) · D435i (USB3) · 3DR 数传 (UART `/dev/ttyAMA0`) | `drone_edge.launch` + `mavlink_bridge` |
| **车机树莓派5** | `ROLE=car` | STM32F407/MSPM0G3507 (UART `/dev/ttyAMA1`) · RPLIDAR (USB `/dev/ttyUSB0`) · ICM42688 (I2C) · HC-SR04×4 (GPIO) · OpenMV (UART) · 3DR 数传 (UART `/dev/ttyAMA0`) | `car_edge.launch` + `mavlink_bridge` + `edge_server_bridge` |

部署覆盖以下方面：

1. **Docker 容器化**：ROS Noetic + 项目节点 → 一键 `docker build`，同一镜像适配两个角色
2. **systemd 自启服务**：开机自动启动对应角色的 ROS edge node
3. **网络配置**：静态 IP + 3DR 数传参数（车机=ground端，无人机=air端）+ WiFi/4G 切换
4. **SSH 加固**：密钥认证 + fail2ban + 最小权限 + 防火墙
5. **健康检查**：节点存活监控 + 自动重启
6. **日志管理**：结构化日志 + logrotate

---

## 架构定位

```
物理部署拓扑 (Phase 1 实机):

实验室服务器 (192.168.1.100)
  Docker: air_ground_lab_server
    ↕ TCP :9090 (WiFi)
    
车机树莓派5 (192.168.1.10)              无人机树莓派5 (192.168.1.20)
  Docker: air_ground_car_edge              Docker: air_ground_drone_edge
    ↕ UART /dev/ttyAMA1 (STM32)             ↕ UART /dev/ttyAMA0 (Pixhawk 6C)
    ↕ I2C / GPIO / SPI (传感器)             ↕ USB (RealSense D435i)
    ↕ MAVLink UDP :14550 ──────────────→   接收 MAVLink 数传
```

---

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | DevOps: 容器化部署 · 开机自启 · 网络管理 · SSH 安全 · 健康检查 |
| **Modified Interface** | 新增部署层接口: 环境变量注入 (`AIR_GROUND_ROLE`, `CHASSIS`, `ROS_MASTER_URI`) · systemd unit 依赖声明 |
| **New Dependency** | Docker + Docker Compose · systemd · chrony (NTP) · fail2ban · NetworkManager |
| **ADR Required** | ADR-0005: 选择 Docker over 裸机部署的理由 · ADR-0006: 树莓派静态 IP 分配方案 |
| **Risk Level** | 🟡 Medium — 部署配置错误会导致实机无法启动，但可通过 Docker CI build 预先验证 |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §四)**：  
> 部署方案的切换 (Docker ↔ 裸机) 不应该影响 Research Layer 的任何代码。  
> 换硬件平台（树莓派5 → Jetson Orin）时，Dockerfile 只需改 base image。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | 树莓派5 (ARM64) | Jetson Orin Nano / 香橙派5 / x86 工控机 |
| **Permanent Interface** | Docker Compose 环境变量注入 · systemd unit 模板 · 标准化日志目录 `/var/log/air-ground/` | 保持不变 — 换硬件只需改 Dockerfile base image |
| **Temporary Implementation** | 静态 IP 手动配置 · SSH 密钥手动分发 · 单机 Docker Compose | v2: DHCP 预留 + mDNS · Ansible 自动化部署 · Kubernetes (K3s) 集群编排 · Watchtower 自动镜像更新 |

---

## 可执行步骤

### 12.1 目录结构

在仓库中创建部署配置目录（非 ROS Package）：

```bash
cd /path/to/research_compitition
mkdir -p src/deployment
cd src/deployment
```

最终结构：

```
src/deployment/
├── README.md                      # 部署总览 + 快速部署命令
├── docker/
│   ├── Dockerfile.edge            # 通用 Edge 节点镜像（drone + car 共用）
│   ├── Dockerfile.server          # 实验室服务器镜像 (已有, 仅作引用)
│   ├── docker-compose.edge.yml    # Edge 节点 Compose
│   └── .dockerignore
├── systemd/
│   ├── air-ground-car-edge.service      # 车机自启单元
│   ├── air-ground-drone-edge.service    # 无人机自启单元
│   ├── air-ground-healthcheck.service   # 健康检查定时器
│   └── air-ground-healthcheck.timer
├── network/
│   ├── 10-static-ips.nmconnection       # NetworkManager 静态 IP
│   ├── setup-3dr-radio.sh               # 3DR SiK 数传配置脚本
│   └── wifi-fallback.sh                 # WiFi 断开→4G 切换
├── ssh/
│   ├── sshd_hardening.conf              # sshd 安全配置片段
│   ├── setup-fail2ban.sh                # fail2ban 安装+配置
│   └── generate-ssh-keys.sh             # 密钥对生成（不提交私钥！）
├── healthcheck/
│   ├── check_nodes.py                   # ROS 节点存活检查
│   ├── check_topics.py                  # Topic 心跳检查
│   └── alert.sh                         # 告警脚本（LED / 蜂鸣器 / syslog）
└── logging/
    ├── ros-logrotate.conf               # ROS 日志轮转
    └── setup-journald.sh                # journald 持久化配置
```

### 12.2 Docker 容器化 (`docker/`)

#### Dockerfile.edge（无人机和车机共用）

```dockerfile
# Dockerfile.edge — 空地联合边缘节点 (drone / car)
# 构建: docker build -f docker/Dockerfile.edge -t air-ground-edge:v1 .
# 运行: 见 docker-compose.edge.yml

FROM ros:noetic-ros-core-focal

LABEL maintainer="vista777-nk"
LABEL description="Air-Ground Joint EQA Edge Node (Drone or Car)"

# --- 系统依赖 ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-pip python3-yaml python3-numpy \
    ros-noetic-mavros ros-noetic-mavros-extras \
    ros-noetic-ros-control ros-noetic-ros-controllers \
    ros-noetic-robot-localization \
    ros-noetic-tf2-ros ros-noetic-tf2-geometry-msgs \
    chrony usbutils i2c-tools \
    && rm -rf /var/lib/apt/lists/*

# --- Python 依赖 ---
RUN pip3 install --no-cache-dir pymavlink pyserial

# --- 创建非 root 用户 ---
RUN useradd -m -s /bin/bash airground && \
    usermod -aG dialout,i2c,plugdev airground

# --- 工作空间 ---
WORKDIR /home/airground/catkin_ws
COPY --chown=airground:airground ./src ./src

# --- 编译 ---
RUN /bin/bash -c "source /opt/ros/noetic/setup.bash && \
    apt-get update && rosdep install --from-paths src --ignore-src -y && \
    catkin build --summarize"

# --- 入口点 ---
COPY --chown=airground:airground ./src/deployment/docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER airground
ENTRYPOINT ["/entrypoint.sh"]
```

#### entrypoint.sh（根据环境变量决定启动 drone 还是 car）

```bash
#!/bin/bash
set -e

source /opt/ros/noetic/setup.bash
source /home/airground/catkin_ws/devel/setup.bash

ROLE="${AIR_GROUND_ROLE:-car}"
CHASSIS="${AIR_GROUND_CHASSIS:-diff}"
ROS_MASTER_URI="${ROS_MASTER_URI:-http://localhost:11311}"

export ROS_MASTER_URI

echo "=== Air-Ground Edge Node ==="
echo "Role:    ${ROLE}"
echo "Chassis: ${CHASSIS}"
echo "Master:  ${ROS_MASTER_URI}"
echo "==========================="

case "${ROLE}" in
    car)
        exec roslaunch air_ground_car_bringup car_edge.launch chassis:=${CHASSIS}
        ;;
    drone)
        exec roslaunch air_ground_drone_bringup drone_edge.launch
        ;;
    *)
        echo "Unknown role: ${ROLE}. Use 'car' or 'drone'."
        exit 1
        ;;
esac
```

#### docker-compose.edge.yml

```yaml
# docker-compose.edge.yml — 部署在树莓派上的 Edge 节点
version: '3.8'

services:
  edge-node:
    image: air-ground-edge:v1
    container_name: air_ground_edge
    restart: unless-stopped
    network_mode: "host"           # ROS 需要 host 网络
    privileged: true               # 访问 UART/I2C/GPIO
    environment:
      - AIR_GROUND_ROLE=${ROLE:-car}
      - AIR_GROUND_CHASSIS=${CHASSIS:-diff}
      - ROS_MASTER_URI=${ROS_MASTER_URI:-http://192.168.1.100:11311}
    volumes:
      - /dev:/dev:ro               # 设备节点
      - /sys:/sys:ro               # 硬件信息
      - /etc/localtime:/etc/localtime:ro
      - air_ground_logs:/home/airground/.ros/log
    devices:
      - /dev/ttyAMA0:/dev/ttyAMA0  # UART: 3DR 数传 (车机/无人机共用)
      - /dev/ttyAMA1:/dev/ttyAMA1  # UART: STM32/MSPM0 下位机 (仅车机)
      - /dev/ttyACM0:/dev/ttyACM0  # USB: Pixhawk 6C (仅无人机)
      - /dev/video0:/dev/video0    # USB: D435i RGB (仅无人机)
      - /dev/video2:/dev/video2    # USB: D435i Depth (仅无人机)
      - /dev/i2c-1:/dev/i2c-1      # I2C: ICM42688 (仅车机)
    logging:
      driver: "json-file"
      options:
        max-size: "50m"
        max-file: "5"

volumes:
  air_ground_logs:
```

### 12.3 systemd 自启服务 (`systemd/`)

#### air-ground-car-edge.service

```ini
[Unit]
Description=Air-Ground Car Edge Node
Documentation=https://github.com/vista777-nk/research_compitition
After=network.target docker.service chrony.service
Requires=docker.service
Wants=network.target chrony.service

[Service]
Type=simple
User=airground
Group=airground
WorkingDirectory=/opt/air-ground
Environment="ROLE=car"
Environment="CHASSIS=diff"
Environment="ROS_MASTER_URI=http://192.168.1.100:11311"
ExecStartPre=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml pull
ExecStart=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml up
ExecStop=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml down
Restart=always
RestartSec=10
TimeoutStartSec=120
TimeoutStopSec=30

# 安全加固
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/opt/air-ground /var/log/air-ground

[Install]
WantedBy=multi-user.target
```

#### air-ground-drone-edge.service

```ini
[Unit]
Description=Air-Ground Drone Edge Node
Documentation=https://github.com/vista777-nk/research_compitition
After=network.target docker.service chrony.service
Requires=docker.service
Wants=network.target chrony.service
# 无人机 Pi 必须先检测到 Pixhawk USB 再启动
Requires=dev-ttyACM0.device
After=dev-ttyACM0.device

[Service]
Type=simple
User=airground
Group=airground
WorkingDirectory=/opt/air-ground
Environment="ROLE=drone"
Environment="ROS_MASTER_URI=http://192.168.1.100:11311"
ExecStartPre=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml pull
ExecStart=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml up
ExecStop=/usr/bin/docker compose -f /opt/air-ground/docker-compose.edge.yml down
Restart=always
RestartSec=10
TimeoutStartSec=120
TimeoutStopSec=30

# 安全加固
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/opt/air-ground /var/log/air-ground

[Install]
WantedBy=multi-user.target
```

> **无人机 vs 车机 systemd 关键区别**：无人机 service 额外声明 `Requires=dev-ttyACM0.device`（Pixhawk USB），确保飞控连接就绪后才启动 ROS 节点。车机无此依赖（STM32/MSPM0 走 UART 常驻）。

#### air-ground-healthcheck.service + timer

```ini
# air-ground-healthcheck.service
[Unit]
Description=Air-Ground Node Health Check
After=air-ground-car-edge.service air-ground-drone-edge.service

[Service]
Type=oneshot
User=airground
ExecStart=/opt/air-ground/healthcheck/check_nodes.py
ExecStartPost=/opt/air-ground/healthcheck/alert.sh
StandardOutput=journal
```

```ini
# air-ground-healthcheck.timer
[Unit]
Description=Run Air-Ground health check every 30 seconds

[Timer]
OnBootSec=60
OnUnitActiveSec=30
AccuracySec=5

[Install]
WantedBy=timers.target
```

### 12.4 网络配置 (`network/`)

#### 静态 IP 分配

| 设备 | IP | 用途 |
|------|-----|------|
| 实验室服务器 | `192.168.1.100` | ROS Master + GPU 推理 |
| 车机树莓派 | `192.168.1.10` | Edge + 通信中继 |
| 无人机树莓派 | `192.168.1.20` | Edge + MAVLink 桥 |
| 3DR 数传 (空地) | 点对点，不参与局域网 | MAVLink UDP |

#### setup-3dr-radio.sh

```bash
#!/bin/bash
# 配置 3DR SiK 数传电台参数
# 用法: sudo ./setup-3dr-radio.sh [air|ground]

ROLE="${1:-ground}"
SERIAL_DEV="/dev/ttyUSB0"
BAUD=57600

echo "Configuring 3DR Radio as ${ROLE}..."

# 使用 pymavlink 工具设置参数
python3 - <<EOF
from pymavlink import mavutil
mav = mavutil.mavlink_connection('${SERIAL_DEV}', baud=${BAUD})
# 设置参数 (具体 AT 命令取决于 3DR SiK 固件版本)
# 详见 https://ardupilot.org/copter/docs/common-3dr-radio.html
EOF

echo "3DR Radio configured."
```

### 12.5 无人机专属硬件连接 (`network/drone-hardware.md`)

无人机树莓派5 的硬件连接比车机更敏感——Pixhawk USB 断开意味着飞控失联，D435i USB3 带宽不足会导致深度图丢帧。以下为无人机 Pi 专属配置：

#### 硬件连接表

| 外设 | Pi 接口 | 设备路径 | 协议 | 备注 |
|------|---------|----------|------|------|
| Pixhawk 6C | USB-C | `/dev/ttyACM0` | MAVLink 2 (115200→921600) | MAVROS 连接，systemd 强依赖此设备 |
| RealSense D435i | USB3 (蓝色) | `/dev/video0` `/dev/video2` | UVC + HID | **必须接 USB3 口**，USB2 带宽不足 |
| 3DR SiK 数传 | UART GPIO (14/15) | `/dev/ttyAMA0` | 透传 57600 | 角色=`air`，与车机 3DR 配对 |
| 供电 | GPIO 5V (Pin 2/4) | — | — | 由无人机 BEC (5V 3A) 供电，非 USB |

#### MAVROS udev 规则（防设备漂移）

```bash
# src/deployment/network/99-pixhawk.rules
# 确保 Pixhawk 始终映射为 /dev/pixhawk (符号链接)
# 放入 /etc/udev/rules.d/

SUBSYSTEM=="tty", ATTRS{idVendor}=="26ac", ATTRS{idProduct}=="0011", SYMLINK+="pixhawk"
SUBSYSTEM=="tty", ATTRS{idVendor}=="26ac", ATTRS{idProduct}=="0032", SYMLINK+="pixhawk"
```

#### MAVROS Launch 配置 (drone Pi 专属)

```xml
<!-- src/air_ground_drone_bringup/launch/drone_mavros.launch -->
<launch>
  <arg name="fcu_url" default="/dev/pixhawk:921600"/>
  <arg name="gcs_url" default="udp://:14550@192.168.1.10:14550"/>

  <include file="$(find mavros)/launch/px4.launch">
    <arg name="fcu_url" value="$(arg fcu_url)"/>
    <arg name="gcs_url" value="$(arg gcs_url)"/>
  </include>
</launch>
```

> **关键**：`gcs_url` 指向车机 Pi (`192.168.1.10:14550`)，因为 MAVLink 数传链路是 **无人机 Pi → (3DR air) → (3DR ground) → 车机 Pi**。车机 Pi 是 MAVLink 消息的汇集点。

#### D435i USB 带宽检查脚本

```bash
#!/bin/bash
# src/deployment/network/check-d435i-usb.sh
# 验证 D435i 插在 USB3 口上 (速度=5000M 而非 480M)

for dev in /sys/bus/usb/devices/*/speed; do
    speed=$(cat "$dev" 2>/dev/null)
    if [ "$speed" = "5000" ]; then
        echo "✓ USB3 SuperSpeed port found"
        exit 0
    fi
done

echo "⚠ WARNING: No USB3 port detected. D435i may have bandwidth issues." >&2
exit 1
```

### 12.6 SSH 加固 (`ssh/`)

#### sshd_hardening.conf

```ini
# 追加到 /etc/ssh/sshd_config

# 禁止 root 登录
PermitRootLogin no

# 只允许密钥认证
PasswordAuthentication no
ChallengeResponseAuthentication no
PubkeyAuthentication yes

# 限制用户
AllowUsers airground

# 空闲超时
ClientAliveInterval 300
ClientAliveCountMax 0

# 限制并发连接
MaxSessions 3
MaxAuthTries 3
```

#### setup-fail2ban.sh

```bash
#!/bin/bash
# 安装并配置 fail2ban 保护 SSH
set -e

sudo apt update
sudo apt install -y fail2ban

sudo tee /etc/fail2ban/jail.local <<'EOF'
[sshd]
enabled = true
port = ssh
filter = sshd
logpath = /var/log/auth.log
maxretry = 3
bantime = 3600
findtime = 600
EOF

sudo systemctl enable --now fail2ban
echo "fail2ban installed and running."
```

#### generate-ssh-keys.sh

```bash
#!/bin/bash
# 生成 SSH 密钥对（私钥存储在用户本地，不提交到仓库）
# 公钥需要部署到树莓派的 ~/.ssh/authorized_keys

KEYFILE="$HOME/.ssh/air_ground_rsa"
if [ -f "$KEYFILE" ]; then
    echo "Key already exists: $KEYFILE"
    exit 0
fi

ssh-keygen -t rsa -b 4096 -f "$KEYFILE" -N "" -C "air-ground-edge"
echo "Key generated: $KEYFILE"
echo "Public key (add to ~/.ssh/authorized_keys on Raspberry Pi):"
cat "${KEYFILE}.pub"
```

### 12.7 健康检查 (`healthcheck/`)

#### check_nodes.py

```python
#!/usr/bin/env python3
"""ROS 节点存活检查 (独立于 ROS 环境运行，通过 TCP 探测 ROS Master)."""

import socket
import sys
import xmlrpc.client
from datetime import datetime

ROS_MASTER_URI = "http://192.168.1.100:11311"
EXPECTED_NODES = {
    "car":  ["car_preprocessor", "mavlink_bridge", "edge_server_bridge"],
    "drone": ["drone_preprocessor", "mavlink_bridge"],
}

def check_ros_master():
    """检查 ROS Master 是否可达."""
    try:
        proxy = xmlrpc.client.ServerProxy(ROS_MASTER_URI)
        code, msg, _ = proxy.getPid("/rosout")
        return code == 1, msg
    except Exception as e:
        return False, str(e)

def main():
    ok, msg = check_ros_master()
    timestamp = datetime.now().isoformat()

    if ok:
        print(f"[{timestamp}] ROS Master OK: {msg}")
        sys.exit(0)
    else:
        print(f"[{timestamp}] ROS Master DOWN: {msg}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
```

### 12.8 日志管理 (`logging/`)

#### ros-logrotate.conf

```
# ROS 日志轮转 — 放入 /etc/logrotate.d/ros
/var/log/air-ground/*.log {
    daily
    rotate 7
    maxsize 100M
    compress
    delaycompress
    missingok
    notifempty
    copytruncate
}
```

---

## 验收标准

- [ ] `docker build -f docker/Dockerfile.edge` 可在 CI 中成功构建
- [ ] `systemd-analyze verify` 对所有 `.service` 文件无语法错误
- [ ] `bash -n` 对所有 `.sh` 脚本无语法错误
- [ ] `check_nodes.py` 可在任意有 Python 3 的机器上运行（检测 ROS Master 可连通性）
- [ ] SSH 加固脚本不包含硬编码密码/密钥
- [ ] `README.md` 包含完整的"从烧录系统到开机自启"部署步骤

---

## 给 Subagent 的执行建议

1. **所有工作都在 `src/deployment/` 下进行**，不污染 ROS Package
2. **不要假设任何特定网络环境**：所有 IP、端口通过环境变量或配置文件注入
3. **所有脚本用 `set -e` 开头**（遇错即停）
4. **私钥生成脚本只生成密钥对，绝不提交私钥到仓库**（`.gitignore` 已含 `*.pem` `*_rsa` `id_*`）
5. **systemd unit 必须包含 `[Install]` 段**，否则 `systemctl enable` 无效
6. **Docker 用 `network_mode: host`**：ROS 依赖多播和动态端口
7. **所有配置中敏感信息用模板变量**（`${ROLE}`, `${ROS_MASTER_URI}` 等）

---

## 参考资料

| 文件 | 内容 |
|------|------|
| `project-prometheus-tasks/PLATFORM.md` §三 | 物理部署映射 |
| `project-prometheus-tasks/PLATFORM.md` §十 | 时钟同步方案 |
| `src/air_ground_car_bringup/launch/car_edge.launch` | 车机边缘节点启动配置 |
| `src/air_ground_drone_bringup/launch/drone_edge.launch` | 无人机边缘节点启动配置 |
| `SECURITY.md` | 安全策略（SSH / 网络 / 数据保护） |

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 · 部署先行*
