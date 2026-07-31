# Capability Matrix — 系统能力矩阵

> **本文档是架构评审的第一入口。**  
> 定义了系统中每个 Capability（能力）的 Provider、Interface、Consumer 和 Replaceability。  
> 所有新增、修改、替换模块的 Review 都应先检查本矩阵。
>
> *创建依据：ChatGPT 2026-07-28 架构评审建议三*

---

## 使用方式

| 场景 | 操作 |
|------|------|
| 新增算法/模块 | 在本矩阵中找到对应的 Capability 行，确认 Interface 兼容 |
| 替换算法/模块 | 检查 Replaceability 评级，确保新实现满足同一 Interface |
| 架构评审 | 逐行检查是否有未记录的 Capability |
| 新增机器人 | 检查 Capability 声明（`Capability.msg`）是否覆盖新机器人 |

---

## 能力矩阵 v1.0

> **Phase 列图例**：`✅` = 已交付并通过测试；`实机⏳` = 骨架/仿真已验证，实机联调待 Phase 2 Step 1（清单见 [AI_HANDOFF](../experiments/AI_HANDOFF.md) §6）；无标记 = 计划中。

### 运动控制 (Locomotion)

| Capability | Provider | Interface | Consumer | Replaceability | Phase |
|------------|----------|-----------|----------|:---:|:---:|
| **麦轮逆运动学** | `stm32_mecanum` 固件 / `mecanum_controller.py` (仿真) | `vx,vy,ω → 四轮RPM` (内部) → `/car/cmd_vel` | `air_ground_car_bringup` | ★★★★☆ | Phase 1 ✅ · 实机⏳ |
| **差速运动学** | `mspm0_diff` 固件 / `diff_drive_controller` (仿真) | `v,ω → 左右轮RPM` (内部) → `/car/cmd_vel` | `air_ground_car_bringup` | ★★★★☆ | Phase 1 ✅ · 实机⏳ |
| **四轮速度PID** | `common/pid.c` (STM32+MSPM0 共享) | `target_rpm, actual_rpm → pwm_duty` | `stm32_mecanum`, `mspm0_diff` | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **无人机飞控** | PX4 v1.14 (Pixhawk 6C) / PX4 SITL (仿真) | MAVLink / MAVROS → `/mavros/*` | `air_ground_com_bridge` | ★★★☆☆ | Phase 0 ✅ (SITL) · 实机⏳ |
| **底盘热切换** | `chassis_swapper.py` | `/car/swap_chassis` (SwapChassis.srv) | `air_ground_lab_server` → Coordinator | ★★★★☆ | Phase 0 ✅ |

### 感知 (Perception)

| Capability | Provider | Interface | Consumer | Replaceability | Phase |
|------------|----------|-----------|----------|:---:|:---:|
| **2D LiDAR 扫描** | RPLIDAR A1 / Gazebo `libgazebo_ros_laser.so` | `/car/scan` (LaserScan) → `Observation.msg` | `car_preprocessor.py` → World Model | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **RGB 图像** | D435i / OpenMV / Gazebo camera plugin | `sensor_msgs/Image` → `Observation.rgb` | `car_preprocessor.py` / `drone_preprocessor.py` → World Model | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **深度图像** | D435i / Gazebo depth plugin | `sensor_msgs/Image` → `Observation.depth` | `drone_preprocessor.py` → World Model | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **IMU** | ICM42688 / Pixhawk 6C 板载 / Gazebo IMU plugin | `sensor_msgs/Imu` → `Observation` (angular_velocity, linear_acceleration) | `car_preprocessor.py` / `drone_preprocessor.py` → World Model | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **超声波测距** | 4×HC-SR04 / Gazebo ultrasonic plugin | `sensor_msgs/Range×4` → `Observation.ultrasonic_ranges` | `car_preprocessor.py` → World Model | ★★★★★ | Phase 1 ✅ · 实机⏳（时序方案未定，ADR-0013 预留） |
| **GPS 定位** | M8N GPS / Gazebo GPS plugin | `sensor_msgs/NavSatFix` → `RobotState.pose` | `gps_converter.py` → World Model | ★★★★☆ | Phase 1 ✅ · 实机⏳ |
| **OpenMV 目标检测** | OpenMV 云台 / (仿真无对照) | `/car/openmv/detections` (JSON String) → `Observation` | `car_preprocessor.py` → World Model | ★★★☆☆ | Phase 1 ✅ · 实机⏳ |

### 通信 (Communication)

| Capability | Provider | Interface | Consumer | Replaceability | Phase |
|------------|----------|-----------|----------|:---:|:---:|
| **空地 MAVLink 桥** | `drone_car_bridge.py` | MAVLink UDP ↔ ROS topics (`/drone/*`) | `air_ground_com_bridge` | ★★★★☆ | Phase 0 ✅ |
| **车服 TCP 桥** | `edge_server_bridge.py` | ROS topics ↔ TCP JSON (`:9090`) | `tcp_receiver.py` (服务器) | ★★★☆☆ | Phase 0 ✅ |
| **MAVLink 2 签名** | 密钥生成脚本 + PX4 参数 | 32-byte 共享密钥 → 消息签名验证 | Pixhawk 6C ↔ MAVROS ↔ 地面站 | ★★★★☆ | Phase 1 ✅（工具链；参数名实机待核） |

### 认知 (Cognition)

