# Phase 1 Pre-Departure Brief — 给 AI Subagent 的上下文摘要

> ⚠️ **此文档已归档，保留仅供历史参考。** 写于 Phase 0→1 交接（2026-07-28）；Phase 1 已于 2026-08-01 完成，现状以 [`docs/experiments/AI_HANDOFF.md`](../docs/experiments/AI_HANDOFF.md) 为准。

> **目标读者**: AI LLM Agent（DeepSeek / ChatGPT / Claude / 混元 等）  
> **日期**: 2026-07-28  
> **状态**: Phase 0 仿真框架 9/9 全部完成，用户尚未回国，希望提前启动 Phase 1 软件工作

---

## 一、当前项目状态

| 项目 | 状态 |
|------|:---:|
| Phase 0: 仿真框架 | ✅ 9/9 完成 |
| ROS Package | 6 个，全部可编译 |
| 单元测试 | 56/56 |
| E2E 测试 | 33/33 |
| Git Tag | `v0.1.0`, `milestone/sim-framework-done` |
| 用户物理位置 | 海外（仅持华为轻薄本 Ubuntu 20.04.6，无独显，无硬件） |

## 二、Phase 1 硬件目标（用户回国后）

| 平台 | 核心硬件 |
|------|---------|
| 无人机 | F450/S500 + Pixhawk 6C + D435i + M8N GPS + 3DR 数传 |
| 车机-差速 | 树莓派5 + MSPM0G3507 (TI 电赛合规) + 520 编码器电机 |
| 车机-麦轮 | 树莓派5 + STM32F407VET6 + R5 麦轮底板 + 520 电机 |
| 传感器 | RPLIDAR A1 / OpenMV / 4×HC-SR04 / ICM42688 |
| 服务器 | 实验室 GPU 服务器（VLM/SLAM 推理） |

## 三、可提前启动的软件工作（无需硬件 + 无需 Ubuntu 20.04）

### 🥇 优先级 1：STM32F407 麦轮固件

```
功能: 逆运动学解算 + 四轮独立 PID + 与树莓派串口通信协议
环境: 任意 OS + arm-none-eabi-gcc (ARM GCC Toolchain)
验证: CI 可自动编译 + 单元测试（Mock 电机反馈）
依赖: 无硬件
参考: src/air_ground_car_bringup/scripts/mecanum_controller.py (仿真版运动学)
```

### 🥈 优先级 2：MSPM0G3507 差速固件

```
功能: 编码器读取 + 左右轮 PID + 串口协议（TI 电赛合规要求）
环境: 任意 OS + TI Code Composer Studio 或 GCC for MSPM0
验证: CI 交叉编译
依赖: 无硬件
参考: src/air_ground_car_bringup/urdf/car_base.urdf.xacro (差速运动学参数)
```

### 🥉 优先级 3：树莓派部署方案

```
1. Dockerfile — 容器化 ROS Noetic + 项目节点
2. systemd 服务 — 开机自启 drone/car edge nodes
3. 网络配置脚本 — 静态 IP / VLAN / 3DR 数传参数
4. SSH 加固脚本 — 密钥认证 / fail2ban / 最小权限
环境: 任意 OS（纯文本/Dockerfile）
验证: Docker build 可在 CI 中执行
依赖: 无硬件
```

### 其他可并行工作

| 工作 | 说明 | 环境要求 |
|------|------|:---:|
| MAVLink 2 签名配置 | 密钥生成脚本 + MAVLink 签名模式参数模板 | 任意 OS |
| CI 交叉编译流水线 | 扩展 `.github/workflows/ci.yml`，添加 ARM + MSPM0 编译 job | 任意 OS |
| 实机传感器驱动骨架 | RPLIDAR/ICM42688/HC-SR04 的 ROS node（接口从 Gazebo plugin → 真实串口/I2C/GPIO） | 任意 OS 编码 |
| IMU/相机标定脚本 | 标定数据采集与处理脚本（ROS bag + Kalibr 工具链） | 任意 OS 编码 |

## 四、Ubuntu 20.04 依赖判断

| 场景 | 是否需要 Ubuntu 20.04 | 替代方案 |
|------|:---:|------|
| Gazebo + PX4 SITL 完整仿真 | ✅ 必须 | 无 |
| STM32/MSPM0 固件编译 | ❌ | 任意 OS + ARM GCC |
| Python ROS 节点编写 | ❌（编码） / ✅（测试） | CI 容器替代本地测试 |
| `catkin build` 编译验证 | ✅ | Docker 容器 / GitHub Actions CI |
| MAVLink 工具链 | ⚠️ 部分 | `pymavlink` 跨平台 |

**核心结论**: Phase 1 中 80% 的代码工作不需要 Ubuntu 20.04。用户当前轻薄本已完成 Phase 0 使命，可暂放。CI + Docker 替代本地环境。

## 五、给 Subagent 的执行建议

如果你被指派执行 Phase 1 的某个任务，请注意：

1. **固件项目放在 `src/firmware/`**（与 ROS package 同级，非 ROS 包）
2. **遵循 CONVENTIONS.md**，提交用 Conventional Commits + 简体中文
3. **分支从 `main` 拉 `feat/phase1-<description>`**（当前 `feat/task-XX` 即将合入 main）
4. **固件必须有 CI 编译验证**（`.github/workflows/` 中扩展 ARM/MSPM0 toolchain job）
5. **接口对齐 ICD.md**：固件串口协议 → Layer 2 Bridge → Layer 3 抽象消息，不绕过架构
6. **测试策略**：固件用单元测试 + Mock 硬件；ROS 节点用 rosbag 回放 + CI 容器测试
7. **文档**：每块固件写一个 `README.md`（引脚定义表 + 协议帧格式 + 编译命令）

---

*本报告供 AI Agent 间上下文传递使用。如需更详细的技术规格，请查阅 `project-prometheus-tasks/ICD.md` 和 `project-prometheus-tasks/PLATFORM.md`。*
