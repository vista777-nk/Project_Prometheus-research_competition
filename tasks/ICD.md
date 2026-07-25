# Interface Control Document (ICD) v1.0

> **稳定接口定义。**  
> 硬件可换，算法可换，这些接口应尽量保持稳定。  
> 所有接口基于 ROS 消息 / 服务实现，但与具体机器人协议 (MAVLink / PX4 / STM32 串口) 解耦。

---

## 一、架构分层

```
┌─────────────────────────────────────────────┐
│  Layer 4: Research (研究层)                 │
│  VLM / EQA / World Model / Planner          │
│  只依赖 Layer 3 的抽象接口，                   │
│  永远不引用任何机器人协议                       │
├─────────────────────────────────────────────┤
│  Layer 3: Abstraction (抽象接口层) ← 本 ICD  │
│  Observation / RobotState / WorldState      │
│  Task / Mission / Capability                │
├─────────────────────────────────────────────┤
│  Layer 2: Bridge (协议翻译层)                │
│  MAVLink ↔ Observation                      │
│  ROS topics ↔ RobotState                    │
│  JSON/TCP ↔ 抽象消息                         │
├─────────────────────────────────────────────┤
│  Layer 1: Hardware (硬件层)                  │
│  Pixhawk / STM32 / LiDAR / Camera / Motor   │
└─────────────────────────────────────────────┘
```

**铁律**：Layer 4 永远不允许 import `mavros`、`gazebo_msgs`、`sensor_msgs/LaserScan` 等硬件相关包。它只依赖 `air_ground_interfaces` 中定义的抽象消息。

---

## 二、核心数据接口

### 2.1 Observation（传感器观测）

**方向**：Edge → Server  
**频率**：≤ 10 Hz (可降采样)  
**ROS 类型**：`air_ground_interfaces/Observation`

```
# Edge node → Server: 一次多模态观测
Header header
string robot_id            # "drone" | "car"
string[] modalities        # ["rgb", "depth", "lidar_2d", "ultrasonic", "imu"]
                           # 标记本次携带哪些模态数据

# 图像 (可选)
sensor_msgs/CompressedImage rgb
sensor_msgs/Image depth

# 激光雷达 (可选，已降采样)
float32[] lidar_ranges
float32 lidar_angle_min
float32 lidar_angle_increment

# 超声波 (可选)
float32[] ultrasonic_ranges # [front, rear, left, right]

# IMU (可选)
geometry_msgs/Vector3 angular_velocity
geometry_msgs/Vector3 linear_acceleration
```

> **设计意图**：Observation 是**多模态稀疏快照**，不是传感器原始流。  
> Edge 端负责降采样、压缩、过滤，Server 端收到的应是可直接消费的结构化数据。

### 2.2 RobotState（机器人自身状态）

**方向**：Edge → Server  
**频率**：≤ 20 Hz  
**ROS 类型**：`air_ground_interfaces/RobotState`

```
# Edge → Server: 机器人自身状态快照
Header header
string robot_id

# 位姿 (world frame)
geometry_msgs/Pose pose
geometry_msgs/Twist velocity

# 模式
string mode                 # "idle" | "navigating" | "exploring" | "emergency"
string chassis_type         # "diff" | "mecanum" | "none"

# 健康
float32 battery_voltage
bool is_armed               # 无人机：是否解锁
bool is_connected
```

> **设计意图**：RobotState 是**机器人自身**的描述，不包含环境信息。  
> 环境信息走 Observation 或 WorldState。

### 2.3 WorldState（世界状态）

**方向**：Server 内部（World Model 维护）  
**频率**：按需  
**ROS 类型**：`air_ground_interfaces/WorldState`

```
# Server 内部: World Model 维护的全局状态
# 这是系统的"认知中心"
Header header

# 所有已知智能体的最新状态
RobotState[] agents

# 静态地图
nav_msgs/OccupancyGrid map_2d

# 动态障碍物
geometry_msgs/Pose[] dynamic_obstacles

# 语义地标 (EQA 相关)
SemanticLandmark[] landmarks

# 时间戳 (用于判断信息新鲜度)
time last_update_perception
time last_update_planning
```

