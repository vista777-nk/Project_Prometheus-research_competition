# Task-06：空地通信桥（MAVLink UDP + TCP JSON）

## 状态

| 项目 | 结果 |
|------|------|
| 依赖 | Task-02、Task-03 |
| 实施状态 | ✅ 已完成 |
| 完成日期 | 2026-07-27 |
| Task-06 自动验收 | `11 passed, 0 failed` |
| 真实 PX4 联合验证 | `3 passed, 0 failed` |
| 回归验收 | Task-03 `7/7`；Task-04 `17/17` |

## 目标与拓扑

在车机侧建立两条受控通信链路：

```text
PX4/无人机 ── MAVLink UDP ──▶ drone_car_bridge ── ROS
车载传感器 ── ROS ──▶ edge_server_bridge ── TCP JSON ──▶ 实验室服务器
```

仿真和实机使用同一逻辑无线电端口：

```text
逻辑无人机 127.0.0.1:14550 ↔ 车机 127.0.0.1:14551
```

PX4 v1.14 SITL 的 GCS 实例实际绑定 `18570`，并非旧草案假设的 `14550`。
仿真默认启用透明适配层：

```text
PX4 SITL :18570 ↔ 无人机逻辑端 :14550 ↔ 车机逻辑端 :14551
```

适配层由
[`network.yaml`](../src/air_ground_com_bridge/config/network.yaml)
的 `simulation_adapter.enable` 控制；实机部署时关闭即可。

## 实现

### MAVLink UDP 桥

节点：
[`drone_car_bridge.py`](../src/air_ground_com_bridge/scripts/drone_car_bridge.py)

- 增量解析 MAVLink v1，支持分片、前导噪声和单个 UDP 包内多帧；
- 对 HEARTBEAT、GLOBAL_POSITION_INT 和 COMMAND_LONG 校验 X.25 CRC extra；
- 可选使用 pymavlink 解析 MAVLink v2；未安装时 v1 核心链路仍可工作；
- 从 `/drone/gps/local_pose` 复用 Task-02 已转换的 ENU 位姿；
- 周期发送 GCS HEARTBEAT，使 PX4 SITL 学习 UDP 回程地址；
- ROS 上行命令必须是结构化 JSON，并编码为合法 COMMAND_LONG；
- 拒绝非法 JSON、越界 ID、超长参数和 NaN/Inf；
- 对命令链路应用 24 KB/s 令牌桶限流；
- 心跳超过三个周期未到达时发布断连状态。

`/car/to_drone/cmd` 示例：

```bash
rostopic pub -1 /car/to_drone/cmd std_msgs/String \
  "data: '{\"command\":400,\"params\":[1.0]}'"
```

### TCP JSON 桥

节点：
[`edge_server_bridge.py`](../src/air_ground_com_bridge/scripts/edge_server_bridge.py)

- 缓存车体里程计、IMU、LiDAR、OpenMV、四向超声波及无人机位姿；
- LiDAR 默认 4:1 降采样，OpenMV JPEG 质量 50、最大 2 Hz；
- JSON 禁止 NaN/Inf，非有限测距转换为 `-1.0`；
- 每帧使用 4 字节网络序长度头，接收端保证完整读取；
- 单帧最大 10 MiB，拒绝零长度和超限数据；
- 24 KB/s 令牌桶限制总上行带宽；
- 服务器缺席时保持节点运行并每 3 秒重试；
- 断线后关闭旧连接并自动恢复发送；
- 服务器命令经过字段与有限数值验证后发布到 ROS。

TCP 上行格式与 Task-07 `tcp_receiver` 兼容，主要字段包括：

```json
{
  "protocol_version": 1,
  "kind": "telemetry",
  "timestamp": 0.0,
  "source": "car",
  "pose": {},
  "twist": {},
  "imu": {},
  "scan": {},
  "ultrasonic": {},
  "image_jpeg_b64": "",
  "drone_pose": {}
}
```

## 公共与兼容接口

