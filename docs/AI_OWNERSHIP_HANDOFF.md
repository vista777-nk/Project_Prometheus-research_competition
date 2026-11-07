# AI 全权负责交接书 — Project Prometheus

> **版本**：v1.0 · **日期**：2026-10-30
> **生效范围**：自本日起，本仓库的日常研发、测试、文档与 CI 由 **DeepSeek** 与 **Claude** 两个 AI 共同负责。
> 人类负责人（@vista777-nk）保留**硬件上电、飞行、经费、发布、对外投稿**的最终决定权。
>
> **与另一份交接文档的分工**：
> - 本文 = **项目级接手章程**：状态、边界、路线、协作协议、历史教训。
> - [`docs/experiments/AI_HANDOFF.md`](./experiments/AI_HANDOFF.md) = **良乡 Pi 现场执行手册**：上机顺序、停止条件、H1~H13 缺失事实。
>
> 两份文档冲突时，以**本文的规则**为准，以现场手册的**硬件事实**为准。

---

## 0. 三十秒版本

Project Prometheus 是一套**空地联合具身智能研究平台**：PX4 无人机 + 双底盘（差速 / 麦轮）地面车 + 实验室服务器，用 ROS Noetic 连接，仿真优先、接口优先、模块可替换。

- **Phase 0（仿真框架）9/9 完成**，**Phase 1（固件 + 部署）6/6 完成**。
- **Phase 1.5（实机接入）进行中**：代码侧与自动化证据基本齐备，**硬件侧尚未闭环**。
- 仓库最后一次提交是 `0ae6d04`（2026-08-03），当前分支 `dev`，工作区干净。
- 换言之：**软件先行的部分已经做完，接下来卡在实物、接线、供电和跨校区链路上。**

---

## 1. 项目是什么，不是什么

**是**：

- 一套寿命长于单篇论文的研究基础设施（Research Infrastructure）。
- 一个接口稳定、模块可替换的插件式机器人平台。
- 一条从仿真到实机、从 EQA 到世界模型的研究路线。

**不是**：

- 不是"跑通一个 demo"的竞赛工程。
- 不是"仿真里能跑就等于实机可用"的项目。
- 不是"文档写完就算完成"的项目——本仓库对"已完成"的定义是**有可复现的证据**。

五条宪法原则（详见 [`RESEARCH_PHILOSOPHY.md`](../project-prometheus-tasks/RESEARCH_PHILOSOPHY.md)）：

1. **Platform First** — 平台比论文寿命更长。
2. **Interface Before Implementation** — 先 ICD，再 Package，再代码。
3. **Everything Produces Knowledge** — 输出 Knowledge，不是 Raw Data。
4. **Simulation is the First Robot** — 仿真是第一台机器人，不是"假的"。
5. **Every Module Must Be Replaceable** — 稳定的是 Capability，不是 Algorithm。

---

## 2. 当前真实状态（截至 2026-10-30）

> ⚠ 以下数字与结论来自 **2026-08-03 的提交、`Research_Diary.md` 与 CI 记录**。
> 2026-10-30 本次接手**没有**在 Ubuntu / Pi / 硬件上重新实测。重新引用前请先复跑，见 §5。

### 2.1 阶段状态

| 阶段 | 状态 | 说明 |
|------|:---:|------|
| Phase 0 仿真框架 | ✅ 9/9 | 无人机 SITL、双底盘、传感器、通信桥、边缘服务器、集成、验证 |
| Phase 1 基础设施 | ✅ 6/6 | STM32 麦轮固件、MSPM0 差速固件、Pi 部署、CI、传感器驱动骨架 + MAVLink 签名、标定与集成验证 |
| **Phase 1.5 实机接入** | 🟡 **进行中** | 代码、部署、静态门禁已就位；实物验收未完成 |
| Phase 2 EQA | 🔴 未开始 | 原计划 2026.09 启动 |
| Phase 3 竞赛 | 🔴 未开始 | 2027 电赛 / CRAIC |
| Phase 4 毕设 | 🔴 未开始 | 3DGS / World Model / Dreamer |

