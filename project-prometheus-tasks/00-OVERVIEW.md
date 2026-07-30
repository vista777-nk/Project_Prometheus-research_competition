# 空地联合具身智能研究平台 — 总索引

> 本文档是项目入口。详细内容已拆分为四个专题文件。
>
> **当前进度**：Phase 0 仿真框架 9/9 ✅ · Phase 1 基础设施 3/6 🟡 · 56/56 ROS 单元测试通过 · E2E 33/33 通过 · 固件 Host 测试 133/133 通过（麦轮 63 + 差速 70）· 部署静态校验 21/21 通过（含 31 个 Host 用例）· 6 个 ROS package 全部可编译

## 📖 必读文件

| 文件 | 内容 | 更新频率 |
|------|------|:---:|
| [**RESEARCH_PHILOSOPHY.md**](./RESEARCH_PHILOSOPHY.md) | 设计哲学与科研宪法（五条不可违反的原则 + ADR 规范） | 🟢 极少 |
| [**PLATFORM.md**](./PLATFORM.md) | 平台架构（分层、包结构、通信、部署） | 🟢 很少 |
| [**ROADMAP.md**](./ROADMAP.md) | 研究路线（EQA → 竞赛 → World Model → 毕设） | 🔴 经常 |
| [**ICD.md**](./ICD.md) | 接口控制文档（Observation / Mission / WorldState / Capability） | 🟡 稳定 |

## 📋 实施任务清单

### Phase 0: 仿真框架（已完成 ✅）

| 任务 | 内容 | 依赖 | 预计耗时 | 状态 |
|------|------|------|---------|:---:|
| [task-01](./task-01-env-setup.md) | 环境搭建 + ROS 工作空间脚手架 | 无 | 1h | ✅ |
| [task-02](./task-02-drone-sitl.md) | PX4 SITL 无人机仿真 + 传感器插件 | task-01 | 2h | ✅ |
| [task-03](./task-03-diff-chassis.md) | 差速底盘仿真（电赛规格） | task-01 | 2h | ✅ |
| [task-04](./task-04-mecanum-chassis.md) | 麦轮底盘仿真 + 模块化切换 | task-03 | 2h | ✅ |
| [task-05](./task-05-sensors.md) | 车载传感器与二维云台 | task-03,04 | 1.5h | ✅ |
| [task-06](./task-06-com-bridge.md) | 空地通信桥（MAVLink + TCP） | task-02,03 | 2h | ✅ |
| [task-07](./task-07-edge-server.md) | 边缘预处理 + 服务器节点 | task-02~06 | 2h | ✅ |
| [task-08](./task-08-integration.md) | 集成总装 Launch + Makefile | task-02~07 | 1.5h | ✅ |
| [task-09](./task-09-validation.md) | 仿真验证 + 测试脚本 | task-08 | 1h | ✅ |

### Phase 1: 基础设施 (Infrastructure Phase) — 当前 🔴

> **定位**：不是"固件阶段"，也不是"部署阶段"，而是在建设整个研究平台的 Infrastructure。  
> Phase 2 起开始 Robot Intelligence · Phase 3 起开始 Embodied Intelligence。  
> **总原则**：先固件，后上机；先接口，后算法；先可复现，后联调。  
> **环境要求**：80% 代码工作不需要 Ubuntu 20.04，CI + Docker 替代本地环境。

| 任务 | 内容 | 战线 | 预计耗时 | 状态 |
|------|------|:---:|---------|:---:|
| [task-10](./task-10-stm32-mecanum-firmware.md) | STM32F407 麦轮固件（逆运动学 + PID + 串口协议） | 🥇 固件先行 | 6h | ✅ |
| [task-11](./task-11-mspm0-diff-firmware.md) | MSPM0G3507 差速固件（编码器 + PID + 电赛合规） | 🥇 固件先行 | 5h | ✅ |
| [task-12](./task-12-drone-firmware-and-rpi-deployment.md) | 树莓派部署方案（Docker + systemd + 网络 + SSH） | 🥈 部署先行 | 4h | ✅ |
| [task-13](./task-13-ci-pipeline.md) | CI 交叉编译流水线（ARM + MSPM0 + Docker + Lint） | 🥉 验证先行 | 3h | 🔴 |
| [task-14](./task-14-sensor-drivers-mavlink.md) | 实机传感器驱动骨架 + MAVLink 2 签名 | 🥉 验证先行 | 4h | 🔴 |
| [task-15](./task-15-calibration-validation.md) | IMU/相机标定脚本 + Phase 1 集成验证 | 🥉 验证先行 | 3h | 🔴 |

