# 空地联合具身智能研究平台 — 总索引

> 本文档是项目入口。详细内容已拆分为四个专题文件。
>
> **当前进度**：Phase 0 仿真框架 9/9 ✅ · Phase 1 基础设施 6/6 ✅ · **Phase 1.5 实机接入进行中** · 服务器常驻/恢复已验证 · Pi 5 / Debian 13 / 8GB / 64GB 基线已确认 · BOM/五模块边界已冻结（ADR-0018）· 地面车几何/编码器/IMU/电气/RC 基线已冻结（ADR-0019）· STM32 四路 HC-SR04、iBUS/RC 安全状态机和 GPIO 正交解码已实现 · MSPM0 SysConfig、两板第二 UART 接入和全部实物验收待良乡上机

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

### Phase 1: 基础设施 (Infrastructure Phase)（已完成 ✅）

> **定位**：不是"固件阶段"，也不是"部署阶段"，而是在建设整个研究平台的 Infrastructure。  
> Phase 2 起开始 Robot Intelligence · Phase 3 起开始 Embodied Intelligence。  
> **总原则**：先固件，后上机；先接口，后算法；先可复现，后联调。  
> **环境要求**：80% 代码工作不需要 Ubuntu 20.04，CI + Docker 替代本地环境。

| 任务 | 内容 | 战线 | 预计耗时 | 状态 |
|------|------|:---:|---------|:---:|
| [task-10](./task-10-stm32-mecanum-firmware.md) | STM32F407 麦轮固件（逆运动学 + PID + 串口协议） | 🥇 固件先行 | 6h | ✅ |
| [task-11](./task-11-mspm0-diff-firmware.md) | MSPM0G3507 差速固件（编码器 + PID + 电赛合规） | 🥇 固件先行 | 5h | ✅ |
| [task-12](./task-12-drone-firmware-and-rpi-deployment.md) | 树莓派部署方案（Docker + systemd + 网络 + SSH） | 🥈 部署先行 | 4h | ✅ |
| [task-13](./task-13-ci-pipeline.md) | CI 交叉编译流水线（ARM + MSPM0 + Docker + Lint） | 🥉 验证先行 | 3h | ✅ |
| [task-14](./task-14-sensor-drivers-mavlink.md) | 实机传感器驱动骨架 + MAVLink 2 签名 | 🥉 验证先行 | 4h | ✅ |
| [task-15](./task-15-calibration-validation.md) | IMU/相机标定脚本 + Phase 1 集成验证 | 🥉 验证先行 | 3h | ✅ |

### Phase 1.5：实机接入工作包（进行中）

| 工作包 | 服务器侧/代码侧 | 现场关闭条件 | 状态 |
|---|---|---|:---:|
| H1 硬件冻结 | 完整 BOM、五个模块、资源所有权、64 GB Pi 基线 | 核对实物标签与数量 | 🟡 |
| H2 差速模块 | 1560 count/rev 与几何已入库；无效 pinmux 已拒绝；软件正交解码和 RC 安全状态机就绪 | SysConfig/中断接入、离地/负载测试 | 🟡 |
| H3 麦轮模块 | 几何/编码器、TIM8 四路超声波、iBUS/RC 安全状态机已入库 | 第二 UART 接入、接线、横移/负载测试 | 🟡 |
| H4 共享车载载荷 | A2M12 256000；ICM42688 0x69/轴映射；OpenMV/云台边界冻结 | I²C/USB 身份、外参/云台电流、两底盘换装复测 | 🟡 |
| H5 无人机本体/视觉 | TELEM2 MAVROS + D435i 实机 launch；双 CSI 失败关闭 | 无桨、数传、RC、推重比、系留实飞；双相机 profile | 🟡 |
| H6 跨校区链路 | 中关村本地 ROS/systemd、单 TCP 隧道边界 | 获批隧道与断线/恢复实测 | 🟡 |