### 2.2 证据分级（接手后请沿用这套说法）

| 等级 | 含义 | 例子 |
|------|------|------|
| **A. 有自动化证据** | 本机或 CI 实跑过，有退出码与计数 | ROS 单元测试 82/82、E2E 33/33、固件 Host 151/151、传感器 Host 81/81、Phase 1 冒烟 65/65、部署校验 55/55 |
| **B. 只有静态/结构证据** | 脚本能解析、配置能通过校验，但没在目标机执行 | systemd unit、udev 规则、Dockerfile、MAVLink 签名模板 |
| **C. 只有代码/设计证据** | 代码存在、逻辑有测试，但从未接触真实硬件 | MSPM0 移植层、IA6B 状态机、HC-SR04 捕获、双 CSI 入口 |
| **D. 未知 / 未验证** | 没有证据，只有计划或推测 | Pixhawk 参数、ESC 校准、推重比、跨校区隧道、时钟偏差 |

**规则**：写"完成"只能引用 A 级；B/C 级必须写明"未在实机验证"；D 级必须写"未知"。
**SKIP ≠ PASS。** 这条在 2026-08 被反复强调，是仓库里最容易犯的错。

**2026-11-06 本机复测（Windows，Git Bash，commit 50751f7）**：`make smoke-phase1` 通过 64 / 失败 0 / 跳过 1（缺 pymavlink）；STM32 Host 80/80；MSPM0 Host 71/71。`validate.sh` 为通过 51 / 失败 2 / 跳过 2，失败集中在 `test_entrypoint_modes.py` 与 `test_server_deployment.py`，已登记为 CLAIMS T-007，疑为 Windows 路径传给 bash 导致，尚未修复。**因此本机的部署校验结果不能作为通过证据**，以 Linux / CI 为准。

### 2.3 时间线偏差（必须让负责人知道）

`ROADMAP.md` 原计划：

- 2026.08：Phase 1.5 实机组装与对齐；
- 2026.09：Phase 2 EQA 系统初步实现；
- 2026.10~12：EQA 迭代 + 论文初稿。

**现实**：2026-08-03 之后代码没有新提交；2026-10-30 与 2026-11-06 只新增了接手、协作与仓库卫生文档。Phase 1.5 的硬件阻塞项（无人机未到货、MSPM0 SysConfig 未接入、IA6B 第二 UART 未接、供电未测）均未关闭。

**因此**：Phase 2 不应按原日历"自动开始"。要么由负责人重排时间线，要么**先用仿真推进 Phase 2 的可离线部分**（VLM 接口、SLAM 占位、World Model 数据通路），但不得把仿真结果写成实机结论。这条判断应在第一次正式开工时向负责人确认。

### 2.4 分支现状（2026-11-06 已整理）

- **集成分支**：`dev`（`origin/HEAD` 指向它，比 `main` 多 103 个提交）。
- **`main`**：已快进到与 `dev` 一致，作为稳定线保留；后续发版仍从 `main` 打 tag。
- **已合并并删除的临时分支**：`task-new`、`docs/ai-takeover-protocol`、`chore/repo-hygiene`。
- **`task-old`**：仅存在于远端，含 5 个未合入 `dev` 的提交（一次 revert 与三份旧文档）。2026-11-06 保留不动；如负责人决定删除，先记录 SHA `b0fb48e`。
- **待固化**：`CONVENTIONS.md` 仍写 `main` 为稳定主线、`feat/*` 为开发分支。分支模型（集成分支、PR 目标、谁能点合并）由 **ADR-0020** 固化，编号已在 CLAIMS T-001 登记。

---

## 3. 仓库地图

### 3.1 必读顺序（约 1.5 小时）

