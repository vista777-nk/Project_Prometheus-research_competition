# PLATFORM.md — 平台架构（稳定文档）

> **本文档很少变动。** 定义系统的骨架：架构分层、接口契约、包结构、通信拓扑、部署方式。  
> 研究路线请参见 [ROADMAP.md](./ROADMAP.md)。  
> 接口细节请参见 [ICD.md](./ICD.md)。

---

## 一、硬件环境

| 项目 | 当前 (仿真) | 目标 (实机) |
|------|------------|------------|
| 无人机飞控 | PX4 SITL v1.14 | Pixhawk 6C + PM07 + M9N |
| 无人机机载 | 树莓派5 (模拟) | 树莓派5（ARM64、8 GB、Debian 13、64 GB microSD 基线） |
| 无人机传感器 | 深度相机 + GPS + IMU (Gazebo) | D435i CB 或双 Pi Camera；M9N；Pixhawk IMU |
| 车机主控 | 树莓派5 (模拟) | 一套共享 Pi 5 载荷；麦轮 STM32F407VET6 / 差速 MSPM0G3507 控制模块 |
| 车机传感器 | OpenMV + 2D LiDAR + 4×超声波 + IMU (Gazebo) | OpenMV + RPLIDAR A2M12 + ICM42688（共享）；每底盘 4×HC-SR04（MCU 管理） |
| 实验室服务器 | 本地 localhost (模拟) | 实验室 GPU 服务器 |
| 底盘 A | 差速 (Gazebo) | R5 标准板尺寸 + 65 mm 轮 + MC520P30 ×2 + DRV8871 ×2 |
| 底盘 B | 麦轮 (Gazebo) | R5 标准板尺寸 + 80 mm 麦轮 + MC520P30 ×4 + DRV8871 ×4 |
| 无人机本体 | iris (Gazebo) | F450 延长脚架 + A2212 980KV ×4 + BL32 30A ×4 + 1045 桨/护罩 |
| 飞控遥测 | UDP localhost | 915 MHz/500 mW：空中端接 TELEM1，地面端接地面站 |
| 车服通信 | TCP localhost (模拟 WiFi) | WiFi / 4G |

---

## 二、软件选型

| 组件 | 选择 | 理由 |
|------|------|------|
| ROS 发行版 | **Noetic** (Python 3.8+) | Ubuntu 20.04 原生支持 |
| Gazebo | **Gazebo Classic 11** | Focal/Noetic 官方源实际提供版本，与 PX4 SITL 兼容 |
| 无人机仿真 | **PX4-Autopilot v1.14** SITL | 与 Pixhawk 6C 固件一致 |
| 车机仿真 | `ros_control` + `diff_drive_controller` + 自定义麦轮控制器 | 轻量、可控 |
| 通信协议 | MAVLink (via MAVROS)；TCP JSON (edge↔server) | Pixhawk 原生 + 自定义抽象层 |
| 传感器仿真 | Gazebo ROS plugins | 标准 ROS-Gazebo 桥接 |
| 消息定义 | package `air_ground_interfaces` | 遵循 ICD 规范 |

---

> 实机 Pi 的 Debian 13 是**容器宿主机**，不裸装 Noetic。Noetic 继续封装在
> Ubuntu 20.04/Focal ARM64 镜像中。滚动硬件记录与换卡预检见
> [`../docs/experiments/phase-1.5-hardware-baseline.md`](../docs/experiments/phase-1.5-hardware-baseline.md)。

## 三、架构总览（能力分层视角）

```
┌──────────────────────────────────────────────────────────────┐
│  Layer 4: Research (研究层)          [air_ground_lab_server]            │
│  VLM · EQA · SLAM · World Model · Planner · Coordinator      │
│  只依赖 Layer 3 抽象接口，永不引用 MAVLink / PX4 / STM32       │
├──────────────────────────────────────────────────────────────┤
│  Layer 3: Abstraction (抽象接口层)    [air_ground_interfaces] │
│  Observation · RobotState · WorldState · Mission · Capability │
│  详见 ICD.md                                                  │
├──────────────────────────────────────────────────────────────┤
│  Layer 2: Bridge (协议翻译层)        [air_ground_com_bridge]            │
│  drone_car_bridge:    MAVLink UDP ↔ ROS topics               │
│  edge_server_bridge:  ROS topics ↔ TCP JSON (Thrift-like)    │
│  边缘预处理器:           传感器原始数据 ↔ Observation          │
├──────────────────────────────────────────────────────────────┤
│  Layer 1: Hardware (硬件层)                                   │
│  Pixhawk 6C · STM32F407 · MSPM0G3507                         │
│  Depth Camera · LiDAR · OpenMV · IMU · Motor · Servo         │
└──────────────────────────────────────────────────────────────┘
```

