# Project Prometheus — 空地联合具身智能研究平台

> **A Research Infrastructure for Air-Ground Embodied Intelligence**
>
> 不是"最厉害的本科项目"。是一套能够持续演进五年以上的机器人研究平台。

[![Status](https://img.shields.io/badge/status-planning-blue)](./Project_Prometheus_Tasks/ROADMAP.md)
[![ROS](https://img.shields.io/badge/ROS-Noetic-brightgreen)](https://wiki.ros.org/noetic)
[![Gazebo](https://img.shields.io/badge/Gazebo-9-orange)](http://gazebosim.org/)
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

## 快速导航

| 你想…… | 去看 |
|---------|------|
| 理解这个项目的"为什么" | [RESEARCH_PHILOSOPHY.md](./Project_Prometheus_Tasks/RESEARCH_PHILOSOPHY.md) |
| 理解系统架构 | [PLATFORM.md](./Project_Prometheus_Tasks/PLATFORM.md) |
| 查看模块间接口 | [ICD.md](./Project_Prometheus_Tasks/ICD.md) |
| 了解研究路线和时间线 | [ROADMAP.md](./Project_Prometheus_Tasks/ROADMAP.md) |
| 开始搭建仿真环境 | [task-01-env-setup.md](./Project_Prometheus_Tasks/task-01-env-setup.md) |
| 了解命名/编码/提交流程规范 | [CONVENTIONS.md](./CONVENTIONS.md) |
| 查看架构决策记录 | [docs/decisions/](./docs/decisions/) |

---

## 架构速览

```
┌──────────────────────────────────────────────┐
│  Layer 4: Research (研究层)                  │
│  VLM · EQA · SLAM · World Model · Planner   │
│  只依赖抽象接口，永不引用硬件协议               │
├──────────────────────────────────────────────┤
│  Layer 3: Abstraction (抽象接口层)            │
│  Observation · RobotState · WorldState       │
│  Mission · Capability                        │
├──────────────────────────────────────────────┤
│  Layer 2: Bridge (协议翻译层)                 │
│  MAVLink ↔ ROS · TCP JSON ↔ 抽象消息         │
├──────────────────────────────────────────────┤
│  Layer 1: Hardware (硬件/仿真层)              │
│  PX4 SITL · Gazebo · ros_control · Sensors   │
└──────────────────────────────────────────────┘
```

---

## 环境要求

| 组件 | 版本 |
|------|------|
| Ubuntu | 20.04.6 |
| ROS | Noetic (Python 3.8+) |
| Gazebo | 9 |
| PX4 | v1.14 SITL |
| Python | 3.8+ |

---

## 仓库结构

```
research_compitition/
├── README.md                           ← 你在这里
├── CONVENTIONS.md                      ← 命名/编码/提交规范
├── .gitignore
├── Research_Diary.md                   ← 科研日记
├── Memo_on_Division_of_Labor_Suggestions.md  ← AI 分工备忘录
├── docs/
│   └── decisions/                      ← ADR (架构决策记录)
├── Project_Prometheus_Tasks/           ← 核心项目文档
│   ├── 00-OVERVIEW.md                  ← 总索引
│   ├── RESEARCH_PHILOSOPHY.md          ← 设计哲学与宪法
│   ├── PLATFORM.md                     ← 平台架构
│   ├── ICD.md                          ← 接口控制文档
│   ├── ROADMAP.md                      ← 研究路线
│   └── task-01~09-*.md                 ← 实施任务
└── Obsolete_or_Outdated_Documentation/ ← 历史文档归档
```

---

## 五条宪法原则

1. **Platform First** — 论文不破坏平台，平台比论文寿命更长
2. **Interface Before Implementation** — 先 ICD，再 Package，再代码
3. **Everything Produces Knowledge** — 输出 Knowledge，不是 Raw Data
4. **Simulation is the First Robot** — Gazebo 是第一台机器人，不是"假的"
5. **Every Module Must Be Replaceable** — 稳定的是 Capability，不是 Algorithm

详见 [RESEARCH_PHILOSOPHY.md](./Project_Prometheus_Tasks/RESEARCH_PHILOSOPHY.md)。

---

## 快速开始

```bash
# 1. 克隆仓库
git clone https://github.com/vista777-nk/research_compitition.git
cd research_compitition

# 2. 阅读总索引
cat Project_Prometheus_Tasks/00-OVERVIEW.md

# 3. 按顺序执行任务
# 从 task-01-env-setup.md 开始
```

---

> *"We shape our tools, and thereafter our tools shape us."*
>
> 这份仓库塑造了平台；希望平台也能塑造你的研究之路。

---

*维护者: @vista777-nk · 项目阶段: 规划完成，仿真搭建中*