| 顺序 | 文件 | 读它是为了 |
|:---:|------|-----------|
| 1 | `README.md` | 项目定位、当前进度、快速导航 |
| 2 | `CONVENTIONS.md` | 分支、提交、命名、编码规范 |
| 3 | `project-prometheus-tasks/RESEARCH_PHILOSOPHY.md` | 五条宪法 + ADR 规范 |
| 4 | `project-prometheus-tasks/PLATFORM.md` | 分层、包结构、通信拓扑、TF、时钟 |
| 5 | `project-prometheus-tasks/ICD.md` | Observation / RobotState / WorldState / Mission / Capability |
| 6 | `project-prometheus-tasks/ROADMAP.md` | 研究路线、Phase 2 前待重估决策 |
| 7 | `docs/architecture/capability_matrix.md` | 每个 Capability 的 Provider / Interface / Consumer / 可替换性 |
| 8 | `docs/experiments/phase-1.5-hardware-baseline.md` | 硬件事实的权威来源 |
| 9 | `docs/experiments/AI_HANDOFF.md` | 现场执行顺序与停止条件 |
| 10 | `Research_Diary.md`（末尾 300 行） | 最近的决策与踩坑 |
| 11 | `docs/decisions/ADR-0001~0019` | 为什么是这样，而不是别样 |

### 3.2 目录职责

| 路径 | 职责 | 可改性 |
|------|------|:---:|
| `project-prometheus-tasks/` | 任务书 + 平台/ICD/路线 | 可改，但**任务书是历史方案**，偏差要记在任务文档的"与原方案的偏差"节 |
| `docs/decisions/` | ADR | **只增不改**；旧 ADR 只能被新 ADR 取代或补充 |
| `docs/architecture/` | 能力矩阵 | 可改，改后要更新审查记录 |
| `docs/experiments/` | 实验记录 + 现场交接 | 可增；已有记录**不得删改失败尝试** |
| `src/air_ground_*` | 6 个 ROS 包 | 可改，遵守分层铁律 |
| `src/firmware/` | 下位机固件（非 ROS） | 可改，必须有 Host 测试 |
| `src/deployment/` | Pi / 服务器部署 + 标定 | 可改，必须先过 `validate.sh` |
| `obsolete-documentation/` | 历史归档 | **只读**，不要恢复成 live 文档 |
| `imu-icm42688/` 等 6 个硬件资料目录 | 厂商原始资料 | **只读**，子目录中文名保持原样 |
| `.github/workflows/ci.yml` | 7 个 CI job | 可改，但门禁强度变更要有 ADR |

### 3.3 不要提交的东西

- `build/`、`devel/`、`install/`（Catkin 生成物）
- 固件编译产物（`*.o`、`*.elf`、`*.bin`、`*.hex`、`*.map`）
- 私钥、MAVLink 密钥、`.env`、MAC/序列号、校园网临时 IP
- 离线镜像包与 bag（只写服务器 `/data2`）

---

## 4. 不可违反的规则

### 4.1 分层铁律

```
Layer 4 Research  → 只依赖 air_ground_interfaces
Layer 3 Abstraction → Observation / RobotState / WorldState / Mission / Capability
Layer 2 Bridge    → MAVLink ↔ ROS、ROS ↔ TCP JSON、传感器 ↔ Observation
Layer 1 Hardware  → PX4 / STM32 / MSPM0 / 传感器
```

**Layer 4 永远不允许 import** `mavros`、`gazebo_msgs`、`sensor_msgs/LaserScan` 等硬件相关包。
违反这条的 PR 一律打回，不管功能多好用。

### 4.2 证据纪律（本仓库最重要的一条）

1. **没有实测，就不写"完成"。** 写"已实现 / 未上机验证"。
2. **SKIP 不是 PASS。** 校验脚本跳过某项时必须显式报 SKIP 并说明原因。
3. **一个从未红过的检查，等于没有检查。** 新增校验脚本必须自己做一次负向测试：故意塞入错误，确认它会变红。
4. **失败尝试不得从记录中删除。** 包括 CI 红、硬件冒烟、Ogre 崩溃。
5. **注释不是验证。** 写"这里做了 X"之前，先问"有没有一个测试能证明 X"。
6. **不要把"能启动"当成"功能正确"。** 进程活着、话题有数据、健康 JSON ok，都不等于物理量正确。

### 4.3 变更流程

