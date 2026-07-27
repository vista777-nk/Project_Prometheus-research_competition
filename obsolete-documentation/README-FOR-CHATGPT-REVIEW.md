# 空地联合 EQA 仿真孪生 — 分发给 Subagent 的使用说明

> ⚠️ **此文档已过时，保留仅供历史参考。** 当前项目已迁至 `research_compitition/`，
> 目录结构、ROS package 命名、接口定义均已发生重大变化。
> 请以 [`project-prometheus-tasks/`](../project-prometheus-tasks/) 下的最新文档为准。

> 本文档供 ChatGPT 校阅。目标读者：AI 编程助手 (subagent)。  
> 项目仓库：`e:\Vista\Pictures\Temp\research_compitition\tasks\`

---

## 一、项目背景

用户在海外，手头仅一台华为轻薄本（Ubuntu 20.04.6，无独显）。计划在回国前利用数字孪生仿真搭建一套 **空地联合具身问答系统 (EQA)** 的完整 ROS 代码框架。

目标架构：

```
实验室服务器 (VLM / SLAM / EQA 推理)
    ↕ TCP (WiFi)
车机边缘 (树莓派5 模拟)  ←── MAVLink (3DR 数传) ──→  无人机边缘 (树莓派5 模拟)
    ↕                                        ↕
差速底盘 或 麦轮底盘                         Pixhawk 6C SITL
  + LiDAR + OpenMV + 超声波 + IMU              + 深度相机 + GPS + IMU
```

- 无人机：PX4 SITL (v1.14) + Gazebo 11，搭载深度相机、GPS、IMU
- 车机：Gazebo 独立模型，搭载 OpenMV 云台相机、RPLIDAR A1、4×HC-SR04、ICM42688 IMU，底盘可在差速/麦轮间模块化切换
- 通信：无人机↔车 3DR 数传直连 (MAVLink)；车↔服务器 TCP (模拟 WiFi)
- 所有智能算法在服务器端运行（VLM、SLAM、EQA），边缘端仅做传感器数据采集、预处理和中继

---

## 二、任务文件清单

所有任务文件位于 `tasks/` 目录下。每个任务是一个完整、独立、可执行的单元。

| 文件 | 内容 | 预计耗时 | 依赖 |
|------|------|---------|------|
| `00-OVERVIEW.md` | 总体架构说明、ROS 包结构、话题一览 | 阅读用 | — |
| `task-01-env-setup.md` | ROS Noetic + Gazebo 11 + PX4 工具链 + 工作空间脚手架 + 自定义消息 | 1h | — |
| `task-02-drone-sitl.md` | PX4 SITL 无人机 + 深度相机/GPS/IMU Gazebo 插件 | 2h | task-01 |
| `task-03-diff-chassis.md` | 差速底盘 URDF + ros_control + 编码器车轮 + 万向轮 | 2h | task-01 |
| `task-04-mecanum-chassis.md` | 麦轮底盘 URDF + 自定义逆运动学控制器 + 底盘动态切换服务 | 2h | task-03 |
| `task-05-sensors.md` | 车机传感器 URDF 宏（OpenMV 云台、LiDAR、超声波×4、IMU）+ 传感器 YAML 配置 | 1.5h | task-02,03 |
| `task-06-com-bridge.md` | MAVLink 解析桥（UDP 收发）+ 边缘↔服务器 TCP 桥（JSON 序列化、带宽限流） | 2h | task-02,03 |
| `task-07-edge-server.md` | 无人机/车机预处理节点（SensorFusion 聚合）；服务器 TCP 接收器 + SLAM/EQA/Coordinator 占位 | 2h | task-01,06 |
| `task-08-integration.md` | 总 launch 文件（一键启动）+ 分场景 launch + Makefile + setup_all.sh | 1.5h | task-02~07 |
| `task-09-validation.md` | 端到端集成测试脚本 + 快速冒烟测试 + 最终检查清单 | 1h | task-08 |

---

## 三、推荐分发顺序

任务间存在硬依赖。建议按以下拓扑分发给 subagent：

```
                     ┌──→ task-02 (Drone) ──┐
task-01 (Env) ───┤                        ├──→ task-05 (Sensors)
                     └──→ task-03 (Diff)  ──┤         │
                              │               │         │
                              └──→ task-04 (Mecanum)    │
                                                       │
task-06 (Bridge) ←── task-02,03 ───────────────────────┘
      │
task-07 (Edge+Server) ←── task-01,06
      │
task-08 (Integration) ←── task-02~07
      │
