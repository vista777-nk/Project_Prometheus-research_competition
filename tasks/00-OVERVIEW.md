# 空地联合具身问答系统 —— 仿真孪生 ROS 框架

## 硬件环境

- **笔记本**：华为轻薄本，双系统 Ubuntu 20.04.6
- **算力限制**：无独立 GPU，Gazebo 物理仿真需关闭 GUI 或使用 headless 模式
- **内存**：建议预留 8GB+ 空闲内存用于 PX4 SITL + Gazebo

## 软件选型

| 组件 | 选择 | 理由 |
|------|------|------|
| ROS 发行版 | **Noetic** (Python 3) | Ubuntu 20.04 原生支持 |
| Gazebo | **Gazebo 9**（ROS Noetic 默认） | 与 PX4 SITL 兼容最好 |
| 无人机仿真 | **PX4-Autopilot v1.14** SITL | 与真实 Pixhawk 6C 固件一致 |
| 车机仿真 | ROS 原生 `diff_drive_controller` + 自定义麦轮插件 | 轻量、可控 |
| 通信协议 | MAVLink (via MAVROS) | Pixhawk 原生协议 |
| 传感器仿真 | Gazebo 插件 (`libgazebo_ros_camera`, `libgazebo_ros_laser`, `libgazebo_ros_depth_camera` 等) | 标准 ROS-Gazebo 桥接 |
| 自定义消息 | package `air_ground_interfaces` | 空地协同专用消息 |

## 架构总览

```
                        ┌─────────────────────────┐
                        │     lab_server           │
                        │  (VLM / SLAM / EQA /    │
                        │   World Model placeholder)│
                        └─────┬──────┬────────────┘
                              │      │
                     WiFi/TCP │      │ WiFi/TCP
                              │      │
    ┌─────────────────────────┴─┐  ┌─┴──────────────────────────┐
    │    drone_edge             │  │    car_edge                │
    │  (数据预处理+中继)        │  │  (数据预处理+中继)         │
    │                          │  │                            │
    │  [depth_camera] ─────┐   │  │  [laser_2d] ──────────┐   │
    │  [gps] ─────────────┤   │  │  [openmv_camera] ────┤   │
    │  [imu] ─────────────┤   │  │  [ultrasonic x4] ────┤   │
    │                      ▼   │  │  [imu_icm42688] ────┤   │
    │              preprocessor │  │                      ▼   │
    │                     │     │  │              preprocessor │
    │                     ▼     │  │                     │     │
    │              mavlink_tx   │  │              tcp_tx │     │
    └───────────────────────────┘  └────────────────────────────┘
              │        ▲                          │        ▲
    MAVLink   │        │                 MAVLink  │        │
    (3DR数传) │        │                 (3DR数传) │        │
              ▼        │                          ▼        │
    ┌──────────────────┴────┐    ┌──────────────────┴────────┐
    │   px4_sitl            │    │  chassis_controller       │
    │  (飞控固件模拟)        │    │  (STM32 模拟)              │
    │                       │    │                           │
    │  [mixer] → [motors]   │    │  ┌─── diff_chassis       │
    │  [estimator]          │    │  ├─── mecanum_chassis    │
    └───────────────────────┘    │  └─── modular_swap       │
                                 └──────────────────────────┘
```

## 代码目录结构

```
~/air_ground_sim_ws/src/
├── air_ground_interfaces/       # 自定义 ROS 消息和服务
│   ├── msg/
│   │   ├── SensorFusion.msg     # edge→server 融合传感器数据
│   │   ├── ServerCommand.msg    # server→edge 控制指令
│   │   └── ChassisState.msg     # 底盘状态反馈
│   └── srv/
│       └── SwapChassis.srv      # 模块化底盘切换请求
│
├── drone_bringup/               # 无人机启动与配置
│   ├── launch/
│   │   ├── drone_sitl.launch    # PX4 SITL + Gazebo 无人机
│   │   └── drone_edge.launch    # 边缘预处理节点
│   ├── config/
│   │   └── drone_sensors.yaml   # 深度相机/GPS/IMU 参数
│   └── scripts/
│       └── drone_preprocessor.py
│
├── car_bringup/                 # 车机启动与配置
│   ├── launch/
│   │   ├── car_diff.launch      # 差速底盘
│   │   ├── car_mecanum.launch   # 麦轮底盘
│   │   └── car_edge.launch      # 边缘预处理节点
│   ├── config/
│   │   ├── car_sensors.yaml
│   │   └── chassis_params.yaml
│   ├── scripts/
│   │   ├── car_preprocessor.py
│   │   ├── mecanum_controller.py
│   │   └── chassis_swapper.py
│   └── urdf/
│       ├── car_base.urdf.xacro  # 车机本体（不含底盘）
│       ├── diff_chassis.urdf.xacro
│       └── mecanum_chassis.urdf.xacro
│
├── com_bridge/                  # 空地通信桥
│   ├── launch/
│   │   └── com_bridge.launch
│   ├── scripts/
│   │   ├── drone_car_bridge.py  # 无人机↔车 MAVLink 桥
│   │   └── edge_server_bridge.py # 边缘↔服务器 TCP 桥
│   └── config/
│       └── network.yaml
│
└── lab_server/                  # 实验室服务器节点
    ├── launch/
    │   └── server.launch
    ├── scripts/
    │   ├── slam_node.py         # SLAM 占位
    │   ├── eqa_engine.py        # EQA 推理占位
    │   └── coordinator.py       # 空地协同策略占位
    └── config/
        └── server_params.yaml
```

## 任务分发计划

以下 9 个任务按依赖顺序排列，每个任务是独立的、可交由 subagent 执行的单元：

| 任务 | 内容 | 依赖 | 预计耗时 |
|------|------|------|---------|
| [task-01](./task-01-env-setup.md) | 环境搭建 + ROS 工作空间脚手架 | 无 | 1h |
| [task-02](./task-02-drone-sitl.md) | PX4 SITL 无人机仿真 + 传感器插件 | task-01 | 2h |
| [task-03](./task-03-diff-chassis.md) | 差速底盘仿真（电赛规格） | task-01 | 2h |
| [task-04](./task-04-mecanum-chassis.md) | 麦轮底盘仿真 + 模块化切换 | task-03 | 2h |
| [task-05](./task-05-sensors.md) | 传感器插件完整配置 | task-02, task-03 | 1.5h |
| [task-06](./task-06-com-bridge.md) | 空地通信桥（MAVLink + TCP） | task-02, task-03 | 2h |
| [task-07](./task-07-edge-server.md) | 边缘预处理 + 服务器节点 | task-01, task-06 | 2h |
| [task-08](./task-08-integration.md) | 集成总装 Launch + 一键启动 | task-02~07 | 1.5h |
| [task-09](./task-09-validation.md) | 仿真验证 + 测试脚本 | task-08 | 1h |

## 给 subagent 的通用规范

1. **代码风格**：Python 3.8+，遵循 PEP 8；ROS 节点使用 `rospy`
2. **注释语言**：英文（变量名）+ 中文（模块 docstring）
3. **容错**：所有节点必须处理 ROS 话题未连接、传感器无数据的情况，打印 warning 而非 crash
4. **轻量化**：Gazebo 仿真运行时 `roslaunch` 应避免不必要的高频话题（如原始点云全量发布），默认频率不超过 30Hz
5. **可配置**：所有关键参数从 `config/*.yaml` 读取，不硬编码
6. **生产就绪**：每个 task 完成后必须包含一个 `test_xxx.sh` 验证脚本