1. 从当前集成分支切 `feat/<desc>`、`fix/<desc>`、`docs/<desc>`、`exp/<desc>`。
2. 提交信息用 **Conventional Commits + 简体中文**，正文写实测数字、硬件状态、仍未验证项。
3. 涉及架构决策 → 新增 ADR（编号递增，不修改旧 ADR）。
4. 涉及任务文档偏差 → 在任务文档末尾追加"与原方案的偏差"，逐条写理由。
5. 合并前至少跑：`git diff --check`、`make smoke-phase1`、`bash src/deployment/validate.sh`，以及受影响模块的 Host 测试。
6. 合并后确认**当前分支的 GitHub CI 实际出现且全绿**——"没有红灯"不等于"跑了且全绿"。

### 4.4 安全红线（不可由 AI 自行放宽）

- 地面车第一次上电：**架空车轮、电源限流、急停可触达**；未确认编码器方向、RC failsafe、串口看门狗前不得落地。
- 无人机：**一律先拆桨**；未经负责人批准不得系留或实飞。
- `EDGE_MODE=real` 缺设备时**失败关闭是正确行为**，禁止自动回退到 mock。
- 服务器 ROS Master（11311）与 TCP 9090 **只监听 `127.0.0.1`**；跨校区只跨一条经批准的受控 TCP 隧道，不跨 WAN 跑 ROS 图。
- 未确认的 pinmux、PX4 参数、USB 身份、网络地址**不得写进生效配置**。
- 不提交密钥、私钥、`.env`、设备序列号、MAC、校园网临时 IP。

### 4.5 AI 的决策边界

| AI 可自主决定 | 必须人类确认 |
|--------------|-------------|
| 代码实现方式、测试设计、文档措辞 | 硬件接线、上电、飞行、拆桨/装桨 |
| 分支内重构、Host 测试补充 | 修改既有 ADR 的结论 |
| CI 内部步骤顺序、缓存策略 | 改变门禁强度（阻塞 ↔ 告警） |
| 新增实验记录、更新能力矩阵 | 打 tag、发版、合并到集成分支（按负责人偏好） |
| 起草 ADR | 对外投稿、竞赛报名、经费与采购 |

---

## 5. 运行手册

### 5.1 三种环境，先确认你在哪一种

| 环境 | 能跑什么 | 不能跑什么 |
|------|---------|-----------|
| **Windows 开发机**（当前工作区） | 固件 Host 测试、Phase 1 冒烟、部署静态校验、文档、`git` | ROS 测试、Gazebo、Docker ARM64、systemd |
| **Ubuntu 20.04 实验室服务器**（中关村，`192.168.3.30`） | `catkin build/test`、E2E、Gazebo headless、真实 TCP 监听 | 硬件串口/I²C（无设备） |
| **Raspberry Pi 5**（良乡，Debian 13） | 宿主预检、Docker、systemd、udev、真实传感器 | ROS 裸装（必须跑 Focal ARM64 容器） |

> 本工作区的 PowerShell 有异常：`Get-ChildItem`、`cd` 等内置 cmdlet 可能不可用。
> 可改用 .NET API（`[System.IO.Directory]::GetFiles`）或 `cmd /c`。这不影响仓库本身。

### 5.2 命令清单

```bash
# —— 任意环境（不需要 ROS）——
make smoke-phase1          # Phase 1 交付物存在性 + 语法 + 自测 + CI 归属
make validate-deployment   # 部署/标定配置静态校验，与 CI 同一份脚本
cd src/firmware/stm32_mecanum && make test   # STM32 Host 测试，只需 gcc
cd src/firmware/mspm0_diff    && make test   # MSPM0 Host 测试，只需 gcc

# —— Ubuntu 20.04 / ROS 环境 ——
make build                 # catkin build 全部 6 个包
make test-all              # Task-02~07 全量回归
make test-unit             # catkin test
make quick-smoke           # 5 项快速冒烟
make test-e2e              # 33 项端到端
make launch-full           # 完整仿真
make status / make kill    # 查看 / 清理

# —— 树莓派 ——
python3 src/deployment/healthcheck/check_pi_host.py --stage base --json
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car
bash src/deployment/install.sh --role car --dry-run
sudo bash src/deployment/install.sh --role car
bash src/deployment/validate.sh

# —— 服务器 ——
systemctl --user status air-ground-lab-server.service
python3 src/deployment/server/check_lab_server.py --json
```