| Capability | Provider | Interface | Consumer | Replaceability | Phase |
|------------|----------|-----------|----------|:---:|:---:|
| **世界状态维护** | `world_model.py` | `WorldState.msg` / `QueryWorldState.srv` | Coordinator, EQA, Planner, SLAM | ★★★★★ | Phase 0 ✅ |
| **SLAM** | `slam_node.py` (占位) / 未来 RTAB-Map | `WorldState.map_2d` | World Model | ★★★★★ | Phase 2+ |
| **EQA 推理** | `eqa_engine.py` (占位) / 未来 VLM | `Mission.msg` | Coordinator | ★★★★★ | Phase 2+ |
| **空地协同** | `coordinator.py` | `Mission.msg` → `/drone/mission`, `/car/mission` | Edge nodes | ★★★★☆ | Phase 0 ✅ |

### 部署与运维 (DevOps)

| Capability | Provider | Interface | Consumer | Replaceability | Phase |
|------------|----------|-----------|----------|:---:|:---:|
| **容器化部署** | `Dockerfile.edge` + `docker-compose.edge.yml` | 环境变量注入 (`AIR_GROUND_ROLE`, `CHASSIS`) | 树莓派5 ×2 | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **开机自启** | `systemd` units | `systemctl enable` | 树莓派5 ×2 | ★★★★☆ | Phase 1 ✅ · 实机⏳ |
| **CI 交叉编译** | GitHub Actions (arm-gcc) | `.bin` artifact | 固件烧录工具 | ★★★★★ | Phase 1 ✅ |
| **标定工具链** | `calibrate-camera.py` + `validate-calibration.py` | YAML 输出 → ROS camera_info | 相机驱动 → Observation | ★★★★★ | Phase 1 ✅ · 实机⏳ |
| **健康检查** | `check_nodes.py` + `healthcheck.timer` | ROS Master XML-RPC 探测 | systemd alert | ★★★★☆ | Phase 1 ✅ · 实机⏳ |

### 传感器→Observation 数据流

| 数据流 | 传感器 | 驱动/仿真 | 预处理 | Observation 字段 | 消费者 |
|--------|--------|----------|--------|-----------------|--------|
| **车机 RGB** | OpenMV / Gazebo | `openmv_bridge.py` / `libgazebo_ros_camera.so` | `car_preprocessor.py` | `Observation.rgb` | World Model → VLM |
| **车机 LiDAR** | RPLIDAR A1 / Gazebo | `rplidar_driver.py` / `libgazebo_ros_laser.so` | `car_preprocessor.py` | `Observation.lidar_ranges` | World Model → SLAM |
| **车机 IMU** | ICM42688 / Gazebo | `icm42688_driver.py` / `libgazebo_ros_imu.so` | `car_preprocessor.py` | `Observation.angular/linear_accel` | World Model |
| **车机超声波** | HC-SR04 ×4 / Gazebo | `hcsr04_driver.py` / `libgazebo_ros_ultrasonic.so` | `car_preprocessor.py` | `Observation.ultrasonic_ranges` | World Model |
| **无人机 RGB+Depth** | D435i / Gazebo | (MAVROS + camera plugin) | `drone_preprocessor.py` | `Observation.rgb`, `Observation.depth` | World Model → VLM |
| **无人机 GPS+IMU** | M8N+Pixhawk / Gazebo | `gps_converter.py` | `drone_preprocessor.py` | `RobotState.pose` | World Model |

---

## Replaceability 评级说明

| 星级 | 含义 | 示例 |
|:---:|------|------|
| ★★★★★ | 替换只需实现同一 Interface，系统其他部分零改动 | 换一个 IMU 芯片 |
| ★★★★☆ | 替换需修改 Adapter，但 Consumer 不变 | 换一个 LiDAR 型号 |
| ★★★☆☆ | 替换可能影响 Bridge 层，但 Research 层不变 | 换一个飞控协议 |
| ★★☆☆☆ | 替换需要改动 ICD 接口定义 | 增加新的传感器模态 |
| ★☆☆☆☆ | 替换需要重构系统架构 | 换 ROS 发行版 |

---

## 当前未覆盖的 Capability（Phase 2+ 计划）

| Capability | 计划 Phase | 所需接口 |
|------------|:---:|---------|
| VLM 视觉推理 | Phase 2 | `Mission.query_text` → `Observation.rgb` → Answer |
| 3D 场景重建 (NeRF/3DGS) | Phase 4 | `Observation.rgb+depth` → 3D Model |
| 动作条件化世界模型 (Dreamer) | Phase 4 | `WorldState + Mission → Predicted WorldState` |
| 多机器人编队 | Phase 3+ | `Capability` 扩展 + 编队 Mission type |
| 4G/5G 远程通信 | Phase 2+ | TCP Bridge 替换或新增 |

---

## 审查记录

| 日期 | 审查者 | 变更 |
|------|--------|------|
| 2026-07-28 | ChatGPT (终审架构师) | 初始创建，覆盖 Phase 0 + Phase 1 全部能力 |
| 2026-08-01 | 接手方 AI 助手 (文档对齐) | Phase 1 各行补交付状态标记与图例；底盘热切换服务名更正为 `/car/swap_chassis` |

---

*版本: v1.1 · 日期: 2026-08-01 · 与 ICD.md 配套使用 · 每次架构变更后更新*
