# Task-07：边缘预处理与实验室服务器

## 状态

| 项目 | 结果 |
|------|------|
| 依赖 | Task-02～06 |
| 实施状态 | ✅ 已完成 |
| 完成日期 | 2026-07-27 |
| Task-07 自动验收 | `15 passed, 0 failed` |
| 工作空间单元测试 | `56 tests, 0 failures` |
| 回归验收 | Task-02～06 全部通过 |

## 目标与数据流

Task-07 在 Task-06 可靠传输链路之上建立稳定抽象层和服务器认知中心：

```text
无人机原始传感器 ─▶ drone_preprocessor ─▶ Observation/RobotState
                                                   │
车载原始传感器 ───▶ car_preprocessor ────▶ Observation/RobotState
                                                   │
                                  edge_server_bridge / TCP JSON
                                                   │
                                                   ▼
                 tcp_server ─▶ World Model ─▶ SLAM / EQA / Coordinator
```

边缘节点可以依赖硬件消息；服务器研究节点只依赖
`air_ground_interfaces`、`nav_msgs` 和通用 ROS 消息，不导入 MAVROS、
Gazebo 或 `LaserScan`。

## 边缘预处理

### 无人机

节点：
[`drone_preprocessor.py`](../src/air_ground_drone_bringup/scripts/drone_preprocessor.py)

配置：
[`drone_edge.yaml`](../src/air_ground_drone_bringup/config/drone_edge.yaml)

- 聚合 RGB、深度、IMU、Task-02 ENU 位姿和 MAVROS 状态；
- RGB 在边缘端缩放到最大 320 像素宽并以 JPEG 质量 40 压缩；
- 以 10 Hz 发布稀疏 `Observation` 和 `RobotState`；
- 使用 2 秒新鲜度窗口，断流时不沿用过期传感器；
- 锁存无人机 `Capability`；
- 与 Task-06 通过 `/drone/state_owner` 协调状态话题所有权：
  Task-07 运行时由 preprocessor 发布，单独运行 Task-06 时通信桥继续提供兼容状态。

启动：

```bash
roslaunch air_ground_drone_bringup drone_edge.launch
```

### 车机

节点：
[`car_preprocessor.py`](../src/air_ground_car_bringup/scripts/car_preprocessor.py)

配置：
[`car_edge.yaml`](../src/air_ground_car_bringup/config/car_edge.yaml)

- OpenMV RGB 在边缘端压缩为 JPEG；
- LiDAR 默认 4:1 降采样，NaN、Inf 和非正值转换为 `-1.0`；
- 四向超声波固定使用 `[front, rear, left, right]` 顺序；
- 超声波缺失时使用最大量程 4 m，避免被误认为近距离障碍；
- 从 `/car/current_chassis` 获取 Task-04 动态切换后的底盘类型；
- 里程计与传感器断流会反映在 `RobotState.is_connected`；
- 锁存车体 `Capability`。

启动：

```bash
roslaunch air_ground_car_bringup car_edge.launch default_chassis:=diff
```

## Task-06 抽象遥测扩展

[`edge_server_bridge.py`](../src/air_ground_com_bridge/scripts/edge_server_bridge.py)
保留 Task-06 原始车载遥测协议，同时增加：

- 订阅无人机 `Observation`、`RobotState` 和车体 `RobotState`；
- 车与无人机遥测轮询发送，单个 TCP 连接即可重建两个 agent；
- 无人机 RGB 继续遵守最大 2 Hz 图像频率和 24 KB/s 总带宽；
- 将 mode、chassis、battery、armed 和 connected 状态写入 JSON；
- Task-06 原有长度帧、重连、命令下行和验收协议保持兼容。

## TCP 接收服务器

节点：
[`tcp_receiver.py`](../src/air_ground_lab_server/scripts/tcp_receiver.py)

- 复用 Task-06 `network.yaml` 中的监听 IP、9090 端口、超时和 10 MiB 上限；
- 完整读取四字节网络序长度头，支持任意 TCP 分片；
- 限制并发客户端数、LiDAR 数组长度和最大 payload；
- 拒绝未知协议版本、未知 agent、非法 UTF-8、非法 JSON、NaN/Inf 和伪 JPEG；
- 单个恶意或损坏连接只会被断开，不影响监听线程和其他客户端；
- 重建 `/server/{car,drone}/{observation,state}`；
- 兼容 Task-06 车机帧中的 `drone_pose` 最小状态。

服务端参数集中在
[`server_params.yaml`](../src/air_ground_lab_server/config/server_params.yaml)。

## World Model

节点：
[`world_model.py`](../src/air_ground_lab_server/scripts/world_model.py)

TELL 入口：