### 物理部署映射

```
中关村服务器 (Layer 4)
    ↕ 受控隧道中的单一 TCP 会话
良乡共享车载 Pi (Layer 2)                  良乡无人机 Pi (Layer 2)
    ↕ /dev/mcu（同一协议）                     ↕ TELEM2 / MAVROS
MSPM0 差速模块 或 STM32 麦轮模块             Pixhawk 6C ──TELEM1── 915 MHz 地面站
    ↕                                        ↕
电机/编码器/IA6B/HC-SR04                    IA6B/PM07/M9N/ESC；可换视觉载荷
```

共享车载 Pi、ICM42688、A2M12 和云台/OpenMV 是一套物理载荷；它一次只能装在一个
底盘上。`SwapChassis` 是换装后的软件选择/身份核对接口，不代表两个底盘同时在线或
存在自动机械切换机构。硬件所有权和未定参数见 ADR-0018。

---

## 四、ROS 包结构

```
~/air_ground_sim_ws/src/
├── air_ground_interfaces/       # 自定义消息和服务 (ICD 规范)
│   ├── msg/
│   │   ├── Observation.msg
│   │   ├── RobotState.msg
│   │   ├── WorldState.msg
│   │   ├── SemanticLandmark.msg
│   │   ├── Mission.msg
│   │   ├── MissionStatus.msg
│   │   ├── Capability.msg
│   │   └── SensorFusion.msg / ChassisState.msg / ServerCommand.msg  # 过渡别名，待删除 (ICD §八)
│   ├── srv/
│   │   ├── SwapChassis.srv
│   │   └── QueryWorldState.srv
│   └── action/
│       └── Navigate.action
│
├── air_ground_drone_bringup/               # 无人机启动与配置
│   ├── launch/
│   │   ├── drone_sitl.launch    # PX4 SITL + Gazebo
│   │   ├── drone_sensors.launch # 传感器话题适配
│   │   ├── drone_edge.launch    # 边缘预处理节点（Task-07）
│   │   ├── drone-edge-real.launch      # MAVROS + 真机视觉 + 预处理
│   │   └── drone-vision-d435i.launch   # D435i 可换载荷
│   ├── config/
│   │   ├── drone_sensors.yaml
│   │   ├── drone_edge.yaml         # 边缘节点配置（Task-07）
│   │   └── mavros_signing.yaml     # MAVLink 签名参数模板（task-14，签名段待实机核实）
│   ├── scripts/
│   │   ├── drone_preprocessor.py   # 传感器→Observation（Task-07）
│   │   ├── gps_converter.py        # NavSatFix→本地ENU
│   │   └── test_drone.sh           # Task-02 无头验收
│   └── test/
│       ├── test_gps_converter.py
│       └── test_drone_preprocessor.py
│
├── air_ground_car_bringup/                 # 车机启动与配置
│   ├── launch/
│   │   ├── car_diff.launch
│   │   ├── car_mecanum.launch
│   │   ├── car_edge.launch
│   │   └── car_edge_real.launch  # 实机模式（默认 real；mock 必须显式选择）
│   ├── config/
│   │   ├── car_sensors.yaml
│   │   ├── car_edge.yaml           # 边缘节点配置（Task-07）
│   │   ├── real_sensors.yaml       # 实机传感器参数（task-14）
│   │   ├── chassis_params.yaml
│   │   ├── diff_chassis_control.yaml
│   │   └── mecanum_chassis_control.yaml
│   ├── scripts/
│   │   ├── car_preprocessor.py     # 传感器→Observation
│   │   ├── mecanum_controller.py   # 麦轮逆运动学
│   │   ├── chassis_swapper.py      # 底盘热切换
│   │   ├── gimbal_controller.py    # 云台控制
│   │   ├── rplidar_driver.py       # 实机驱动骨架（task-14）
│   │   ├── icm42688_driver.py      #   同上
│   │   ├── chassis_protocol.py     # MCU 协议/CRC/帧解析
│   │   ├── chassis_bridge.py       # /cmd_vel + 遥测/超声波桥
│   │   ├── openmv_bridge.py        #   同上
│   │   ├── hardware_interface.py   # 硬件后端抽象（task-14）
│   │   ├── mock_hardware.py        # mock 后端（task-14）
│   │   └── sensor_config.py        # 传感器配置加载（task-14）
│   ├── urdf/
│   │   ├── car_base.urdf.xacro
│   │   ├── car_sensors.urdf.xacro
│   │   ├── diff_chassis.urdf.xacro
│   │   └── mecanum_chassis.urdf.xacro
│   └── test/
│       ├── test_*.py               # 工作空间单元测试
│       └── host/                   # 无 ROS 环境的 Host 测试（ros_stub.py，task-14/15）
│
├── air_ground_com_bridge/                  # 空地通信桥 (Layer 2)
│   ├── launch/
│   │   └── air_ground_com_bridge.launch
│   ├── config/
│   │   ├── network.yaml             # 仿真回环
│   │   ├── network_lab.yaml         # 同一受控实验室网
│   │   └── network_server.yaml      # 跨校区服务器回环默认
│   ├── scripts/
│   │   ├── drone_car_bridge.py     # MAVLink ↔ ROS
│   │   └── edge_server_bridge.py   # ROS ↔ TCP JSON
│   └── test/                       # 桥单元测试 + 运行时对端
│
├── air_ground_bringup/                     # 顶层集成启动 (Task-08)
│   ├── launch/
│   │   ├── air_ground_sim.launch
│   │   ├── drone_only.launch
│   │   ├── car_only.launch
│   │   ├── server_only.launch
│   │   └── lab-server-real.launch
│   └── package.xml
│
└── air_ground_lab_server/                  # 实验室服务器 (Layer 2~3)
    ├── launch/
    │   └── server.launch
    ├── config/
    │   └── server_params.yaml
    ├── scripts/
    │   ├── tcp_receiver.py         # TCP → ROS
    │   ├── world_model.py          # World Model (核心)
    │   ├── slam_node.py            # SLAM (占位)
    │   ├── eqa_engine.py           # EQA 推理 (占位)
    │   └── coordinator.py          # 空地协同
    └── test/                       # 服务器组件单元测试
```