### 5.3 最后一次记录的基线数字（引用前请复跑）

| 项目 | 数字 | 来源 |
|------|------|------|
| ROS 单元测试 | 82/82 | 2026-08-02 日记 |
| E2E | 33/33 | 2026-08-02 |
| Quick Smoke | 5/5 | Task-09 |
| Phase 1 冒烟 | 65/65 | 2026-08-03 |
| 部署校验 | 55/55 | 2026-08-03 |
| STM32 固件 Host | 80/80 | 2026-08-03 |
| MSPM0 固件 Host | 71/71 | 2026-08-03 |
| 传感器 Host | 81/81 | 2026-08-03 |
| CI job 数 | 7 | `.github/workflows/ci.yml` |
| Windows 本机冒烟 | 64 通过 / 0 失败 / 1 跳过（缺 pymavlink） | 2026-11-06，CLAIMS 基线记录 |
| Windows 本机部署校验 | 51 通过 / 2 失败 / 2 跳过（路径问题，T-007） | 2026-11-06，**不可作为通过证据** |
| Ubuntu 服务器 catkin 单元测试 | 82/82 | 2026-11-06，commit 1240643，干净重建 `devel/` |
| Ubuntu 服务器运行时测试 | test-diff 7、test-mecanum 17、test-sensors 28、test-bridge 11、test-server 15，共 78 项，0 失败 | 同上，各目标单独运行 |
| Ubuntu 服务器 Phase 1 冒烟 / 部署校验 | 65/0/0；55/0/0 | 同上；PATH 需 `/usr/bin` 在前，否则缺 pymavlink，签名自测会 SKIP |
| test-drone / quick-smoke / test-e2e | **未运行**（缺 PX4 v1.14 SITL，T-011）；8 月的 5/5、33/33 未复现 | 2026-11-06；`make test-all` 整体没跑完，不能称绿 |
| GitHub CI（`dev` 头 1240643） | 3 次运行均红：只有 Phase 1 Smoke job 的依赖安装步骤失败，其余 6 个 job 通过；根因未确认（T-009） | 2026-11-06，经公开 API 读取，没有 job 日志 |

> 2026-08-03 在受限沙箱里，部署校验曾出现 53/0/2、冒烟 62/0/3——**这是环境缺依赖导致的 SKIP，不是回归**。复跑时先确认依赖齐全（`pymavlink`、`numpy`、`OpenCV`、`systemd-analyze` 可用）。
> 2026-11-06 的 Windows 结果只证明这些用例**在本机没有检查到任何东西**，不证明代码退化；部署校验以 Linux / CI 为准。

---

## 6. 未完成工作

### 6.1 Phase 1.5 的出口条件（全部满足才能关闭）

1. H1~H13 硬件事实全部由实物证据关闭（见 §6.2）。
2. 两套底盘控制模块各自完成：编码器方向、1560 counts/rev、急停、500 ms 看门狗、RC failsafe。
3. 共享车载载荷在两底盘间换装后，PONG 身份与全部传感器健康检查通过。
4. 无人机完成无桨验收、推重比测量，并经负责人批准进入系留/保护区实飞。
5. 跨校区受控隧道获批，断线/恢复实测通过，三机时间偏差有数据。
6. 所有实验记录进入 `docs/experiments/`，每条写明"验证了什么"与"没有验证什么"。

### 6.2 硬件阻塞清单（摘要，权威版本在 `docs/experiments/AI_HANDOFF.md` §9）

