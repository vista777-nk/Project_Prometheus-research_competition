# PLATFORM.md — 平台架构（稳定文档）

> **本文档很少变动。** 定义系统的骨架：架构分层、接口契约、包结构、通信拓扑、部署方式。  
> 研究路线请参见 [ROADMAP.md](./ROADMAP.md)。  
> 接口细节请参见 [ICD.md](./ICD.md)。

---

## 一、硬件环境

| 项目 | 当前 (仿真) | 目标 (实机) |
|------|------------|------------|
| 无人机飞控 | PX4 SITL v1.14 | Pixhawk 6C (铝壳款) |
| 无人机机载 | 树莓派5 (模拟) | 树莓派5 |
| 无人机传感器 | 深度相机 + GPS + IMU (Gazebo) | Intel RealSense D435i + M8N GPS + 板载 IMU |
| 车机主控 | 树莓派5 (模拟) | 树莓派5 + STM32F407VET6 协处理器 |
| 车机传感器 | OpenMV + RPLIDAR A1 + 4×HC-SR04 + ICM42688 (Gazebo) | 同实物 |
| 实验室服务器 | 本地 localhost (模拟) | 实验室 GPU 服务器 |
| 底盘 A | 差速 (Gazebo) | TI 电赛亚克力底盘 + 520 编码器电机 |
| 底盘 B | 麦轮 (Gazebo) | R5 系列麦轮底板 + 520 电机 |
| 空地通信 | UDP localhost (模拟 3DR 数传) | 3DR SiK 数传电台 |
| 车服通信 | TCP localhost (模拟 WiFi) | WiFi / 4G |

---

## 二、软件选型

| 组件 | 选择 | 理由 |
|------|------|------|
| ROS 发行版 | **Noetic** (Python 3.8+) | Ubuntu 20.04 原生支持 |
| Gazebo | **Gazebo 9** | ROS Noetic 默认，与 PX4 SITL 兼容 |
| 无人机仿真 | **PX4-Autopilot v1.14** SITL | 与 Pixhawk 6C 固件一致 |
| 车机仿真 | `ros_control` + `diff_drive_controller` + 自定义麦轮控制器 | 轻量、可控 |
| 通信协议 | MAVLink (via MAVROS)；TCP JSON (edge↔server) | Pixhawk 原生 + 自定义抽象层 |
| 传感器仿真 | Gazebo ROS plugins | 标准 ROS-Gazebo 桥接 |
| 消息定义 | package `air_ground_interfaces` | 遵循 ICD 规范 |

---

## 三、架构总览（能力分层视角）

```
┌──────────────────────────────────────────────────────────────┐
│  Layer 4: Research (研究层)          [lab_server]            │
│  VLM · EQA · SLAM · World Model · Planner · Coordinator      │
│  只依赖 Layer 3 抽象接口，永不引用 MAVLink / PX4 / STM32       │
├──────────────────────────────────────────────────────────────┤
│  Layer 3: Abstraction (抽象接口层)    [air_ground_interfaces] │
│  Observation · RobotState · WorldState · Mission · Capability │
│  详见 ICD.md                                                  │
├──────────────────────────────────────────────────────────────┤
│  Layer 2: Bridge (协议翻译层)        [com_bridge]            │
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
实验室服务器 (Layer 4)
    ↕ TCP
车机边缘树莓派5 (Layer 2) ←── MAVLink UDP ──→ 无人机边缘树莓派5 (Layer 2)
    ↕                                        ↕
STM32F407 / MSPM0G3507 (Layer 1)           Pixhawk 6C (Layer 1)
    ↕                                        ↕
底盘 (差速 或 麦轮)                          电机 / 深度相机 / GPS
```

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
│   │   └── Capability.msg
│   ├── srv/
│   │   ├── SwapChassis.srv
│   │   └── QueryWorldState.srv
│   └── action/
│       └── Navigate.action
│
├── drone_bringup/               # 无人机启动与配置
│   ├── launch/
│   │   ├── drone_sitl.launch    # PX4 SITL + Gazebo
│   │   └── drone_edge.launch    # 边缘预处理节点
│   ├── config/
│   │   └── drone_sensors.yaml
│   ├── worlds/
│   │   └── empty.world
│   └── scripts/
│       ├── drone_preprocessor.py   # 传感器→Observation
│       └── gps_converter.py        # NavSatFix→本地ENU
│
├── car_bringup/                 # 车机启动与配置
│   ├── launch/
│   │   ├── car_diff.launch
│   │   ├── car_mecanum.launch
│   │   └── car_edge.launch
│   ├── config/
│   │   ├── car_sensors.yaml
│   │   ├── chassis_params.yaml
│   │   ├── diff_chassis_control.yaml
│   │   └── mecanum_chassis_control.yaml
│   ├── scripts/
│   │   ├── car_preprocessor.py     # 传感器→Observation
│   │   ├── mecanum_controller.py   # 麦轮逆运动学
│   │   ├── chassis_swapper.py      # 底盘热切换
│   │   └── gimbal_controller.py    # 云台控制
│   └── urdf/
│       ├── car_base.urdf.xacro
│       ├── car_sensors.urdf.xacro
│       ├── diff_chassis.urdf.xacro
│       └── mecanum_chassis.urdf.xacro
│
├── com_bridge/                  # 空地通信桥 (Layer 2)
│   ├── launch/
│   │   └── com_bridge.launch
│   ├── config/
│   │   └── network.yaml
│   └── scripts/
│       ├── drone_car_bridge.py     # MAVLink ↔ ROS
│       └── edge_server_bridge.py   # ROS ↔ TCP JSON
│
└── lab_server/                  # 实验室服务器 (Layer 4)
    ├── launch/
    │   └── server.launch
    ├── config/
    │   └── server_params.yaml
    └── scripts/
        ├── tcp_receiver.py         # TCP → ROS
        ├── world_model.py          # World Model (核心)
        ├── slam_node.py            # SLAM
        ├── eqa_engine.py           # EQA 推理
        └── coordinator.py          # 空地协同
```

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
  MAVLink UDP :14550 ──────────────────┐
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
roslaunch drone_bringup drone_edge.launch

# 车机树莓派
roslaunch car_bringup car_edge.launch chassis:=diff
roslaunch com_bridge com_bridge.launch

# 实验室服务器
roslaunch lab_server server.launch
```

---

## 八、给贡献者的规范

1. **代码风格**：Python 3.8+，PEP 8；ROS 节点用 `rospy`
2. **注释语言**：英文变量名 + 中文模块 docstring
3. **容错**：传感器断连时 `rospy.logwarn`，不 crash
4. **轻量化**：仿真传感器频率 ≤ 30Hz，默认 headless
5. **可配置**：所有参数从 `config/*.yaml` 读取
6. **分层铁律**：Layer 4 不得 import `mavros` / `sensor_msgs/LaserScan` / `gazebo_msgs`
7. **测试**：每个 package 含 `test_*.sh`；E2E 用 `e2e_test.sh`

---

*版本: v1.0 · 日期: 2026-07-25 · 与 ICD.md 配套*