> 注：`src/` 下另有 `firmware/`（STM32/MSPM0 下位机固件，非 ROS）与
> `deployment/`（树莓派部署 + 服务器 systemd + 标定工具链，非 ROS），结构见各自 README。

---

## 五、话题命名规范

遵循 ICD §五：

| 话题 | 方向 | 频率 |
|------|------|------|
| `/<robot_id>/observation` | Edge → Server | ≤ 10 Hz |
| `/<robot_id>/state` | Edge → Server | ≤ 20 Hz |
| `/server/world_state` | World Model → 所有节点 | 按需 |
| `/<robot_id>/mission` | Server → Edge | 事件驱动 |
| `/<robot_id>/mission_status` | Edge → Server | ≤ 2 Hz |
| `/<robot_id>/capability` | Edge → Server | 注册时 |

> Layer 1 原始话题（`/car/scan`、`/mavros/...`）不在接口稳定承诺范围内。

---

## 六、通信拓扑

```
无人机 (PX4)
  MAVLink UDP :18570 (SITL 实测) ──────┐
                                        ▼
                          drone_car_bridge (车机上)
                            ↓ ROS topics (/drone/*)
                          edge_server_bridge (车机上)
                            ↓ TCP JSON :9090
                          tcp_receiver (服务器)
                            ↓ ROS topics (/server/*)
                          World Model → EQA / Planner / Coordinator
```

---

## 七、部署方式

### 仿真（当前）

```bash
# 单机 localhost，所有节点共存
make launch-full    chassis:=diff    gui:=false    headless:=true
```

### 实机（未来）

```bash
# 无人机树莓派
roslaunch air_ground_drone_bringup drone_edge.launch

# 车机树莓派
roslaunch air_ground_car_bringup car_edge.launch chassis:=diff
roslaunch air_ground_com_bridge air_ground_com_bridge.launch

# 中关村实验室服务器（已安装为 Linger 用户服务）
systemctl --user status air-ground-lab-server.service
python3 src/deployment/server/check_lab_server.py --json
```

---

## 九、TF 树管理

> （P3-01 补全）

跨机器人的坐标系是实机部署的核心挑战。必须定义统一的 TF 树，避免"仿真能跑，实机崩"。

### 标准 TF 层级

```
map                    ← 全局固定坐标系 (GPS/地标融合)
  └── odom             ← 里程计漂移补偿
        └── base_link  ← 机器人本体
              ├── sensor_mount
              │     ├── lidar_link
              │     ├── gimbal_pan_link → gimbal_tilt_link → openmv_camera_link
              │     └── ultrasonic_*_link
              └── depth_camera_link  ← 无人机深度相机
```

