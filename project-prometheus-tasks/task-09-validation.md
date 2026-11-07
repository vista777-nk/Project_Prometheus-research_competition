# Task-09: 仿真集成验证 + 端到端测试

> **状态：✅ 已完成；本环境完整 E2E 33/33 通过。**

## 前置条件

- Task-01~08 全部完成
- `make build` 无错误
- 至少 4GB 可用内存，用于同时运行 PX4 SITL、Gazebo 和 ROS nodes
- `AIR_GROUND_WS` 指向当前 ROS 工作空间；默认使用仓库根目录

## 目标

验证数据从传感器、边缘预处理、通信桥、服务器 World Model 到 EQA/Mission 分发的完整闭环。

## 9.1 端到端集成测试

脚本：[`src/e2e_test.sh`](../src/e2e_test.sh)

脚本会完成以下阶段：

1. 加载 ROS/PX4/Gazebo 运行时并启动独立 ROS Master、Xvfb 和总入口 Launch。
2. 检查无人机 `iris` 与差速车 `diff_car` 已生成。
3. 检查无人机 GPS、IMU、深度相机，以及车辆 OpenMV、LiDAR、IMU、超声波和里程计数据。
4. 检查 `/drone|car/{observation,state}` 边缘抽象消息。
5. 检查 MAVLink/TCP 桥及服务器侧重建消息。
6. 检查 World Model、SLAM、EQA 和 Coordinator 节点。
7. 发布 `/server/eqa/query`，验证实际链路 `/server/eqa/mission` -> `/car/mission`。
8. 检查可用内存是否满足 4GB 前置条件。

运行方式：

```bash
bash src/e2e_test.sh
# 或
make test-e2e
```

可通过 `TASK09_TIMEOUT_SECONDS` 调整单项等待时间。脚本会记录完整 roslaunch 日志，并在退出时清理其进程组。

## 9.2 快速冒烟测试

脚本：[`src/quick_smoke.sh`](../src/quick_smoke.sh)

该脚本约 30 秒内启动差速总装，验证系统进程仍存活、`/car/observation`、`/drone/heartbeat`、`/server/world_state` 和 `coordinator`。适合提交前快速回归：

```bash
bash src/quick_smoke.sh
# 或
make quick-smoke
```

可通过 `TASK09_SMOKE_BOOT_SECONDS` 调整启动等待时间。

## 9.3 最终检查清单

完成所有 9 个 task 后，逐一确认：

| # | 检查项 | 命令 |
|---|--------|------|
| 1 | 工作空间编译无错误 | `make build` |
| 2 | 自定义消息可用 | `source scripts/setup_runtime.sh ros && rosmsg show air_ground_interfaces/SensorFusion` |
| 3 | 无人机可启动 | `make launch-drone`（等 15s，`Ctrl+C`） |
| 4 | 差速小车可启动 | `make launch-car` |
| 5 | 麦轮小车可启动 | `make launch-car-mecanum` |
| 6 | 所有传感器话题有数据 | `make test-sensors` |
| 7 | 通信桥正常转发 | `make test-bridge` |
| 8 | 服务器节点运行 | `make test-server` |
| 9 | 完整系统启动 | `make launch-full` |
| 10 | E2E 测试全 PASS | `make test-e2e` |

底盘切换服务由 `car_mecanum.launch` 中的 `chassis_swapper` 提供，已经由 `make test-mecanum` 覆盖；差速总装不宣称提供 `/car/swap_chassis`。

## 9.4 已知限制与 TODO

| 限制 | 说明 | 后续计划 |
|------|------|---------|
| 麦轮摩擦近似 | Gazebo 无法原生模拟麦轮辊子，用 `mu2≈0` 近似 | 实机上验证即可 |
| 云台为速度控制 | Gazebo 中云台是 velocity-controlled joint | 加 PID 位置环 |
| MAVLink 解析不完整 | 当前只解析 heartbeat 和 global_position | 按需扩展 pymavlink 解析 |
| 无 world 模型 | 使用空白世界，无障碍物 | 后续加障碍物世界测试 SLAM |
| TCP 无加密 | 仿真中明文传输，实机需加 TLS | 实机部署时处理 |

## 交付产物

1. `src/e2e_test.sh`：验证传感器 -> 边缘 -> 服务器 -> Mission 分发链路。
2. `src/quick_smoke.sh`：供日常开发快速验证。
3. Makefile 中的 `test-e2e`、`quick-smoke` 入口。
4. 最终检查清单可在目标 Ubuntu 20.04 仿真主机执行。