- `/server/{car,drone}/observation`
- `/server/{car,drone}/state`
- `/server/world_state/update`

ASK 出口：

- 锁存 `/server/world_state`
- 服务 `/server/query_world_state`

当前实现具有：

- 线程安全状态存储；
- agent 状态新鲜度过滤，默认 3 秒；
- 确定性的 agent 排序；
- 地图、动态障碍物、语义地标和更新时间合并；
- `snapshot`、`all`、`agent`、`nearest_landmark` 查询；
- 未实现或无结果查询返回 `found: false`，不伪造成功。

## 研究占位节点

服务器通过
[`server.launch`](../src/air_ground_lab_server/launch/server.launch)
启动五个节点：

| 节点 | 当前职责 |
|------|----------|
| `tcp_server` | TCP JSON → ICD 消息 |
| `world_model` | 全局状态存储与 ASK/TELL 中心 |
| `slam_node` | 统计抽象观测并发布感知更新时间 |
| `eqa_engine` | ASK World Model 后生成探索 Mission |
| `coordinator` | 校验、去重并分发到 `/<robot_id>/mission` |

`coordinator` 不再像旧草案一样直接发布 MAVROS setpoint 或具体控制器
`cmd_vel`。服务器只决定“做什么”，Edge 执行器负责“怎么做”。

## 公共接口

| 接口 | 类型 |
|------|------|
| `/drone/observation` | `air_ground_interfaces/Observation` |
| `/drone/state` | `air_ground_interfaces/RobotState` |
| `/drone/capability` | `air_ground_interfaces/Capability` |
| `/car/observation` | `air_ground_interfaces/Observation` |
| `/car/state` | `air_ground_interfaces/RobotState` |
| `/car/capability` | `air_ground_interfaces/Capability` |
| `/server/{car,drone}/observation` | `air_ground_interfaces/Observation` |
| `/server/{car,drone}/state` | `air_ground_interfaces/RobotState` |
| `/server/world_state` | `air_ground_interfaces/WorldState` |
| `/server/query_world_state` | `air_ground_interfaces/QueryWorldState` |
| `/server/eqa/query` | `std_msgs/String` |
| `/server/eqa/mission` | `air_ground_interfaces/Mission` |
| `/{car,drone}/mission` | `air_ground_interfaces/Mission` |

旧 `SensorFusion` 和 `ServerCommand` 仅保留兼容，不用于 Task-07 新接口。

## 自动验收

脚本：
[`test_server.sh`](../src/air_ground_lab_server/scripts/test_server.sh)

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
source devel/setup.bash
rosrun air_ground_lab_server test_server.sh
```

验收不依赖 Gazebo 和人工观察。确定性数据源会验证：

1. 两个 edge preprocessor、Task-06 TCP bridge 和五个服务器节点持续运行；
2. 车与无人机 Observation、RobotState、Capability 均有效；
3. TCP 服务器重建两个 agent 的 Observation 和 RobotState；
4. World Model 单个快照同时包含 car 和 drone；
5. `QueryWorldState` 返回指定且未过期的 agent；
6. EQA 查询经过 World Model ASK、Mission 生成和 Coordinator 分发；
7. 全程使用超时、独立日志和进程组清理，不以 `[WARN]` 作为通过。

## 验证记录

2026-07-27 在 Ubuntu 20.04、ROS Noetic、Gazebo 11 和 PX4 v1.14
环境完成：

| 验证项 | 结果 |
|--------|------|
| Python、Ruff、Shell、XML、YAML 静态检查 | ✅ |
| 全工作空间单元测试 | ✅，56 tests |
| 全工作空间 `catkin build` | ✅，5 个包 |
| `rosdep check --from-paths src --ignore-src` | ✅ |
| 安装目标 | ✅，Edge、Server、Launch、配置与验收辅助文件 |
| Task-07 localhost 自动验收 | ✅，15 passed, 0 failed |
| Task-02 回归 | ✅，9 passed, 0 failed |
| Task-03 回归 | ✅，7 passed, 0 failed |
| Task-04 回归 | ✅，17 passed, 0 failed |
| Task-05 回归 | ✅，28 passed, 0 failed |
| Task-06 回归 | ✅，11 passed, 0 failed |

## 验收结论

- [x] 两类 Edge 均输出 ICD 抽象消息和能力声明；
- [x] TCP 服务器能安全、完整地重建双 agent 数据；
- [x] World Model 成为服务器统一认知与查询入口；
- [x] 五个服务器节点均可独立运行；
- [x] EQA 查询能够触发稳定 Mission 分发链；
- [x] Layer 4 不依赖 MAVROS、Gazebo 或原始 LiDAR 消息；
- [x] Task-02～06 无回归。