### 依赖拓扑 (Phase 0)

```
task-01 ──┬── task-02 ─────────────┬── task-06 ── task-07 ──┐
          └── task-03 ──┬─────────┘                        │
                         ├── task-04 ──┐                    ├── task-08 ── task-09
                         └─────────────┴── task-05 ─────────┘
```

### 依赖拓扑 (Phase 1)

```
task-10 (STM32 固件) ─────────────┐
                                   ├── task-13 (CI 流水线) ── task-15 (集成验证)
task-11 (MSPM0 固件) ─────────────┘

task-12 (树莓派部署) ── (独立，无强依赖) ── task-15

task-14 (传感器 + MAVLink) ── (独立) ── task-15
```

> **并行策略**：task-10、task-11、task-12、task-14 可同时分发给 4 个 subagent。task-13 在 task-10/11 固件目录就位后启动。task-15 在所有任务完成后执行集成验证。
>
> **进度更新（2026-07-29）**：task-10、task-11 已完成。`src/firmware/common/`（task-13 §13.1 权威布局）
> 已就位并**被 task-11 一行未改地复用** —— "帧层/PID/CRC 板无关"至此被证明而非仅被声称。
> 串口帧协议由 [ADR-0003](../docs/decisions/ADR-0003.md) 固化，
> TI 电赛合规与移植层分离由 [ADR-0004](../docs/decisions/ADR-0004.md) 固化。
> CI 已有两个固件 job 模板，task-13 不必再做提取重构。
>
> **2026-07-29 补充**：移植层分离（`common/mcu_port.h`）与故障状态机（`common/faults.c`）
> 已回溯应用到 task-10，两块板结构对齐，`bsp.c/.h` 删除。
>
> **2026-07-30 补充**：task-12 完成。`src/deployment/` 已就位，
> 容器化部署与地址/时钟分配分别由 [ADR-0005](../docs/decisions/ADR-0005.md) 与
> [ADR-0006](../docs/decisions/ADR-0006.md) 固化。CI 增至 5 个 job。
> task-14 需交付 `car_edge_real.launch`，在此之前 entrypoint 会自动降级。
>
> ⚠ task-11 的默认构建产出**不是可烧录固件**（MSPM0 移植层默认空实现，
> 原因见 ADR-0004 §决策-3）。算法层完整且有 70 个 Host 用例覆盖；
> 上板需按其 README §7 接入 TI SDK 与 SysConfig。

## 🚀 快速开始

```bash
# 设置环境 (仅首次)
bash ~/air_ground_sim_ws/setup_all.sh

# 编译
cd ~/air_ground_sim_ws && make build

# 一键启动
make launch-full

# Task-02~07 全量回归
make test-all

# Task-09 快速冒烟与完整 E2E
make quick-smoke
make test-e2e
```

## 🔧 给 Subagent 的通用规范

1. **代码风格**：Python 3.8+，PEP 8；ROS 节点使用 `rospy`
2. **注释语言**：英文变量名 + 中文模块 docstring
3. **分层铁律**：Layer 4 (Research) 不 import `mavros` / `gazebo_msgs`
4. **容错**：传感器断连 `rospy.logwarn`，不 crash
5. **轻量化**：仿真传感器默认 ≤ 30Hz，Gazebo headless
6. **可配置**：所有参数从 `config/*.yaml` 读取
7. **带测试**：每个 task 含 `test_*.sh` 验证脚本

---

*版本: v6.3 · 日期: 2026-07-27 · 作者: DeepSeek (经 ChatGPT 、 混元3 、 豆包 、 执行端subagent集群审阅后重构)*