> **设计意图**：WorldState 是 World Model 的**输出**。  
> 系统中的所有决策节点只问 WorldState，不问传感器。

### 2.4 SemanticLandmark（语义地标）

**ROS 类型**：`air_ground_interfaces/SemanticLandmark`

```
# 语义地标 (EQA 查询目标)
string landmark_id
string semantic_label        # "red_ball", "door", "table", "charging_station"
geometry_msgs/Pose pose
float32 confidence           # 0.0 ~ 1.0
time last_observed
```

---

## 三、核心控制接口

### 3.1 Task / Mission（任务指令）

**方向**：Server → Edge  
**频率**：按需（事件驱动）  
**ROS 类型**：`air_ground_interfaces/Mission`

```
# Server → Edge: 高层任务指令
# 注意：不是速度指令，不是航点，是"任务"
Header header
string mission_id            # UUID
string robot_id              # 目标机器人

# 任务类型
string type                  # "navigate" | "search" | "inspect" | "return_home" | "follow" | "explore" | "takeoff" | "land"

# 任务参数 (按 type 解释)
geometry_msgs/Pose target_pose        # navigate/search 的目标
string target_landmark_id             # search 的目标地标
float32 search_radius                 # search 的搜索半径

# 优先级
int32 priority               # 0=lowest, 255=highest

# 关联查询 (EQA)
string query_text             # 原始自然语言查询
string query_id               # 查询 UUID
```

> **设计意图**：Server 下发的是 **"做什么"** (What)，不是 **"怎么做"** (How)。  
> "怎么做"由 Edge 上的路径规划器和控制器决定。

### 3.2 MissionStatus（任务状态反馈）

**方向**：Edge → Server  
**频率**：≤ 2 Hz 或状态变化时  
**ROS 类型**：`air_ground_interfaces/MissionStatus`

```
# Edge → Server: 任务执行状态
Header header
string mission_id
string status                # "accepted" | "executing" | "completed" | "failed" | "aborted"
string failure_reason        # 失败原因 (如 "obstacle_blocking", "out_of_range")
float32 progress             # 0.0 ~ 1.0
```

### 3.3 Capability（机器人能力自描述）

**方向**：Edge → Server（注册时发送一次，能力变化时更新）  
**频率**：按需  
**ROS 类型**：`air_ground_interfaces/Capability`

```
# Edge → Server: 机器人能力声明
Header header
string robot_id

# 运动能力
string locomotion_type       # "aerial" | "ground_wheeled" | "ground_tracked"
float32 max_speed            # m/s
float32 max_endurance        # 秒 (续航)

# 感知能力
string[] sensor_modalities   # ["rgb", "depth", "lidar_2d", "ultrasonic", "gps"]
float32 sensor_range         # 有效感知距离 (m)

# 计算能力
string compute_tier          # "edge_low" | "edge_mid" | "edge_high" | "server"

# 负载能力
float32 max_payload_kg
bool has_gripper
```

> **设计意图**：Server 可以不知道你是什么机器人，但知道你**能做什么**。  
> 新增一种机器人只需实现 Capability → 服务器自动适配。

---

## 四、服务接口

### 4.1 SwapChassis（底盘切换）

**方向**：Server → Edge (Car)  
**ROS 类型**：`air_ground_interfaces/SwapChassis.srv`

```
string target_chassis        # "diff" | "mecanum"
---
bool success
string message
```

### 4.2 QueryWorldState（查询世界状态）

**方向**：任意节点 → World Model  
**ROS 类型**：`air_ground_interfaces/QueryWorldState.srv`

```
string query_type            # "nearest_landmark" | "path_to" | "visibility_from"
string[] args                # 查询参数
---
WorldState result
bool found
```

---

## 五、话题命名规范