| ID | 缺失事实 |
|---|---|
| H1 | 两台 64 GB 卡实际容量、镜像与恢复演练 |
| H2 | 5 V/5 A 降压模块满载/纹波/瞬态/温升 |
| H3 | 云台独立 BEC 容量与舵机堵转电流 |
| H4 | 两底盘满载质量、重心、编码器方向与漏计数 |
| H5 | MSPM0 SysConfig、GPIO 双边沿中断、无冲突 pinmux |
| H6 | STM32 TIM8 四路 Echo 的真板示波器证据 |
| H7 | 三套 IA6B 的通道、端点、failsafe、独立绑定 |
| H8 | ICM42688 真实总线/轴符号；各设备 VID/PID/序列号 |
| H9 | A2M12 / ICM42688 / OpenMV 相对底盘外参 |
| H10 | OpenMV 生产脚本与 `/car/openmv/image_raw` 是否有真实发布者 |
| H11 | D435i 实际连接形态，或双 CSI 型号/端口/profile |
| H12 | Pixhawk 固件、TELEM1/2、M9N、数传、ESC、RC、签名参数 |
| H13 | F450 重量/重心/推重比/低压阈值 |
| N1 | 良乡实验网地址、中关村入口、隧道与授权方式 |
| N2 | 隧道断线/重连、积压、三机时钟偏差 |

### 6.3 代码侧欠账（不需要硬件就能推进）

1. **MSPM0 真实构建剖面**：SysConfig 工程 + GPIO 双边沿正交解码接入；移除编译期 `#error` 门禁。默认 `ci-link` 产物**不可烧录**，这是故意的（ADR-0004 §决策-3）。
2. **两 MCU 第二 UART 接入控制环**：iBUS 解析器与 RC/failsafe 状态机已有 Host 测试，但第二 UART 尚未进控制环。
3. **分支模型 ADR**：`dev` vs `main` 的偏差需要一份 ADR 固话。
4. **`/car/openmv/image_raw` 契约**：若无发布者，不要伪造话题；写 ADR 记录实际能力。
5. **Phase 2 离线骨架**：VLM 接口、SLAM 占位、World Model 新鲜度策略——可在仿真中推进，但必须标"仿真证据"。

---

## 7. 下一步建议（按风险递增）

### 第 0 步：恢复可信基线（1 天，无硬件）

1. 在 Ubuntu 服务器跑：`make build` → `make test-all` → `make quick-smoke` → `make test-e2e` → `make smoke-phase1` → `bash src/deployment/validate.sh`。
2. 记录**真实** PASS/FAIL/SKIP 数，与 §5.3 对比；有差异先查环境，不要先改代码。
3. 确认 `dev` 分支 CI 真实运行且全绿。
4. 把结果追加到 `Research_Diary.md`，注明日期与 commit。

### 第 1 步：关闭代码侧欠账（3~5 天，无硬件）

- MSPM0 SysConfig 工程化（可先在没有板子的情况下把工程结构、pinmux 表、ISR 骨架做完，Host 测试补上）。
- 两 MCU 第二 UART 接入控制环 + 负向用例（丢帧、超时、failsafe）。
- 分支模型 ADR、OpenMV 契约 ADR 草案。

### 第 2 步：良乡台架（按 `AI_HANDOFF.md` §4~§7）

严格按 **MCU → 单个传感器 → 预处理 → 全链路** 的顺序；用 `car_edge_real.launch` 的 `enable_*` 参数隔离被测设备。每关闭一个 H 项，写一条实验记录。

### 第 3 步：无人机（到货后）

无桨验收 → 推重比 → 系留 → 保护区实飞。**参数不得从地面车外推。**

### 第 4 步：Phase 2

只有 Phase 1.5 出口条件满足后才启动实机 EQA；在那之前只做仿真与离线部分。

---

## 8. 双 AI 协作协议（DeepSeek + Claude）

> 正式细则见 [`ai-collaboration/PROTOCOL.md`](./ai-collaboration/PROTOCOL.md) 与认领表 [`ai-collaboration/CLAIMS.md`](./ai-collaboration/CLAIMS.md)。
> 本节是摘要；冲突时以 PROTOCOL 与 ADR 为准。

### 8.1 基本原则