### 多机器人约定

- 每台机器人有独立的 `odom` frame（prefixed: `drone/odom`, `car/odom`）
- 所有机器人共享一个全局 `map` frame
- `map` 的原点和方向由以下之一确定：
  - 仿真：Gazebo 原点
  - 实机：RTK-GPS 基准站 或 AprilTag 地面标记

### 实机部署的 TF 发布

| 节点 | 发布的 transform |
|------|-----------------|
| MAVROS (无人机) | `map → drone/odom → drone/base_link` |
| robot_localization (车机) | `map → car/odom → car/base_link` |
| AprilTag 检测 (服务器) | 校正 `drone/base_link → map` 漂移 |

---

## 十、时钟同步方案

> （P3-02 补全）

仿真中所有节点共享同一 ROS 主机，`sim_time` 自动同步。**实机上三台设备（无人机树莓派、车机树莓派、实验室服务器）各有时钟**，不同步会导致 TF 查询失败。

### 建议方案

| 环境 | 方案 | 精度 |
|------|------|:---:|
| 仿真 | `use_sim_time:=true`（rosmaster 统一管理） | 毫秒 |
| 良乡实机局域网 | chrony（NTP），车机作为 NTP server、无人机 sync | 毫秒 |
| 中关村服务器 | 校园/公共 NTP；与良乡设备另做同参考源偏差实测 | 毫秒（待实测） |
| 实机户外 | PTP（IEEE 1588），需要硬件时间戳支持 | 微秒 |

### 验证方法

```bash
# 检查各机器与服务器的时钟偏差
ssh drone-pi  "chronyc tracking | grep 'System time'"
ssh car-pi    "chronyc tracking | grep 'System time'"
timedatectl status   # 中关村服务器；还需记录三机相对同一参考源的偏差
# 偏差应 < 10ms
```

---

## 十一、已知架构债务

> （P2-07 记录）本清单来自 2026-07-25 subagent 集群执行前审阅，记录已知但暂不修复的架构问题。

| # | 问题 | 状态 | 计划修复 |
|---|------|:---:|---------|
| AD-01 | 双 Gazebo 部署（资源翻倍 + 服务冲突） | ✅ | Task-08 已改为 PX4 Launch 独占 Gazebo，车模型注入同一世界 |
| AD-02 | 仿真-实机 MAVLink 桥断层（P1-11） | ⚠️ | 实机部署阶段重写 `drone_car_bridge` |
| AD-03 | 低带宽遥测电台不适合图像 | ✅ 边界已定 | 电台只接 Pixhawk TELEM1；图像走受控 IP/TCP |
| AD-04 | TCP JSON 传输不适合生产环境图像流 | ⚠️ | v2 迁移到 ROS2/DDS 或 ZeroMQ |
| AD-05 | Coordinator 既做决策又做翻译（违反分层） | ⚠️ | 引入 Edge 端 `mission_executor` 节点 |
| AD-06 | World Model 可能成为单点性能瓶颈 | ⚠️ | 高频数据走 topic bus，低频查询走 service |
| AD-07 | World State 暂无 TF 广播 (只发布 topic 不发布 transform) | ⚠️ | 与 §九 TF 树一起规划 |
| AD-08 | 麦轮 low-friction 近似：Gazebo 不仿真辊子物理 | ℹ️ 设计取舍 | 仿真仅验证控制逻辑；横向运动精度以实机为准 |
| AD-09 | 仿真底盘检测依赖 `rostopic list` | ✅ 实机隔离 | 实机由 `/dev/mcu` PONG 的 board/chassis 身份失败关闭 |
| AD-10 | PX4 v1.14 无 `iris_depth_camera` 专用 airframe | ℹ️ 设计约束 | 使用官方 Iris airframe，并以完整路径覆盖深度相机 SDF |
| AD-11 | GPS HOME 默认值固定在仿真配置中 (P3-06) | ✅ 隔离 | HOME 只供 SITL 转换；实机直接转发 Pixhawk EKF 的 MAVROS local pose |
| AD-12 | 缺少 CI/CD、性能监控、代码风格强制 | ✅ 部分 | task-13 已建 CI（7 job，含全仓 lint 门禁）；性能监控仍待引入 |

---

*版本: v6.3 · 日期: 2026-08-02 · 与 ICD.md、ADR-0018 配套；本版同步确认 BOM、五个物理模块及实机接口所有权*