完整现场表见 [Phase 1.5 实机硬件基线](../docs/experiments/phase-1.5-hardware-baseline.md)。

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
>
> **2026-07-31 补充（task-12 评审收口）**：
> `network_mode: host` 的暴露面与三条 Phase 2 重估触发条件由
> [ADR-0007](../docs/decisions/ADR-0007.md) 固化，触发条件同时挂在
> [ROADMAP §待重估的技术决策](./ROADMAP.md)——不指望有人回来读 ADR。
> 降级运行状态已贯通到健康检查（退出码 4）。
> **2026-08-01 Phase 1.5 收口**：上述假绿灯已由
> [ADR-0015](../docs/decisions/ADR-0015.md) 关闭。`EDGE_MODE=real` 只会传
> `backend:=real` 且失败关闭；`mock` 必须显式选择并由健康检查报告 DEGRADED；
> `sim` 才使用仿真适配器。当前 8 条入口模式用例含 real 不得自动回退、
> `ROS_IP` 缺失必须拒绝启动的负向验证。
> CI 抓出 `StartLimitIntervalSec` 写错段（systemd 会静默忽略），
> 已补一条不依赖 systemd 的段归属自查。
>
> **2026-08-02 Phase 1.5 实机入口推进**：UART / I²C Linux 后端与统一底盘串口桥
> 已落地并有 78 条 Host 用例；真实 ROS 对照证明 mock 路径持续存活、real 缺
> MCU 身份或四路超声波时失败关闭。
> 实验室服务器新增不自连的 `lab-server-real.launch`，在当前
> `192.168.3.30` 上真实监听 `0.0.0.0:9090` 并把 TCP heartbeat 解码为
> `/server/car/state`。机器人隔离网 `192.168.1.100` 仍未配置，见 ADR-0016。
>
> **2026-08-01 跨校区补充**：硬件在良乡、服务器在中关村，ADR-0007 的外网
> 重估条件已触发。ADR-0017 改为服务器本地 ROS + 回环 TCP，由后续受控隧道承载
> 唯一跨校区会话。服务器 Linger 用户 systemd 已启用；required 节点故障注入后
> 自动恢复，11311/9090 只监听回环，ROS 日志迁到 `/data2`。
>
> ⚠ task-11 的默认构建产出**不是可烧录固件**（MSPM0 移植层默认空实现，
> 原因见 ADR-0004 §决策-3）。算法层完整且有 71 个 Host 用例覆盖；
> 上板需按其 README §7 接入 TI SDK 与 SysConfig。
>
> **2026-07-31 补充（task-13）**：CI 增至 6 个 job。但本任务真正新增的只有
> `lint-scripts` 一个 —— 文档要求的另外四个维度，**task-10/11/12 在交付各自
> 功能时已经顺带建好了**。这说明任务文档会过期，执行前必须核对现状而不是照抄
> 「可执行步骤」，偏差已逐条记入 [task-13 §与原方案的偏差](./task-13-ci-pipeline.md)。
>
> 门禁强度按「**是否在本地实测过**」分层，由
> [ADR-0008](../docs/decisions/ADR-0008.md) 固化：shellcheck / yamllint / flake8
> 三项提交前跑过全仓（分别 0/0/0 条）故设为阻塞并钉死版本号；
> cppcheck 一次都没跑过，故只告警且 job 名带 `ADVISORY`。
> 同时清掉两处「永远不会失败的检查」——`validate-deployment` 里 `|| true` 到底的
> shellcheck 步骤，以及 README 顶部硬编码的假 CI badge。
>
> **2026-07-31 补充（task-15，Phase 1 收官）**：标定工具链 + 集成验证就位，
> `scripts/smoke_test_phase1.sh` 61 项本地全绿。**Phase 1 六个任务全部完成**
> （task-14 的状态本来就该是 ✅，这次一并改正）。
>
> 本任务再次印证了 task-13 记下的那条：**任务文档会过期**。task-15 原文里
> 采集脚本的五个话题名在仓库里一个都不存在，冒烟测试查的两处路径/job 名
> 也都对不上，`cv2.FileStorage` 写的 YAML 根本喂不进 ROS。七处偏差逐条记在
> [task-15 §与原方案的偏差](./task-15-calibration-validation.md)，
> 决策记录 [ADR-0011](../docs/decisions/ADR-0011.md)。
>
> 新增一条评审红线（ADR-0011 §方案 G）：**替身可以替环境（ROS、硬件、时钟），
> 不能替被测对象**。原文的 `MockObservation` 测的是它自己那二十行模拟件，
> 现在改成用 task-14 的 ROS 替身跑真的 `car_preprocessor` 和真的 `WorldModelStore`。
> 换过来的当天就抓到一个替身缺陷（`rospy.Duration` 不收位置参数）。
>
> ⚠ 仍未核实、**上机第一件事**要做的四条，见
> [标定 README §5](../src/deployment/calibration/README.md)：
> PX4/MAVROS 的签名参数名（承 ADR-0010）、`camera_info_manager` 是否接受
> 额外键、以及 `/car/openmv/image_raw` 到底有没有发布者。
>
> **2026-08-01 补充（task-15 收尾）**：首轮 CI（run #25 / `fe0cf66`）除
> ARM64 镜像外全部通过，其中 `validate-deployment` 装了 CI 钉的
> numpy<2 + OpenCV 4.x 并真跑了标定流水线 —— numpy 版本差异那笔账结清。
> 为了让"通过"之外还能看到**逐项数值差**（容差留了 3~10 倍余量，
> 系统性偏移可以躲在里面），流水线测试现在会打印实测值与本地基准的对比表。
>
> cppcheck 从 ADVISORY **升为阻塞**（[ADR-0012](../docs/decisions/ADR-0012.md)）。
> 依据不是读日志（job log 要 admin 权限），而是 Actions 的 jobs API 会单独记录
> 每一步的结论：该步骤带 `--error-exitcode=1`，run #24 与 #25 两次退出码都是 0，
> 即**两次零发现**。ADR-0008 当初留的条件是"等它的输出被观测到"，现在兑现了。
>
> 升级时的取舍值得记：cppcheck 是四个阻塞工具里唯一**不钉版本**的
> （没有官方预编译 Linux 二进制，钉版本要源码构建，每次 CI 加 3~6 分钟）。
> ADR-0008 要求钉版本的**目的**是让人能区分"代码退化了"和"工具升级了"——
> 这里改用报错文案达成同一目的，代价（判断从自动降级成人读一行）写在 ADR 里。
>
> **2026-08-02 BOM 同步**：ADR-0013 已决定 HC-SR04 由各底盘 MCU 定时并通过
> `0x14` 上报；ADR-0018 冻结五个物理模块。旧 Pi GPIO 驱动已删除，DRV8871、
> A2M12、M9N、F450/A2212/BL32/1045 和可换无人机视觉载荷已进入代码/部署。
> 剩余欠账是物理 pinmux、MC520P30 参数、组装几何、IA6B、PX4 参数、USB 身份、
> 双 CSI profile、供电/电池和跨校区隧道，均只能用实物关闭。