1. **仓库是唯一事实源。** 聊天记录不算证据，代码、ADR、实验记录、日记才算。
2. **对等权限，按任务认领。** 不预设"谁负责固件、谁负责算法"；谁认领谁负责，认领写进提交信息或日记。
3. **同一时间只有一个 AI 写同一条分支。** 分支即锁；避免并发覆盖。
4. **冲突时的优先级**：`ADR` > 本文 > 任务文档 > 聊天中的口头约定。
5. **不互相"修好算了"。** 发现对方留下的问题，先写清楚现象与证据，再改；不要在没理解原因前重写。

### 8.2 每次会话的固定动作

**开始**：

```bash
git status --short --branch
git log --oneline -5
```

- 读本文 §2（状态）与 §7（下一步）。
- 确认本次要动的模块与环境。
- 若涉及硬件，先读 `docs/experiments/AI_HANDOFF.md` 对应章节。

**结束**：

- 更新 `Research_Diary.md`：发现 / 完成 / 失败 / 小结 / 下一步。
- 若状态或阻塞变化，更新本文 §2 与 §6。
- 提交信息写清：改了什么、实测数字、**没验证什么**。

### 8.3 交接给另一个 AI 时

不要转述聊天。给对方三样东西：

1. 当前分支 + commit SHA；
2. 本次改动的文件清单与未完成项；
3. 复现命令与实测结果（含失败尝试）。

### 8.4 禁止事项

- 禁止两个 AI 同时改 `docs/decisions/` 下的同一份 ADR。
- 禁止在未运行测试的情况下声称"已修复"。
- 禁止把 SKIP 写成 PASS，或把"进程启动"写成"功能验证"。
- 禁止删除失败记录、CI 红记录、硬件异常记录。

---

## 9. 已知坑（血泪清单，按出现频率排序）

1. **假绿灯**：`privileged: true` 配 devices 白名单、恒为真的 USB3 检查、只打印不设置的电台脚本、注释里根本不存在的 `PartOf`。共同点：**看起来做了，其实没做**。比"缺了"更危险。
2. **从未红过的检查**：校验脚本必须做负向测试。MSYS `grep` 会静默剥掉 CR，导致 CRLF 检查在 Windows 上永远绿、在 Linux CI 上才红。
3. **CRLF 杀手**：systemd / udev / sshd / Dockerfile 逐行解析，CRLF 会让它们在 Linux 上直接失效。`.gitattributes` 必须作为仓库第一批提交。
4. **Git for Windows 中文编码**：没有 `working-tree-encoding=UTF-8` 时，中文 Markdown 会被过滤器破坏，且修复后 checkout 可能再次破坏。
5. **systemd 段归属**：`StartLimitIntervalSec` / `StartLimitBurst` 在 `[Unit]`，写在 `[Service]` 会被**静默忽略**，单元照常启动但限流失效。
6. **catkin relay 遮蔽**：`catkin_install_python` 在 devel 空间生成同名 relay，驱动 import 到 relay 外壳而不是真实模块。Host 测试全绿，真 ROS 节点全死。
7. **Catkin 缓存路径**：仓库改名后 `build/`、`devel/`、`install/` 里嵌着旧绝对路径，必须清空重建。
8. **`.gitignore` 裸文件名规则**：裸 `Makefile` 会吞掉固件工程的手写 Makefile，`git status` 一片干净但克隆后 `make` 报 "No targets"。
9. **cppcheck 易失寄存器误判**：`TIM8->CNT - start` 两次读取被当成同一表达式。解决方式是封装显式硬件访问函数，**不要加抑制**。
10. **多故障位的优先级**：`FAULT_STALL` 在急停/过流期间语义失效，位与位的互斥关系也是协议的一部分。注释不等于验证，要有 Host 用例。
11. **MSPM0 默认产物不可烧录**：`ci-link` 剖面是故意的残缺，用 `#error` 和文件名后缀阻止误烧。不要"顺手补全寄存器地址"。
12. **ROS 1 不能跨 WAN**：除了 11311，每个节点还有随机 XML-RPC/TCPROS 端口。跨校区只跨一个带重连的 TCP 会话。
13. **服务器根分区 97%**：bag、镜像、模型、ROS 日志只写 `/data2`。2026-11-06 实测 `/data2` 也已 97%（约剩 67 GB），8 月日记记的「还有约 1.1 TiB」已不成立；在上面装大东西（如 PX4 SITL）前先确认空间。
14. **供电与地线**：HC-SR04 Echo 5 V 必须分压到 3.3 V；云台舵机必须独立 BEC；共地问题会表现为"随机重启"而不是"传感器坏了"。
15. **"参数能启动"不等于"参数正确"**：PX4/MAVROS 签名参数名、双 CSI profile、OpenMV 图像话题，全部要等实物核对。