```
/<robot_id>/<layer>/<content>

示例:
/drone/observation           — 无人机观测 (Edge 发布)
/car/observation             — 车机观测 (Edge 发布)
/drone/state                 — 无人机 RobotState (Edge 发布)
/car/state                   — 车机 RobotState (Edge 发布)
/drone/mission               — 发送给无人机的任务
/car/mission                 — 发送给车机的任务
/drone/mission_status        — 无人机任务状态
/car/mission_status          — 车机任务状态
/drone/capability            — 无人机能力声明
/car/capability              — 车机能力声明

# 服务器侧话题 (TCP 接收器在服务器端重建):
# 加上 /server/ 前缀以区分来源 (即数据已过 TCP 桥接，非原始边缘发布)
/server/drone/observation    — 服务器侧重建的无人机观测 (来源: TCP)
/server/car/observation      — 服务器侧重建的车机观测 (来源: TCP)
/server/drone/state          — 服务器侧重建的无人机状态
/server/car/state            — 服务器侧重建的车机状态
/server/world_state          — World Model 发布的全局世界状态
/server/world_state/update   — SLAM/EQA 节点 TELL WorldModel 的更新通道

# 硬件相关话题 (Layer 1-2, 不在 ICD 保证范围内)
# 以下为桥接层内部实现细节，可能随硬件更换而变化
/drone/mavros/...            — MAVROS 原始话题 (Layer 2)
/car/scan                    — LiDAR 原始话题 (Layer 2)
/car/cmd_vel                 — 底盘速度命令 (Layer 2)
/drone/heartbeat             — 桥接层临时话题 (非稳定接口)
```

---

## 六、消息包结构

```
air_ground_interfaces/
├── msg/
│   ├── Observation.msg          # 多模态观测
│   ├── RobotState.msg           # 机器人状态
│   ├── WorldState.msg           # 世界状态
│   ├── SemanticLandmark.msg     # 语义地标
│   ├── Mission.msg              # 任务指令
│   ├── MissionStatus.msg        # 任务状态
│   └── Capability.msg           # 能力声明
├── srv/
│   ├── SwapChassis.srv          # 底盘切换
│   └── QueryWorldState.srv      # 世界状态查询
└── action/
    └── Navigate.action          # 导航 Action (长耗时任务)
```

---

## 七、接口稳定性承诺

| 接口 | 稳定性 | 说明 |
|------|:---:|------|
| `Observation` | 🟢 稳定 | 模态字段可追加，不删除已有字段 |
| `RobotState` | 🟢 稳定 | 同上 |
| `WorldState` | 🟡 演进中 | 允许增加新的语义层，结构可能调整 |
| `Mission` | 🟢 稳定 | type 枚举可扩展，核心字段不变 |
| `Capability` | 🟢 稳定 | 新增能力用新字段，保持向后兼容 |
| `SemanticLandmark` | 🟡 演进中 | VLM 能力提升后会丰富字段 |

---

## 八、与现有 task 的映射

| ICD 接口 | 当前 task 中的对应 | 迁移行动 |
|----------|-------------------|---------|
| `Observation` | `SensorFusion.msg` | 重命名 + 重构为模态可选 |
| `RobotState` | 部分在 `ChassisState.msg` | 合并 + 扩展 |
| `WorldState` | 无（新增） | task-07 `slam_node.py` 作为 WM 初始化点 |
| `Mission` | `ServerCommand.msg` | 重命名 + 从命令改为任务语义 |
| `SwapChassis` | `SwapChassis.srv` | 不变 |
| `Capability` | 无（新增） | task-07 `drone_preprocessor.py` / `car_preprocessor.py` 启动时各发一条 |

> **迁移策略**：先新增 `Observation` 等消息定义，保留原 `SensorFusion`/`ServerCommand` 作为别名过渡一个版本，task-09 E2E 测试通过后再删除旧消息。这样不会阻塞当前 9 个 task 的执行。

---

*版本: v5.0 · 日期: 2026-07-25 · 作者: DeepSeek (经 ChatGPT 、 混元3 、 豆包 、 执行端subagent集群审阅后重构)*