| 接口 | 类型 | 方向 |
|------|------|------|
| `/drone/heartbeat` | `std_msgs/Bool` | MAVLink → ROS |
| `/drone/pose` | `geometry_msgs/PoseStamped` | Task-02 ENU → ROS |
| `/drone/state` | `air_ground_interfaces/RobotState` | MAVLink → ROS |
| `/car/to_drone/cmd` | `std_msgs/String`（JSON） | ROS → MAVLink |
| `/car/server_connected` | `std_msgs/Bool` | TCP 状态 |
| `/car/server_command` | `air_ground_interfaces/ServerCommand` | TCP → ROS |

`/drone/state` 使用 ICD 规定的 `RobotState`，修正了草案中
`std_msgs/String` 与 Task-07 稳定接口类型冲突的问题。
`ServerCommand` 是保留到 Task-09 的兼容消息；后续稳定任务接口为 `Mission`。

## 配置

[`network.yaml`](../src/air_ground_com_bridge/config/network.yaml)
集中配置以下内容：

| 配置 | 默认值 |
|------|-------:|
| 无人机逻辑 UDP 端口 | 14550 |
| 车机逻辑 UDP 端口 | 14551 |
| PX4 v1.14 SITL GCS 端口 | 18570 |
| 服务器 TCP 端口 | 9090 |
| TCP 重连间隔 | 3 s |
| TCP 发布频率 | 10 Hz |
| 最大 TCP 帧 | 10 MiB |
| 总带宽 | 24,000 B/s |
| JPEG 质量 | 50 |
| 图像最大频率 | 2 Hz |
| LiDAR 降采样 | 4:1 |

启动入口：

```bash
roslaunch air_ground_com_bridge air_ground_com_bridge.launch
```

可使用 `start_drone_bridge` 和 `start_edge_bridge` 参数单独启动任一链路。

## 自动验收

脚本：
[`test_bridge.sh`](../src/air_ground_com_bridge/scripts/test_bridge.sh)

```bash
cd ~/air_ground_sim_ws
source /opt/ros/noetic/setup.bash
source devel/setup.bash
rosrun air_ground_com_bridge test_bridge.sh
```

验收脚本不依赖人工观察，也不把 `[WARN]` 当作通过。它会：

1. 检查 14550、14551、18570 和 9090 无端口占用；
2. 在服务器缺席时确认 TCP retry 状态；
3. 发送带合法 CRC 的 MAVLink HEARTBEAT；
4. 校验 `/drone/pose` 和 ICD `RobotState`；
5. 验证 ROS JSON 命令被编码为合法 COMMAND_LONG；
6. 发布完整模拟车载传感器数据；
7. 验证长度头 JSON、90 点 LiDAR、四向超声波和 base64 JPEG；
8. 分片发送服务器命令并校验 `/car/server_command`；
9. 强制断开第一次 TCP 连接，要求第二次连接恢复遥测；
10. 全程使用超时、独立日志和进程组清理。

## 验证记录

2026-07-27 在 Ubuntu 20.04、ROS Noetic、PX4 v1.14 和 Gazebo 11
环境完成：

| 验证项 | 结果 |
|--------|------|
| Python、Shell、XML、Launch 静态检查 | ✅ |
| MAVLink/TCP 单元测试 | ✅，16 tests |
| pymavlink HEARTBEAT 交叉解析 | ✅ |
| 全工作空间 `catkin build` | ✅，5 个包 |
| `rosdep check --from-paths src --ignore-src` | ✅ |
| 安装目标 | ✅，节点、脚本、launch、config |
| Task-06 localhost 自动验收 | ✅，11 passed, 0 failed |
| 真实 PX4/MAVROS 连接 | ✅ |
| PX4 18570→14550→14551 心跳 | ✅ |
| Task-02 ENU 位姿经 Task-06 转发 | ✅ |
| Task-03 回归 | ✅，7 passed, 0 failed |
| Task-04 回归 | ✅，17 passed, 0 failed |

## 验收结论

- [x] MAVLink UDP 双向通信具有有效帧和 CRC；
- [x] PX4 v1.14 实际端口已完成联合验证；
- [x] 车载传感器可按限制编码并发送到 TCP；
- [x] 服务器缺席、断线和重连均不会导致节点退出；
- [x] 下行命令可完整、安全地重建为 ROS 消息；
- [x] Task-03、Task-04 无回归。