---

## 10. 文档维护与交接节奏

| 时机 | 动作 |
|------|------|
| 每个工作日结束 | 追加 `Research_Diary.md`（发现/完成/失败/小结/下一步） |
| 每个 Phase 节点 | 更新本文 §2、§6、§7 与 `00-OVERVIEW.md` |
| 每次架构决策 | 新增 ADR（编号递增），并挂到 `ROADMAP.md` 的"待重估"表（若有时效性） |
| 每次硬件验收 | 新建 `docs/experiments/<date>-<topic>.md`，含命令、原始输出、PASS/FAIL/SKIP、仪器 |
| 每次换 AI 接手 | 更新本文版本号与 §2，写清 commit SHA 与未完成项 |
| 每两周 | 检查本文与 `AI_HANDOFF.md` 是否漂移，互相加链接 |

**本文不是纪念碑，是工作台。** 状态变了就改，不要等"下次大整理"。

---

## 附录 A：关键参数速查（已冻结，出处 ADR-0019）

| 项目 | 值 |
|------|-----|
| 底盘电机 | MC520P30，30:1，13 PPR，四倍频 **1560 counts/rev**，12 V 空载 360±20 RPM |
| 差速底盘 | 有效半径 `r = 0.031 m`，轮距 `track = 0.166 m` |
| 麦轮底盘 | 有效半径 `r = 0.0395 m`，轴距 `0.124 m`，轮距 `0.166 m` |
| 电机驱动 | 每电机独占一片 DRV8871（只有内部 ILIM，无电流反馈，遥测电流发 NaN） |
| ICM42688 | I²C `0x69`，100 Hz，±4 g / ±500 dps，轴映射 `(sensor_y, sensor_x, -sensor_z)` |
| RPLIDAR A2M12 | 256000 bps，前装，约前 225° 可用 |
| HC-SR04 | 每底盘 4 路，由底盘 MCU 定时，经协议 `0x14` 上报，顺序 `front/rear/left/right` |
| MCU 串口 | Pi 40 针 GPIO14/15 → `/dev/ttyAMA0`；`ttyAMA10` 是 3 针调试口，不能当生产口 |
| Pi 基线 | Pi 5 Model B 8 GB / AArch64 / Debian 13 Trixie / 64 GB microSD / UID 1000 |
| 服务器 | 中关村 `192.168.3.30`，11311 与 9090 仅回环；根分区 97%，数据写 `/data2` |
| 无人机 | F450 + Pixhawk 6C/PM07/M9N + A2212 980KV + BL32 30A + 1045 桨；**参数未冻结** |

## 附录 B：接口与话题速查

| 接口 | 方向 | 频率 |
|------|------|------|
| `/<robot_id>/observation` | Edge → Server | ≤ 10 Hz |
| `/<robot_id>/state` | Edge → Server | ≤ 20 Hz |
| `/server/world_state` | World Model → 所有节点 | 按需 |
| `/<robot_id>/mission` | Server → Edge | 事件驱动 |
| `/<robot_id>/mission_status` | Edge → Server | ≤ 2 Hz |
| `/<robot_id>/capability` | Edge → Server | 注册时 |

> Layer 1 原始话题（`/car/scan`、`/mavros/...`）**不在稳定承诺范围内**。

## 附录 C：本文件变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-10-30 | 首次建立：项目级接手章程、状态分级、双 AI 协作协议、已知坑清单 |

---

*维护者：DeepSeek × Claude · 人类负责人保留硬件、安全、经费、发布与对外事务的最终决定权*
