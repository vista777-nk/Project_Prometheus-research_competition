# PLATFORM.md �?平台架构（稳定文档）

> **本文档很少变动�?* 定义系统的骨架：架构分层、接口契约、包结构、通信拓扑、部署方式�? 
> 研究路线请参�?[ROADMAP.md](./ROADMAP.md)�? 
> 接口细节请参�?[ICD.md](./ICD.md)�?

---

## 一、硬件环�?

| 项目 | 当前 (仿真) | 目标 (实机) |
|------|------------|------------|
| 无人机飞�?| PX4 SITL v1.14 | Pixhawk 6C (铝壳�? |
| 无人机机�?| 树莓�? (模拟) | 树莓�? |
| 无人机传感器 | 深度相机 + GPS + IMU (Gazebo) | Intel RealSense D435i + M8N GPS + Pixhawk 6C 板载 IMU |
| 车机主控 | 树莓�? (模拟) | 树莓�? +  下位机协处理�?麦轮底盘STM32F407VET6；差速底盘MSPM0G3507) |
| 车机传感�?| OpenMV + 思岚RPLIDAR A1 + 4×HC-SR04 + ICM42688 (Gazebo) | 同实�?|
| 实验室服务器 | 本地 localhost (模拟) | 实验�?GPU 服务�?|
| 底盘 A | 差�?(Gazebo) | TI 电赛亚克力底�?+ 520 编码器电�?|
| 底盘 B | 麦轮 (Gazebo) | R5 系列麦轮底板 + 520 电机 |
| 空地通信 | UDP localhost (模拟 3DR 数传) | 3DR SiK 数传电台 |
| 车服通信 | TCP localhost (模拟 WiFi) | WiFi / 4G |

---

## 二、软件选型

| 组件 | 选择 | 理由 |
|------|------|------|
| ROS 发行�?| **Noetic** (Python 3.8+) | Ubuntu 20.04 原生支持 |
| Gazebo | **Gazebo Classic 11** | Focal/Noetic 官方源实际提供版本，�?PX4 SITL 兼容 |
| 无人机仿�?| **PX4-Autopilot v1.14** SITL | �?Pixhawk 6C 固件一�?|
| 车机仿真 | `ros_control` + `diff_drive_controller` + 自定义麦轮控制器 | 轻量、可�?|
| 通信协议 | MAVLink (via MAVROS)；TCP JSON (edge↔server) | Pixhawk 原生 + 自定义抽象层 |
| 传感器仿�?| Gazebo ROS plugins | 标准 ROS-Gazebo 桥接 |
| 消息定义 | package `air_ground_interfaces` | 遵循 ICD 规范 |

---

## 三、架构总览（能力分层视角）

```
┌──────────────────────────────────────────────────────────────�?
�? Layer 4: Research (研究�?          [air_ground_lab_server]            �?
�? VLM · EQA · SLAM · World Model · Planner · Coordinator      �?
�? 只依�?Layer 3 抽象接口，永不引�?MAVLink / PX4 / STM32       �?
├──────────────────────────────────────────────────────────────�?
�? Layer 3: Abstraction (抽象接口�?    [air_ground_interfaces] �?
�? Observation · RobotState · WorldState · Mission · Capability �?
�? 详见 ICD.md                                                  �?
├──────────────────────────────────────────────────────────────�?
�? Layer 2: Bridge (协议翻译�?        [air_ground_com_bridge]            �?
�? drone_car_bridge:    MAVLink UDP �?ROS topics               �?
�? edge_server_bridge:  ROS topics �?TCP JSON (Thrift-like)    �?
�? 边缘预处理器:           传感器原始数�?�?Observation          �?
├──────────────────────────────────────────────────────────────�?
�? Layer 1: Hardware (硬件�?                                   �?
�? Pixhawk 6C · STM32F407 · MSPM0G3507                         �?
�? Depth Camera · LiDAR · OpenMV · IMU · Motor · Servo         �?
└──────────────────────────────────────────────────────────────�?
```

### 物理部署映射

```
实验室服务器 (Layer 4)
    �?TCP
车机边缘树莓�? (Layer 2) ←── MAVLink UDP ──�?无人机边缘树莓派5 (Layer 2)
    �?                                       �?
STM32F407 / MSPM0G3507 (Layer 1)           Pixhawk 6C (Layer 1)
    �?                                       �?
底盘 (差�?�?麦轮)                          电机 / 深度相机 / GPS
```

---

## 四、ROS 包结�?