> **2026-08-03 英文答复同步**：ADR-0019 将 MC520P30（1560 count/rev、360RPM
> 空载）、两底盘几何、ICM42688 0x69/安装轴、OpenMV 云台边界、HC-SR04 分压/轮询、
> IA6B 安全仲裁和供电门禁写入权威基线。STM32 四路 TIM8 捕获已实现；MSPM0 旧表
> 错把 TIMG7 当 QEI，现由真实构建 `#error` 阻止误烧，直到 SysConfig 与 GPIO 软件
> 正交解码完成。无人机整套未到货，继续失败关闭，不从地面车参数外推。

> **2026-08-03 服务器离场收口**：GitHub 公共 API 证实 `task-new` 的 CI run 为 0；
> 根因是 `push.branches` 只允许 `main/feat/*/fix/*`。现改为任意分支 push 触发，
> Phase 1 冒烟增加触发契约守卫，当前基线 65/65。中关村服务器服务/timer active，
> 五个服务节点健康，11311/9090 仅回环；根分区 97%，大数据继续只写 `/data2`。
> 旧 AI/CLAUDE 交接已归档，明天从
> [Raspberry Pi AI 交接](../docs/experiments/AI_HANDOFF.md) 开始，审计证据见
> [服务器收口报告](../docs/experiments/phase-1.5-server-readiness-2026-08-03.md)。

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

*版本: v6.9 · 日期: 2026-08-03 · v6.9：服务器离场收口、全分支 CI 与 Pi 交接同步*
