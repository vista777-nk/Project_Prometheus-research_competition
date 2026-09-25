# Project Prometheus — 空地联合具身智能研究平台

> **A Research Infrastructure for Air-Ground Embodied Intelligence**
>
> 不是"最厉害的本科项目"。是一套能够持续演进五年以上的机器人研究平台。

[![Phase](https://img.shields.io/badge/phase1.5-hardware__integration-yellow)](./project-prometheus-tasks/ROADMAP.md)
[![Phase0](https://img.shields.io/badge/phase0_tasks-9/9-brightgreen)](./project-prometheus-tasks/00-OVERVIEW.md)
[![Phase1](https://img.shields.io/badge/phase1_tasks-6/6-brightgreen)](./project-prometheus-tasks/00-OVERVIEW.md)
[![ROS Tests](https://img.shields.io/badge/ROS_tests-82/82-brightgreen)](./docs/decisions/ADR-0014.md)
[![E2E](https://img.shields.io/badge/E2E-33/33-brightgreen)](./project-prometheus-tasks/task-09-validation.md)
[![Firmware Host](https://img.shields.io/badge/firmware_host-151/151-brightgreen)](./src/firmware/README.md)
[![Deploy Validate](https://img.shields.io/badge/deploy_validate-55/55-brightgreen)](./src/deployment/README.md)
[![Smoke Phase1](https://img.shields.io/badge/smoke_phase1-65/65-brightgreen)](./project-prometheus-tasks/task-15-calibration-validation.md)
[![ROS](https://img.shields.io/badge/ROS-Noetic-brightgreen)](https://wiki.ros.org/noetic)
[![PX4](https://img.shields.io/badge/PX4-v1.14-blueviolet)](https://px4.io/)
[![CI](https://github.com/vista777-nk/Project_Prometheus-research_competition/actions/workflows/ci.yml/badge.svg)](https://github.com/vista777-nk/Project_Prometheus-research_competition/actions/workflows/ci.yml)

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
| **Phase 0: 仿真框架** | ✅ 完成 (9/9) | 无人机 SITL · 差速底盘 · 麦轮底盘 · 传感器 · 通信桥 · 边缘服务器 · 集成总装 · 验证 |
| **Phase 1: 固件+部署先行** | ✅ 完成 (6/6) | STM32 麦轮固件 ✅ · MSPM0 差速固件 ✅ · 树莓派部署 (无人机+车机) ✅ · CI 流水线 ✅ · 传感器驱动骨架 + MAVLink 签名 ✅ · 标定工具链 + 集成验证 ✅ |
| **Phase 1.5: 实机接入** | 🟡 进行中 | 地面车参数/接口基线 ✅ · STM32 HC-SR04 ✅ · 中关村服务器收口 ✅ · 全分支 CI ✅ · Pi 实机/MSPM0 SysConfig/组装/受控隧道/标定待完成 |
| Phase 2: EQA 论文 | 🔴 2026.09~12 | VLM + SLAM + 空地联合探索 |
| Phase 3: 竞赛季 | 🔴 2027.01~08 | 全国电赛 + CRAIC2027 |
| Phase 4: 毕设 | 🔴 2027~2028 | World Model · 3DGS · Dreamer |

> **Phase 1 策略**：先固件，后上机；先接口，后算法；先可复现，后联调。80% 代码工作不需要硬件，CI + Docker 替代本地环境。
> 详见 [Phase 1 Pre-Departure Brief](./obsolete-documentation/phase1-pre-departure-brief.md)（已归档）。

📋 详见 [00-OVERVIEW.md](./project-prometheus-tasks/00-OVERVIEW.md) 和 [ROADMAP.md](./project-prometheus-tasks/ROADMAP.md)

---

## 快速导航

| 你想…… | 去看 |
|---------|------|
| **在 Raspberry Pi 接手 Phase 1.5** | **[docs/experiments/AI_HANDOFF.md](./docs/experiments/AI_HANDOFF.md)** — Pi 上机顺序、安全边界、未完成工作和硬件未知项 |
| 查看服务器离场前状态 | [Phase 1.5 服务器收口报告](./docs/experiments/phase-1.5-server-readiness-2026-08-03.md) |
| 理解这个项目的"为什么" | [RESEARCH_PHILOSOPHY.md](./project-prometheus-tasks/RESEARCH_PHILOSOPHY.md) |
| 理解系统架构 | [PLATFORM.md](./project-prometheus-tasks/PLATFORM.md) |
| 查看模块间接口 | [ICD.md](./project-prometheus-tasks/ICD.md) |
| 了解研究路线和时间线 | [ROADMAP.md](./project-prometheus-tasks/ROADMAP.md) |
| 查看全部任务进度 | [00-OVERVIEW.md](./project-prometheus-tasks/00-OVERVIEW.md) |
| 🆕 开始 Phase 1 固件开发 | [task-10](./project-prometheus-tasks/task-10-stm32-mecanum-firmware.md) · [task-11](./project-prometheus-tasks/task-11-mspm0-diff-firmware.md) |
| 🆕 搭建树莓派部署 | [task-12](./project-prometheus-tasks/task-12-drone-firmware-and-rpi-deployment.md) |
| 🆕 标定与集成验证 | [task-15](./project-prometheus-tasks/task-15-calibration-validation.md) · [标定 README](./src/deployment/calibration/README.md) |
| 搭建仿真环境 (Phase 0) | [task-01-env-setup.md](./project-prometheus-tasks/task-01-env-setup.md) |
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
│  Phase 1: STM32/MSPM0 固件 ✅ (已交付)        │
└──────────────────────────────────────────────┘
```

---

## 已实现的 ROS Package

| Package | 层级 | 功能 | 测试 |
|---------|:---:|------|:---:|
| `air_ground_interfaces` | Layer 3 | 10 个自定义消息 + 2 个服务 + 1 个 Action | — |
| `air_ground_drone_bringup` | Layer 1 | PX4 SITL 无人机 + 深度相机/GPS/IMU | ✅ |
| `air_ground_car_bringup` | Layer 1 | 差速/麦轮双底盘 + 车载传感器 + 云台仿真控制/实机边界 | ROS 40/40 + Host 81/81 |
| `air_ground_com_bridge` | Layer 2 | MAVLink UDP 桥 + TCP JSON 桥 | 17/17 |
| `air_ground_lab_server` | Layer 2~3 | TCP 接收 + World Model + 研究占位节点 | ✅ |
| `air_ground_bringup` | Orchestration | 单 Gazebo 世界的顶层集成 Launch | ✅ |

---

## 环境要求

| 组件 | 版本 |
|------|------|
| 仿真/服务器 | Ubuntu 20.04.6 |
| 树莓派宿主 | Pi 5 · ARM64 · 8 GB · Debian 13 (Trixie) · 64 GB microSD |
| ROS | Noetic (Python 3.8+；Pi 上运行于 Focal ARM64 容器) |
| Gazebo | Classic 11 |
| PX4 | v1.14 SITL |
| Python | 3.8+ |

---

## 仓库结构

```
project-prometheus/
├── README.md                           ← 你在这里
├── CONVENTIONS.md                      ← 命名/编码/提交规范
├── SECURITY.md                         ← 安全策略
├── LICENSE / Makefile / requirements.txt / setup_all.sh
├── .gitignore / .gitattributes / .github/  ← CI 工作流在 .github/workflows/
├── Research_Diary.md                   ← 科研日记
├── scripts/                            ← 运行环境 setup + Phase 1 冒烟脚本
├── docs/
│   ├── architecture/                   ← 能力矩阵 (capability_matrix.md)
│   ├── decisions/                      ← ADR (架构决策记录, ADR-0001~0019)
│   └── experiments/                    ← 实验记录 + AI_HANDOFF.md (交接必读)
├── project-prometheus-tasks/           ← 核心项目文档
│   ├── 00-OVERVIEW.md                  ← 总索引 + 任务清单
│   ├── RESEARCH_PHILOSOPHY.md          ← 设计哲学与宪法
│   ├── PLATFORM.md                     ← 平台架构
│   ├── ICD.md                          ← 接口控制文档
│   ├── ROADMAP.md                      ← 研究路线
│   └── task-01~15-*.md                 ← 实施任务
├── imu-icm42688/                       ← 硬件参考：ICM42688 IMU
├── mspm0g3507/                         ← 硬件参考：MSPM0G3507 MCU
├── openmv-visual-module/               ← 硬件参考：OpenMV 视觉模块
├── r5-general-chassis-panel-specifications/  ← 硬件参考：R5 底盘底板
├── stm32f407vet6/                      ← 硬件参考：STM32F407VET6 MCU
├── two-axis-gimbal/                    ← 硬件参考：二轴云台
├── src/                                ← 源代码
│   ├── air_ground_interfaces/          ← 自定义消息/服务
│   ├── air_ground_drone_bringup/       ← 无人机仿真
│   ├── air_ground_car_bringup/         ← 车机仿真
│   ├── air_ground_com_bridge/          ← 通信桥
│   ├── air_ground_lab_server/          ← 实验室服务器
│   ├── air_ground_bringup/             ← 顶层集成启动
│   ├── firmware/                       ← 下位机固件 (非 ROS)
│   │   ├── common/                     ← 共享模块 (PID/CRC/Unity)
│   │   ├── stm32_mecanum/              ← STM32F407 麦轮固件
│   │   └── mspm0_diff/                 ← MSPM0G3507 差速固件
│   ├── deployment/                     ← 部署配置 (非 ROS)
│   │   ├── docker/                     ← Docker 镜像
│   │   ├── systemd/                    ← 自启服务
│   │   ├── network/                    ← Pi UART / Pixhawk / 915 MHz 数传
│   │   ├── ssh/                        ← SSH 加固
│   │   ├── mavlink/                    ← MAVLink 签名
│   │   ├── calibration/                ← 标定工具链 + 归档规范
│   │   ├── test/                       ← 集成验证 (串口回路 / 数据流)
│   │   └── healthcheck/                ← 健康检查
│   ├── e2e_test.sh                     ← Task-09 E2E 验收
│   ├── quick_smoke.sh                  ← Task-09 快速冒烟
│   └── test_task01.sh                  ← Task-01 环境验收
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
git clone https://github.com/vista777-nk/Project_Prometheus-research_competition.git project-prometheus
cd project-prometheus

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

# --- Phase 1 (无需 Ubuntu 20.04) ---
# 7. 编译 STM32 麦轮固件 (任意 OS + ARM GCC)
cd src/firmware/stm32_mecanum && make

# 8. 编译 MSPM0 差速固件 (任意 OS + ARM GCC)
cd src/firmware/mspm0_diff && make

# 9. 构建树莓派 Docker 镜像
docker build --platform linux/arm64 \
  -f src/deployment/docker/Dockerfile.edge -t air-ground-edge:v1 .

# 10. Phase 1 冒烟 —— 交付物存在性 + 语法 + 自测 + CI 归属 (任意 OS, 不需要 ROS)
make smoke-phase1

# 11. 部署与标定配置静态校验 (与 CI 跑同一份脚本)
make validate-deployment

# 12. 64 GB 新卡上的 Pi 基线预检；软件和接口装好后改用 --stage deploy
python3 src/deployment/healthcheck/check_pi_host.py --stage base
```

> 第 10、11 步在没有 numpy / OpenCV / pymavlink 的机器上会把相应检查报成
> **SKIP 而不是通过** —— 完整校验以 CI 为准。

---

> *"We shape our tools, and thereafter our tools shape us."*
>
> 这份仓库塑造了平台；希望平台也能塑造你的研究之路。

---

*维护者: @vista777-nk · Phase 0 仿真完成 ✅ · Phase 1 固件+部署完成 ✅（待实机联调）*