```
~/air_ground_sim_ws/src/
├── air_ground_interfaces/       # 自定义消息和服务 (ICD 规范)
�?  ├── msg/
�?  �?  ├── Observation.msg
�?  �?  ├── RobotState.msg
�?  �?  ├── WorldState.msg
�?  �?  ├── SemanticLandmark.msg
�?  �?  ├── Mission.msg
�?  �?  ├── MissionStatus.msg
�?  �?  └── Capability.msg
�?  ├── srv/
�?  �?  ├── SwapChassis.srv
�?  �?  └── QueryWorldState.srv
�?  └── action/
�?      └── Navigate.action
�?
├── air_ground_drone_bringup/               # 无人机启动与配置
�?  ├── launch/
�?  �?  ├── drone_sitl.launch    # PX4 SITL + Gazebo
�?  �?  └── drone_edge.launch    # 边缘预处理节�?
�?  ├── config/
�?  �?  └── drone_sensors.yaml
�?  ├── worlds/
�?  �?  └── empty.world
�?  └── scripts/
�?      ├── drone_preprocessor.py   # 传感器→Observation
�?      └── gps_converter.py        # NavSatFix→本地ENU
�?
├── air_ground_car_bringup/                 # 车机启动与配�?
�?  ├── launch/
�?  �?  ├── car_diff.launch
�?  �?  ├── car_mecanum.launch
�?  �?  └── car_edge.launch
�?  ├── config/
�?  �?  ├── car_sensors.yaml
�?  �?  ├── chassis_params.yaml
�?  �?  ├── diff_chassis_control.yaml
�?  �?  └── mecanum_chassis_control.yaml
�?  ├── scripts/
�?  �?  ├── car_preprocessor.py     # 传感器→Observation
�?  �?  ├── mecanum_controller.py   # 麦轮逆运动学
�?  �?  ├── chassis_swapper.py      # 底盘热切�?
�?  �?  └── gimbal_controller.py    # 云台控制
�?  └── urdf/
�?      ├── car_base.urdf.xacro
�?      ├── car_sensors.urdf.xacro
�?      ├── diff_chassis.urdf.xacro
�?      └── mecanum_chassis.urdf.xacro
�?
├── air_ground_com_bridge/                  # 空地通信�?(Layer 2)
�?  ├── launch/
�?  �?  └── air_ground_com_bridge.launch
�?  ├── config/
�?  �?  └── network.yaml
�?  └── scripts/
�?      ├── drone_car_bridge.py     # MAVLink �?ROS
�?      └── edge_server_bridge.py   # ROS �?TCP JSON
�?
└── air_ground_lab_server/                  # 实验室服务器 (Layer 4)
    ├── launch/
    �?  └── server.launch
    ├── config/
    �?  └── server_params.yaml
    └── scripts/
        ├── tcp_receiver.py         # TCP �?ROS
        ├── world_model.py          # World Model (核心)
        ├── slam_node.py            # SLAM
        ├── eqa_engine.py           # EQA 推理
        └── coordinator.py          # 空地协同
```

---

## 五、话题命名规�?

遵循 ICD §五：

| 话题 | 方向 | 频率 |
|------|------|------|
| `/<robot_id>/observation` | Edge �?Server | �?10 Hz |
| `/<robot_id>/state` | Edge �?Server | �?20 Hz |
| `/server/world_state` | World Model �?所有节�?| 按需 |
| `/<robot_id>/mission` | Server �?Edge | 事件驱动 |
| `/<robot_id>/mission_status` | Edge �?Server | �?2 Hz |
| `/<robot_id>/capability` | Edge �?Server | 注册�?|

> Layer 1 原始话题（`/car/scan`、`/mavros/...`）不在接口稳定承诺范围内�?

---

## 六、通信拓扑

```
无人�?(PX4)
  MAVLink UDP :14550 ──────────────────�?
                                        �?
                          drone_car_bridge (车机�?
                            �?ROS topics (/drone/*)
                          edge_server_bridge (车机�?
                            �?TCP JSON :9090
                          tcp_receiver (服务�?
                            �?ROS topics (/server/*)
                          World Model �?EQA / Planner / Coordinator
```

---

## 七、部署方�?

### 仿真（当前）

```bash
# 单机 localhost，所有节点共�?
make launch-full    chassis:=diff    gui:=false    headless:=true
```

### 实机（未来）

```bash
# 无人机树莓派
roslaunch air_ground_drone_bringup drone_edge.launch

# 车机树莓�?
roslaunch air_ground_car_bringup car_edge.launch chassis:=diff
roslaunch air_ground_com_bridge air_ground_com_bridge.launch

# 实验室服务器
roslaunch air_ground_lab_server server.launch
```

---

## 九、TF 树管�?

> （P3-01 补全�?

跨机器人的坐标系是实机部署的核心挑战。必须定义统一�?TF 树，避免"仿真能跑，实机崩"�?