task-09 (Validation) ←── task-08
```

**并行窗口**：
- task-02 和 task-03 可以同时分发（无相互依赖）
- task-04 在 task-03 完成后立即跟进
- task-05 在 task-02 和 task-03 都完成后分发

---

## 四、每个 Subagent 的标准执行流程

1. 通读分配给它的 task 文件全文
2. 严格按照文件中的 **命令**（bash 块）和 **代码** 逐段执行
3. 所有文件路径使用 `~/air_ground_sim_ws/...` 绝对路径
4. 每个 Python 脚本执行 `chmod +x`
5. 完成后运行 task 文件末尾的**验证脚本**（`test_xxx.sh`）
6. 报告结果：PASS/FAIL 数量 + 关键错误信息

---

## 五、Subagent 容易踩的坑（重点提醒）

以下 8 条是跨 task 的关键约定，请在每个 subagent 的 prompt 中强调：

### 5.1 硬件约束
- **目标机是华为轻薄本，无 NVIDIA GPU**。Gazebo **必须**以 `headless:=true` 启动。GUI 仅限调试偶尔打开。
- 所有传感器分辨率和频率已按 **真实硬件规格** 设置，禁止为提高仿真效果擅自提高（会导致轻薄本卡死）：
  - 深度相机：320×240 @ 15Hz
  - OpenMV：320×240 @ 10Hz
  - RPLIDAR A1：360 samples @ 10Hz
  - HC-SR04：1 beam @ 25Hz

### 5.2 仿真约定
- 所有节点运行在同一台机器上，通过 ROS **namespace** 隔离：`/drone/`、`/car/`、`/server/`
- 两个 Gazebo 实例（无人机 + 车）使用不同的 `world` 文件，避免模型冲突
- PX4 SITL 启动需要 12-15 秒初始化，所有测试脚本中 `sleep` 时间至少 10 秒

### 5.3 通信约定
- 无人机↔车 MAVLink：UDP `127.0.0.1:14550` ↔ `127.0.0.1:14551`
- 车↔服务器 TCP：`127.0.0.1:9090`
- 在仿真中**所有通信走 localhost**，不同端口区分

### 5.4 代码风格
- Python 3.8+，ROS 节点使用 `rospy`（非 `rospy2`）
- 变量名英文，模块级 docstring 用中文
- 所有关键参数从 `config/*.yaml` 读取，**不硬编码**
- 传感器话题无数据时，打印 `rospy.logwarn` 而非 crash

### 5.5 占位节点
- `slam_node.py`、`eqa_engine.py`、`coordinator.py` 当前只需**占位运行**，不需要实现真实算法
- 占位节点应：订阅相关话题、打印统计信息（Hz）、发布空结果

### 5.6 麦轮仿真
- Gazebo 无法原生仿真麦轮辊子的横向滑动
- 采用 `<surface><friction><ode><mu2>0.01</mu2>` 近似
- 这是**有意为之的工程权衡**，不要试图做物理级仿真

### 5.7 电赛合规
- 仿真框架中的底盘控制用 `ros_control`（无 MCU 模拟），这是为快速验证架构
- 电赛实际比赛时需将控制逻辑移植到 MSPM0G3507（TI MCU）
- 当前仿真暂不模拟 MCU 层级

### 5.8 测试要求
- 每个 task 末尾必须有验证脚本，产出明确的 PASS/FAIL
- task-09 是最终 E2E 测试，必须全绿才算项目交付
- 测试脚本中 `rostopic echo` 用 `timeout 5` 防止永久挂起

---

## 六、给 User（你）的本地操作指南

你的华为轻薄本上只需三步即可启动：

```bash
# 1. 确认系统环境
lsb_release -a          # 应为 Ubuntu 20.04.x
free -h                 # 确保 ≥4GB 空闲

# 2. 创建工作空间根目录（subagent 会往里填充）
mkdir -p ~/air_ground_sim_ws/src

# 3. 所有 subagent 完成后，编译并验证
cd ~/air_ground_sim_ws
make build               # 编译所有包
bash src/e2e_test.sh     # 端到端测试，全 PASS 即交付

# 日常开发
make launch-full         # 启动完整系统
make kill                # 杀掉所有仿真进程
make status              # 查看运行状态
```

---

## 七、文档版本

- **日期**：2026-07-25
- **用途**：供 ChatGPT 校阅，确认逻辑一致性、可执行性、任务划分合理性
- **审阅重点**：
  1. 9 个 task 的依赖关系是否闭环
  2. 每个 task 的输入/输出是否清晰
  3. 是否有遗漏的关键模块
  4. 轻薄本资源约束下的参数设置是否合理
  5. 代码约定（namespace、topic、端口）是否一致
