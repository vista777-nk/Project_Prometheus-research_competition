# Task-12: 树莓派部署方案

> **状态：✅ 已完成（2026-07-30）** | **优先级：🥉 高** | **实际耗时：约 4h**
>
> **产出**：`src/deployment/`（8 个子目录 37 个文件）·
> [ADR-0005](../docs/decisions/ADR-0005.md)（容器化部署）·
> [ADR-0006](../docs/decisions/ADR-0006.md)（静态 IP 与时钟主从）·
> [ADR-0007](../docs/decisions/ADR-0007.md)（`network_mode: host` 的暴露面与重估触发条件）·
> CI job `validate-deployment` + `build-edge-image`
>
> **验收结果**：静态校验 25 项通过（含 45 个 Host 单元测试）。
> 与本文档共 **14 处偏差，全部是"照抄会失败"的问题**，逐条见
> [`src/deployment/README.md` §6](../src/deployment/README.md#6-与任务文档task-12的偏差)
> ——**动手前先看那张表**（本页"可执行步骤"开头也有摘要）。
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

> ## ⚠ 下面的代码块**不要照抄**
>
> 本节写于实现之前。实现过程中发现其中 **14 处照抄会直接失败**——
> 不是风格问题，是"构建报错""服务起不来""把自己锁在门外"这一类。
> 权威实现是 `src/deployment/`，偏差逐条附理由列在
> **[`src/deployment/README.md` §6](../src/deployment/README.md#6-与任务文档task-12的偏差)**。
>
> 最容易踩的四条（完整 14 条见上面链接）：
>
> | 本节原文 | 照抄的后果 | 正确做法 |
> |---|---|---|
> | `FROM ros:noetic-ros-core-focal` | `rosdep install` 直接报错——ros-core 里 rosdep 没初始化过 | 用 `ros-base` |
> | `roslaunch ... chassis:=${CHASSIS}` | `RLException: unused args`，第一次启动就失败 | `default_chassis:=` |
> | `ChallengeResponseAuthentication no` | OpenSSH 9.x 已移除该项，`sshd -t` 报错、sshd 起不来——**把自己锁在门外** | `KbdInteractiveAuthentication no` |
> | `ExecStartPre=docker compose pull` | Phase 1 没有 registry，开机 pull 必然失败 → 整个单元起不来 | `require-image.sh` 查本地镜像 |
>
> 本节保留原文不改，是因为它记录的是**当初的设想**；
> 改掉它就看不出实现过程中学到了什么了。


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
│   ├── Dockerfile.server          # 实验室服务器镜像 (Phase 2 实现, 当前仅占位)
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
EDGE_MODE="${EDGE_MODE:-sim}"  # sim=仿真 launch | real=实机 launch (task-14 提供)
ROS_MASTER_URI="${ROS_MASTER_URI:-http://localhost:11311}"

export ROS_MASTER_URI

echo "=== Air-Ground Edge Node ==="
echo "Role:    ${ROLE}"
echo "Chassis: ${CHASSIS}"
echo "Master:  ${ROS_MASTER_URI}"
echo "==========================="

case "${ROLE}" in
    car)
        if [ "${EDGE_MODE}" = "real" ]; then
            exec roslaunch air_ground_car_bringup car_edge_real.launch chassis:=${CHASSIS}
        else
            exec roslaunch air_ground_car_bringup car_edge.launch chassis:=${CHASSIS}
        fi
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

#### docker-compose.edge.yml（公共基础，角色通过 override 文件叠加）

```yaml
# docker-compose.edge.yml — Edge 节点公共基础 (车机/无人机共用)
# 角色特定配置由 docker-compose.car.yml / docker-compose.drone.yml 覆盖
version: '3.8'

services:
  edge-node:
    image: air-ground-edge:v1
    container_name: air_ground_edge
    restart: unless-stopped
    network_mode: "host"
    privileged: true  # 注: 与 systemd 加固并存仅为开发便利, 非安全边界
    environment:
      - AIR_GROUND_ROLE=${ROLE:-car}
      - AIR_GROUND_CHASSIS=${CHASSIS:-diff}
      - EDGE_MODE=${EDGE_MODE:-real}
      - ROS_MASTER_URI=${ROS_MASTER_URI:-http://192.168.1.100:11311}
    volumes:
      - /etc/localtime:/etc/localtime:ro
      - /var/log/air-ground:/home/airground/.ros/log
    logging:
      driver: "json-file"
      options:
        max-size: "50m"
        max-file: "5"
    # 公共设备: 3DR 数传 (车机/无人机均有)
    devices:
      - /dev/ttyAMA0:/dev/ttyAMA0  # UART: 3DR SiK 数传 (Pi 硬件串口)
```

#### docker-compose.car.yml（车机角色 override，仅车机独有设备）

```yaml
# docker-compose.car.yml — 车机角色覆盖
# 用法: docker compose -f docker-compose.edge.yml -f docker-compose.car.yml up
version: '3.8'

services:
  edge-node:
    environment:
      - AIR_GROUND_ROLE=car
    devices:
      - /dev/ttyAMA1:/dev/ttyAMA1  # UART: STM32/MSPM0 下位机
      - /dev/i2c-1:/dev/i2c-1      # I2C: ICM42688 IMU
      - /dev/ttyUSB0:/dev/ttyUSB0   # USB-UART: RPLIDAR A1
```

#### docker-compose.drone.yml（无人机角色 override，仅无人机独有设备）

```yaml
# docker-compose.drone.yml — 无人机角色覆盖
# 用法: docker compose -f docker-compose.edge.yml -f docker-compose.drone.yml up
version: '3.8'

services:
  edge-node:
    environment:
      - AIR_GROUND_ROLE=drone
    devices:
      - /dev/ttyACM0:/dev/ttyACM0   # USB: Pixhawk 6C
      - /dev/video0:/dev/video0     # USB: D435i RGB
      - /dev/video2:/dev/video2     # USB: D435i Depth
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
# 注意: 本脚本在 PC 端配置 3DR 电台 (USB-UART /dev/ttyUSB0)
#       部署到树莓派后, 电台连接树莓派硬件串口 /dev/ttyAMA0, 参数已在电台内, 无需重新配置

ROLE="${1:-ground}"
# PC 端: 电台通过 USB-UART 连接 → /dev/ttyUSB0
# Pi 端: 电台通过硬件串口连接 → /dev/ttyAMA0 (部署阶段无需再运行本脚本)
SERIAL_DEV="${2:-/dev/ttyUSB0}"
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
"""ROS 节点存活检查 (独立于 ROS 环境运行, 通过 TCP 探测 ROS Master).

设计意图: 本脚本检查 ROS Master 可达性。容器内节点存活由 systemd + docker restart policy 保障。
如需检查具体 rosnode 列表, 应在容器内执行 `rosnode list` (不在本脚本职责范围)。
"""

import sys
import xmlrpc.client
from datetime import datetime

ROS_MASTER_URI = "http://192.168.1.100:11311"

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

- [x] `docker build` 可在 CI 中成功构建（`build-edge-image` job，ARM64 + qemu）
- [x] `systemd-analyze verify` 对所有 `.service`/`.timer` 无语法错误（CI 执行）
- [x] `bash -n` 对所有 `.sh` 脚本无语法错误（本地 + CI，10 个脚本）
- [x] `check_nodes.py` 可在任意有 Python 3 的机器上运行（纯标准库，无 rospy）
- [x] SSH 加固脚本不包含硬编码密码/密钥（校验脚本含专门的泄漏扫描项）
- [x] `README.md` 包含完整的"从烧录系统到开机自启"部署步骤（§4，9 个小节）

### 超出验收标准的部分

- `validate.sh` —— 本地与 CI **共用同一份**静态校验入口，7 类检查
- `test_healthcheck.py` —— 31 个 Host 单元测试，覆盖健康判定的纯逻辑
- `validate_consistency.py` —— 跨文件一致性（compose 结构 / 话题名与 ROS 配置对齐）
- 校验脚本自身做过**负向测试**，详见实施记录

---

## 实施记录（2026-07-30）

### 交付清单

| 路径 | 内容 |
|------|------|
| `src/deployment/README.md` | 从烧录系统到开机自启的完整流程 + 排查手册 + 已知限制 |
| `src/deployment/validate.sh` | **静态校验入口，本地与 CI 同一份** |
| `src/deployment/validate_consistency.py` | compose 结构 + 话题名与 ROS 配置的一致性 |
| `src/deployment/install.sh` | 装到树莓派上，幂等，不自动启用服务 |
| `src/deployment/docker/` | Dockerfile ×2 + entrypoint + compose ×4 + .env.example |
| `src/deployment/systemd/` | 车机/无人机/健康检查 unit + timer |
| `src/deployment/scripts/` | `require-image.sh` · `wait-for-device.sh` |
| `src/deployment/network/` | udev 规则 · 3DR AT 配置 · USB3 检查 · 静态 IP 模板 · 无人机接线文档 |
| `src/deployment/chrony/` | 车机=NTP 服务器 / 无人机+服务器=客户端 |
| `src/deployment/ssh/` | sshd 加固 · fail2ban · 密钥生成 |
| `src/deployment/healthcheck/` | 纯判定逻辑 + 两个 CLI + 告警 + **31 个单元测试** |
| `src/deployment/logging/` | logrotate + journald 持久化 |
| `docs/decisions/ADR-0005.md` | 容器化部署（5 条决策 + 3 个否决方案） |
| `docs/decisions/ADR-0006.md` | 静态 IP 与时钟主从（4 条决策 + 3 个否决方案） |
| `.github/workflows/ci.yml` | 新增 `validate-deployment` + `build-edge-image` |
| `.gitattributes` | 补 `*.service`/`*.rules`/`Dockerfile*` 等的 `eol=lf` |

### 最要紧的一件事：`.gitattributes` 必须先落地

`systemd` / `sshd` / `udev` / `Dockerfile` 都是**逐行解析**的，CRLF 会让它们
在 Linux 上直接失效 —— 不是"看起来有点乱"，而是：

* systemd 把 `Type=simple
` 里的 `
` 当成值的一部分 → 单元加载失败
* sshd 配置行尾带 `
` → 参数非法，sshd 拒绝启动 → **把自己锁在门外**
* udev 规则静默不匹配 → 符号链接不出现，容器起不来且没有任何报错

这正是 2026-07-25 编码事故的同一类问题，仓库日记里那条教训是
"`.gitattributes` 必须作为第一批提交"。因此本任务**先写属性规则，再写被它保护的文件**。

### 与本文档的 14 处偏差

全部是"照抄会失败"的问题，不是风格分歧。完整表格见
[`src/deployment/README.md` §6](../src/deployment/README.md)，这里只摘最关键的四条：

| # | 文档原文 | 问题 |
|:---:|---|---|
| 4 | `privileged: true` **且** `devices:` 白名单 | 两者矛盾。privileged 已给了全部设备，白名单一行都不起作用，**只是看起来像做了权限控制** |
| 6 | `ExecStartPre=docker compose pull` | Phase 1 离线分发没有 registry，开机 pull 必然失败 → `ExecStartPre` 失败 → 整个单元起不来。症状是"实验室能起、拉到场地就起不来" |
| 7 | `roslaunch ... chassis:=${CHASSIS}` | `car_edge.launch` 声明的是 `default_chassis`。roslaunch 对未声明参数是**硬错误**，第一次启动就会失败 |
| 9 | `ChallengeResponseAuthentication no` | 该选项在 OpenSSH 9.x 已**移除**，而 Bookworm 带的是 9.2。照抄会让 `sshd -t` 报错、sshd 起不来 —— 而这台 Pi 可能已经装在无人机上了 |

另有两处是"文档给的东西其实没做事"：

* §12.4 的 `setup-3dr-radio.sh` 只建了个 mavutil 连接就打印 "3DR Radio configured."，
  **一个参数都没设**。改写为真实的 SiK AT 命令实现，且默认只读。
* §12.5 的 `check-d435i-usb.sh` 扫描系统里有没有任何 5000M 端口 ——
  树莓派5 本身就有 USB3 口，所以这个检查**恒为真**，相机插在 USB2 上也报通过。
  改为定位相机自身所在的那个端口。

### 一处 systemd 依赖的连锁问题

§12.3 用 `Requires=dev-ttyACM0.device` 让无人机等飞控就绪，§12.5 又给了
一条 `SYMLINK+="pixhawk"` 的 udev 规则。这两段单独看都没错，**合起来跑不通**：

只写 `SYMLINK` 的话 `/dev/pixhawk` 会出现，但 systemd 里**并不存在
`dev-pixhawk.device` 这个单元** —— 设备单元名是从设备节点真实路径推导的。
要让符号链接也成为可依赖的单元名，需要 `TAG+="systemd"` 与
`ENV{SYSTEMD_ALIAS}="/dev/pixhawk"` 两件事（systemd.device(5)）。

同时把 `Requires=` 改成了 `Wants=` + 显式等待脚本，理由写在 unit 文件头：
`Requires` 硬阻塞意味着"飞控没插 = 相机数传遥测全都不启动"，
而这些在飞控没接时仍然有用。故障范围不该被启动依赖放大。

### 校验脚本自己的负向测试暴露了一个假绿灯

写完 `validate.sh` 之后做了负向测试 —— 故意塞进去一个语法错误的脚本、
一个 CRLF 的 unit、一份假私钥，看它会不会红。

前后两项正常红了，**中间那项没有**。

原因：Git for Windows 附带的 MSYS `grep` 在读入时会静默剥掉 CR，
于是 `grep $'
'` 在 Windows 上永远匹配不到。而 Linux CI 上 grep 行为正常 ——
结果就是本地永远绿、真出问题时本地反而发现不了，属于最坏的一类假绿灯。
改用"剥掉 CR 前后字节数是否变化"判断后三项全部正确报红。

**教训**：校验脚本必须自己先过一遍负向测试。一个从来没红过的检查，
和没有这个检查是等价的 —— 而它还会让人以为已经查过了。

### 已知限制

- **整套配置尚未在真实树莓派上执行过。** Phase 1 无硬件，目标是
  "静态可校验 + CI 可构建"。首次上机按 README §4 逐步走。
- **`docker build` 与 `systemd-analyze verify` 未在本地执行**
  （开发机是 Windows，无 Docker/systemd），由 CI 首次验证。
  ARM64 构建更是只能在 CI 上做（需 buildx + qemu）。
- **udev 规则里的 VID/PID 与序列号需上机核对。** 3DR 数传与 RPLIDAR
  同为 CP2102（`10c4:ea60`），必须靠序列号区分；规则里留的是
  `REPLACE_WITH_*_SERIAL` 占位符，不填则那两条规则不匹配任何设备 ——
  这是刻意的：宁可符号链接不出现（`wait-for-device.sh` 会报出来），
  也不要两个设备随机抢同一个名字。
- **`setup-3dr-radio.py` 未在真实电台上验证。** AT 命令集依据 SiK 公开文档，
  默认只读模式。
- **`car_edge_real.launch` 尚不存在**（task-14 交付）；`EDGE_MODE=real`
  时 entrypoint 会检测到并降级，同时打印说明。
- **网络降级（WiFi→4G）与本地缓存模式未实现**，Phase 2 规划（评审建议 4）。

### 对下游任务的影响

| 任务 | 影响 |
|------|------|
| **task-13** | CI 已有两个部署相关 job 模板；`validate.sh` 的"本地/CI 同一份"模式可推广到其他静态检查 |
| **task-14** | 需交付 `car_edge_real.launch`；MAVROS 的 `fcu_url` 用 `/dev/pixhawk`、`gcs_url` 指向车机 `192.168.1.10:14550` |
| **task-15** | 集成验证直接跑 `healthcheck/check_nodes.py`（退出码即结论）+ `chronyc tracking` |
| **换硬件平台** | 改 `Dockerfile.edge` 的 base image 与 compose 里的设备路径即可 |

---

## ⓘ 优化建议（混元3 评审）

1. **收敛 Docker 权限范围**：当前 `privileged: true` 为开发便利选项，生产部署建议改为逐个挂载具体设备文件（`--device /dev/ttyAMA0 --device /dev/i2c-1`），仅授予必要的硬件访问权限，符合最小权限原则。

2. **明确时钟同步主从关系**：chrony 配置中，**车机树莓派 (192.168.1.10) 作为局域网 NTP 服务器**（stratum 10, 以 GPS/PPS 为参考源），**无人机树莓派 (192.168.1.20) 和实验室服务器 (192.168.1.100) 向车机同步**。车机 GPS 信号优于无人机（地面更稳定），且空地通信拓扑中车机是 MAVLink 中继节点。

3. **镜像更新方案**：Phase 1 采用离线刷入（`docker save/load` 通过 U 盘），Phase 2 引入 Watchtower 自动拉取 Docker Hub 新镜像并滚动重启。注意：自动更新应避开飞行/实验期间。

4. **网络降级策略**：WiFi 断开时，NetworkManager 自动切换至 4G 热点（通过 `nmcli con add` 预配置备用连接）。边缘节点检测到 ROS Master 不可达时进入「本地缓存模式」：将 Observation/RobotState 写入本地 SQLite，周期性重试 TCP 连接。此功能为 Phase 2 规划。

5. **多架构构建**：树莓派 ARM64 镜像构建需 Docker buildx + qemu-user-static 模拟。CI 中增加非阻塞 `--platform linux/arm64` 构建验证（参考 task-13）。

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