### 标准 TF 层级

```
map                    �?全局固定坐标�?(GPS/地标融合)
  └── odom             �?里程计漂移补�?
        └── base_link  �?机器人本�?
              ├── sensor_mount
              �?    ├── lidar_link
              �?    ├── gimbal_pan_link �?gimbal_tilt_link �?openmv_camera_link
              �?    └── ultrasonic_*_link
              └── depth_camera_link  �?无人机深度相�?
```

### 多机器人约定

- 每台机器人有独立�?`odom` frame（prefixed: `drone/odom`, `car/odom`�?
- 所有机器人共享一个全局 `map` frame
- `map` 的原点和方向由以下之一确定�?
  - 仿真：Gazebo 原点
  - 实机：RTK-GPS 基准�?�?AprilTag 地面标记

### 实机部署�?TF 发布

| 节点 | 发布�?transform |
|------|-----------------|
| MAVROS (无人�? | `map �?drone/odom �?drone/base_link` |
| robot_localization (车机) | `map �?car/odom �?car/base_link` |
| AprilTag 检�?(服务�? | 校正 `drone/base_link �?map` 漂移 |

---

## 十、时钟同步方�?

> （P3-02 补全�?

仿真中所有节点共享同一 ROS 主机，`sim_time` 自动同步�?*实机上三台设备（无人机树莓派、车机树莓派、实验室服务器）各有时钟**，不同步会导�?TF 查询失败�?

### 建议方案

| 环境 | 方案 | 精度 |
|------|------|:---:|
| 仿真 | `use_sim_time:=true`（rosmaster 统一管理�?| 毫秒 |
| 实机局域网 | chrony（NTP），车机作为 NTP server，无人机/服务�?sync | 毫秒 |
| 实机户外 | PTP（IEEE 1588），需要硬件时间戳支持 | 微秒 |

### 验证方法

```bash
# 检查各机器与服务器的时钟偏�?
ssh drone-pi  "chronyc tracking | grep 'System time'"
ssh car-pi    "chronyc tracking | grep 'System time'"
# 偏差�?< 10ms
```

---

## 十一、已知架构债务

> （P2-07 记录）本清单来自 2026-07-25 subagent 集群执行前审阅，记录已知但暂不修复的架构问题�?

| # | 问题 | 状�?| 计划修复 |
|---|------|:---:|---------|
| AD-01 | �?Gazebo 部署（资源翻�?+ 服务冲突�?| ⚠️ | Task-08 实施时改为单世界 |
| AD-02 | 仿真-实机 MAVLink 桥断层（P1-11�?| ⚠️ | 实机部署阶段重写 `drone_car_bridge` |
| AD-03 | EQA/VLM 图像带宽矛盾�?DR 24KB/s vs JPEG 10KB+ | ⚠️ | ROADMAP 已记录；VLM 必须�?WiFi/4G |
| AD-04 | TCP JSON 传输不适合生产环境图像�?| ⚠️ | v2 迁移�?ROS2/DDS �?ZeroMQ |
| AD-05 | Coordinator 既做决策又做翻译（违反分层） | ⚠️ | 引入 Edge �?`mission_executor` 节点 |
| AD-06 | World Model 可能成为单点性能瓶颈 | ⚠️ | 高频数据�?topic bus，低频查询走 service |
| AD-07 | World State 暂无 TF 广播 (只发�?topic 不发�?transform) | ⚠️ | �?§�?TF 树一起规�?|
| AD-08 | 麦轮 low-friction 近似：Gazebo 不仿真辊子物�?| ℹ️ 设计取舍 | 仿真仅验证控制逻辑；横向运动精度以实机为准 |
| AD-09 | 底盘检测逻辑依赖 `rostopic list` 探测 (P2-06) | ℹ️ | 仿真可用；实机改用硬件引脚（MSPM0 GPIO）检�?|
| AD-10 | PX4 SITL airframe ID 4032 尚未�?PX4 官方固件注册 | ℹ️ | 仅影响实机固件烧录，仿真不影�?|
| AD-11 | `setup_all.sh` �?PX4 编译参数需在目标机上验�?| ℹ️ | 首次安装时验证；若失败，改用手动步骤 |
| AD-12 | GPS HOME 坐标硬编�?(P3-06) | ℹ️ | 实机部署时从启动脚本参数读取 |
| AD-13 | 缺少 CI/CD、性能监控、代码风格强�?| ℹ️ | 项目稳定后引�?|

---

*版本: v6.0 · 日期: 2026-07-25 · 作�? DeepSeek (�?ChatGPT、混�?、豆包、执行端subagent集群审阅后重�? · �?ICD.md 配套*
