# Project Prometheus — 空地联合具身智能研究平台

> **A Research Infrastructure for Air-Ground Embodied Intelligence**
>
> 不是"最厉害的本科项目"。是一套能够持续演进五年以上的机器人研究平台。

[![Phase](https://img.shields.io/badge/phase-sim__framework-brightgreen)](./project-prometheus-tasks/ROADMAP.md)
[![Tasks](https://img.shields.io/badge/tasks-9/9-brightgreen)](./project-prometheus-tasks/00-OVERVIEW.md)
[![Tests](https://img.shields.io/badge/tests-56/56-brightgreen)](./project-prometheus-tasks/task-08-integration.md)
[![ROS](https://img.shields.io/badge/ROS-Noetic-brightgreen)](https://wiki.ros.org/noetic)
[![Gazebo](https://img.shields.io/badge/Gazebo_Classic-11-orange)](http://gazebosim.org/)
[![PX4](https://img.shields.io/badge/PX4-v1.14-blueviolet)](https://px4.io/)
[![Python](https://img.shields.io/badge/Python-3.8+-yellow)](https://www.python.org/)

---

## 这是什么？

一套**空地联合具身智能仿真与研究平台**。无人机 (PX4 SITL) + 地面车 (差速/麦轮双底盘) + 实验室服务器，通过 ROS 通信，在 Gazebo 中实现完整的数字孪生仿真。

目标不是"跑通一个 demo"，而是建立一套：
- 平台比论文寿命更长的 **Research Infrastructure**
- 接口稳定、模块可替换的 **插件式架构**
- 仿真即第一台机器人的 **Sim-First 工作流**

---

## 当前进度

| 阶段 | 状态 | 内容 |
|------|:---:|------|
| **Phase 0: 仿真框架** | ✅ 完成 (9/9) | 无人机 SITL ✅ · 差速底盘 ✅ · 麦轮底盘 ✅ · 传感器 ✅ · 通信桥 ✅ · 边缘服务器 ✅ · 集成总装 ✅ · 验证 ✅ |
| Phase 1: 实机调试 | 🔴 2026.08 | 组装 F450/S500 + 树莓派 + 传感器套件 |
| Phase 2: EQA 论文 | 🔴 2026.09~12 | VLM + SLAM + 空地联合探索 |
| Phase 3: 竞赛季 | 🔴 2027.01~08 | 全国电赛 + CRAIC2027 |
| Phase 4: 毕设 | 🔴 2027~2028 | World Model · 3DGS · Dreamer |

📋 详见 [00-OVERVIEW.md](./project-prometheus-tasks/00-OVERVIEW.md) 和 [ROADMAP.md](./project-prometheus-tasks/ROADMAP.md)

---

## 快速导航

| 你想…… | 去看 |
|---------|------|
| 理解这个项目的"为什么" | [RESEARCH_PHILOSOPHY.md](./project-prometheus-tasks/RESEARCH_PHILOSOPHY.md) |
| 理解系统架构 | [PLATFORM.md](./project-prometheus-tasks/PLATFORM.md) |
| 查看模块间接口 | [ICD.md](./project-prometheus-tasks/ICD.md) |
| 了解研究路线和时间线 | [ROADMAP.md](./project-prometheus-tasks/ROADMAP.md) |
| 查看当前任务进度 | [00-OVERVIEW.md](./project-prometheus-tasks/00-OVERVIEW.md) |
| 搭建仿真环境 | [task-01-env-setup.md](./project-prometheus-tasks/task-01-env-setup.md) |
| 了解命名/编码/提交流程规范 | [CONVENTIONS.md](./CONVENTIONS.md) |
| 查看架构决策记录 | [docs/decisions/](./docs/decisions/) |
| 阅读科研日记 | [Research_Diary.md](./Research_Diary.md) |
| 了解安全策略 | [SECURITY.md](./SECURITY.md) |

---

## 架构速览

```
┌──────────────────────────────────────────────┐
│  Layer 4: Research (研究层)        [待实现]  │
│  VLM · EQA · SLAM · World Model · Planner   │
│  只依赖抽象接口，永不引用硬件协议               │
├──────────────────────────────────────────────┤
│  Layer 3: Abstraction (抽象接口层)  [✅ 已实现] │
│  Observation · RobotState · WorldState       │
│  Mission · Capability  (air_ground_interfaces)│
├──────────────────────────────────────────────┤
│  Layer 2: Bridge (协议翻译层)       [✅ 已实现] │
│  MAVLink UDP ↔ ROS                             │
│  TCP JSON ↔ 抽象消息  (air_ground_com_bridge)  │
├──────────────────────────────────────────────┤
│  Layer 1: Hardware (硬件/仿真层)    [✅ 已实现] │
│  PX4 SITL · Gazebo · ros_control · Sensors   │
│  drone_bringup · car_bringup                   │
└──────────────────────────────────────────────┘
```

---

## 已实现的 ROS Package

| Package | 层级 | 功能 | 测试 |
|---------|:---:|------|:---:|
| `air_ground_interfaces` | Layer 3 | 10 个自定义消息 + 2 个服务 + 1 个 Action | — |
| `air_ground_drone_bringup` | Layer 1 | PX4 SITL 无人机 + 深度相机/GPS/IMU | ✅ |
| `air_ground_car_bringup` | Layer 1 | 差速/麦轮双底盘 + 车载传感器 + 云台 | 16/16 |
| `air_ground_com_bridge` | Layer 2 | MAVLink UDP 桥 + TCP JSON 桥 | 17/17 |
| `air_ground_lab_server` | Layer 4 | TCP 接收 + World Model + 研究占位节点 | ✅ |
| `air_ground_bringup` | Orchestration | 单 Gazebo 世界的顶层集成 Launch | ✅ |

---

## 环境要求

| 组件 | 版本 |
|------|------|
| Ubuntu | 20.04.6 |
| ROS | Noetic (Python 3.8+) |
| Gazebo | Classic 11 |
| PX4 | v1.14 SITL |
| Python | 3.8+ |

---

## 仓库结构

```
research_compitition/
├── README.md                           ← 你在这里
├── CONVENTIONS.md                      ← 命名/编码/提交规范
├── SECURITY.md                         ← 安全策略
├── .gitignore / .gitattributes
├── Research_Diary.md                   ← 科研日记
├── Memo_on_Division_of_Labor_Suggestions.md  ← AI 分工备忘录
├── docs/
│   └── decisions/                      ← ADR (架构决策记录)
├── project-prometheus-tasks/           ← 核心项目文档
│   ├── 00-OVERVIEW.md                  ← 总索引 + 任务清单
│   ├── RESEARCH_PHILOSOPHY.md          ← 设计哲学与宪法
│   ├── PLATFORM.md                     ← 平台架构
│   ├── ICD.md                          ← 接口控制文档
│   ├── ROADMAP.md                      ← 研究路线
│   └── task-01~09-*.md                 ← 实施任务
├── src/                                ← ROS Package 源代码
│   ├── air_ground_interfaces/          ← 自定义消息/服务
│   ├── air_ground_drone_bringup/       ← 无人机仿真
│   ├── air_ground_car_bringup/         ← 车机仿真
│   ├── air_ground_com_bridge/          ← 通信桥
│   ├── air_ground_lab_server/          ← 实验室服务器
│   └── air_ground_bringup/             ← 顶层集成启动
└── obsolete-documentation/ ← 历史文档归档
```

---

## 五条宪法原则

1. **Platform First** — 论文不破坏平台，平台比论文寿命更长
2. **Interface Before Implementation** — 先 ICD，再 Package，再代码
3. **Everything Produces Knowledge** — 输出 Knowledge，不是 Raw Data
4. **Simulation is the First Robot** — Gazebo 是第一台机器人，不是"假的"
5. **Every Module Must Be Replaceable** — 稳定的是 Capability，不是 Algorithm

详见 [RESEARCH_PHILOSOPHY.md](./project-prometheus-tasks/RESEARCH_PHILOSOPHY.md)。

---

## 快速开始

```bash
# 1. 克隆仓库
git clone https://github.com/vista777-nk/research_compitition.git
cd research_compitition

# 2. 在 Ubuntu 20.04 上搭建环境
# 详见 project-prometheus-tasks/task-01-env-setup.md

# 3. 编译全部 6 个包
make build

# 4. 运行全部已有测试
make test-all

# 5. 快速冒烟和完整 E2E 验证
make quick-smoke
make test-e2e

# 6. 一键启动完整仿真
make launch-full
```

---

> *"We shape our tools, and thereafter our tools shape us."*
>
> 这份仓库塑造了平台；希望平台也能塑造你的研究之路。

---

*维护者: @vista777-nk · 项目阶段: 规划完成，仿真搭建中*
