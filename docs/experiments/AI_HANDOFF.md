# AI_HANDOFF.md — 交接给下一个 AI 执行体

> **读者是 AI，不是人。** 这份文档的目标是让一个没有任何本项目上下文的模型，
> 在 15 分钟内达到可以安全动手的状态，并且**知道自己不知道什么**。
>
> 写于 2026-08-01；同日 Phase 1.5 接手后已在 Ubuntu 20.04 实验室服务器完成
> ROS 真环境基线与部署失败关闭，实机硬件联调继续按本文顺序推进。
> 上一份同类文档是 [phase1-pre-departure-brief.md](../../obsolete-documentation/phase1-pre-departure-brief.md)（Phase 0→1 交接，已归档）。

---

## 0. 先做这三件事

```bash
# 1. 确认你在哪、代码是什么状态
git log --oneline -5 && git status -sb

# 2. 确认交付物齐、语法对、自测能跑（不需要 ROS，任意 OS）
make smoke-phase1

# 3. 确认 CI 是什么结论（公开仓库，不需要 token）
curl -sSL "https://api.github.com/repos/vista777-nk/research_compitition/actions/runs?per_page=5"
```

第 2 步应当输出 `通过 61 · 失败 0 · 跳过 N`。**`跳过` 不是通过**——
它表示这台机器缺依赖，那几项在这里没被检查过。

---

## 1. 项目一句话与当前状态

空地联合具身智能研究平台：一架 PX4 无人机 + 一台可换底盘（麦轮/差速）的地面车 +
一台实验室服务器，目标是 Phase 2 的 EQA（具身问答）论文。

| 项 | 值 |
|---|---|
| 分支 | `feat/task-XX`（**不是** main；main 落后很多，合并是人的决定） |
| HEAD | `dc2b38e`（2026-08-01；此后的提交均为纯文档，代码状态同 `a419b98`） |
| Phase | Phase 0 仿真 ✅ 9/9 · Phase 1 基础设施 ✅ 6/6 · **Phase 1.5 实机接入进行中** · Phase 2 未开始 |
| 最近 CI | run #25 (`fe0cf66`) 全绿；run #26 (`a419b98`) 是 cppcheck 转阻塞后的首跑；此后 4 个提交均为纯文档 |
| ADR | 0001–0012、0014–0017 已用；**0013 已预留给「超声波时序方案 A/B/C」**，别占 |

⚠ **提交会自动推送**（VSCode 的 post-commit sync）。`git commit` 之后
`origin/feat/task-XX` 立刻就前进了。**把每一次 commit 当成已公开**。

---

## 2. 必读顺序

| 顺序 | 文件 | 为什么 |
|:---:|------|-------|
| 1 | `project-prometheus-tasks/RESEARCH_PHILOSOPHY.md` | 五条宪法。ADR 制度、分层铁律出自这里 |
| 2 | `CONVENTIONS.md` | 命名/分支/commit/注释语言。**注释与文档写中文，标识符写英文** |
| 3 | `project-prometheus-tasks/ICD.md` §2 | Observation / RobotState / WorldState 的字段语义 |
| 4 | `docs/decisions/ADR-0008.md` | 门禁分层标准，本仓库最常被引用的一条 |
| 5 | `docs/decisions/ADR-0011.md` `ADR-0012.md` | 最近两次决策，含大量"为什么不那样做" |
| 6 | `Research_Diary.md` 末尾三条 | 踩过的坑，按时间倒序读最有效 |
| 7 | `src/deployment/calibration/README.md` §5 | **上机核实清单**——明天要逐条做的事 |

`project-prometheus-tasks/task-NN-*.md` 是**任务书**，不是现状。见 §3 第 1 条。

---

## 3. 不可违反的规则

这些不是风格偏好，每一条背后都有一次真实事故。

### 1. 开工前核对现状，不要照抄任务书
任务文档写于开工之前，**会过期**。已经连续三次抓到冲突：
task-13 抓到 4 个 job 早已存在；task-14 抓到 3 处话题/类型不一致；
task-15 抓到 5 个话题名在仓库里根本不存在。

做法：动手前把任务书提到的**每一个**话题名、文件路径、job 名、函数名
在仓库里 grep 一遍。发现的偏差写进该任务文档末尾的「§与原方案的偏差」，
**不修改正文**（正文是当初的想法，偏差段是实际发生的事）。

### 2. 替身可以替环境，不能替被测对象
（ADR-0011 §方案 G）ROS、硬件、时钟可以用替身。**被测对象本身不行。**

task-15 原文写了个 `preprocess_to_observation()` 号称"模拟 car_preprocessor
的核心逻辑"，三个用例必过——因为测的是那二十行模拟件。真的预处理器里有
新鲜度窗口、降采样、`-1.0` 无效标记等五件事它一件都没有。

现在的做法：`src/air_ground_car_bringup/test/host/ros_stub.py` 提供带
`__slots__` 的消息替身（写错字段名当场 `AttributeError`），跑**真的**节点类。

### 3. 没实测过的检查不能设成阻塞门禁
（ADR-0008 §决策-1）也不能反过来——**实测干净了就不该继续留在告警态**
（ADR-0012）。「每次 CI 都打印、没人读、不会让任何事失败」的步骤是没有含义的灯。

### 4. 查不了的项要报 SKIP 并说明原因，不能静默略过
全仓统一约定：**退出码 2 = SKIP（依赖缺失），不算失败；1 = 真失败。**
`validate.sh` / `smoke-test-phase1.sh` 都按这个解释子进程。

### 5. 写完守卫必须做一次负向测试
只测"该过的过了"不够，要测"该拦的真的拦住了"。已有先例：
CI 里有一步专门删掉一个交付物、断言冒烟测试**必须失败**；
`validate-deployment` 里有一步断言不给 `ROS_IP` 时 compose **必须解析失败**。

### 6. ADR 不可变
已采纳的 ADR 永不修改，只由新 ADR 替代或补充。日记必须写日期。

### 7. 参数名/接口没核实过，就不要写进"生效配置"
（ADR-0010 §决策-3）配置文件的存在本身会被读成"功能已启用"。
没核实的要么留空并自曝，要么标成核实清单。

---

## 4. 仓库地图

```
src/
├── air_ground_interfaces/     Layer 3 抽象接口（msg/srv）——改这里等于改契约
├── air_ground_{drone,car}_bringup/   Layer 1/2 仿真 + 实机驱动骨架
│   └── car_bringup/test/host/ros_stub.py   ★ ROS 替身，无 ROS 环境测试的基础设施
├── air_ground_com_bridge/     Layer 2 通信桥
├── air_ground_lab_server/     Layer 2~3 world_model.py 在这里
├── firmware/                  非 ROS。common/ 是两块板共用的帧/CRC/PID
│   ├── stm32_mecanum/         STM32F407 麦轮，可烧录
│   └── mspm0_diff/            MSPM0G3507 差速，⚠ CI 产物 ci-link 剖面**不可烧录**
└── deployment/                非 ROS。validate.sh 是这个目录的唯一校验入口
    ├── calibration/           task-15 标定工具链
    └── test/                  task-15 集成验证
scripts/smoke-test-phase1.sh   Phase 1 总入口（`make smoke-phase1`）
docs/decisions/ADR-*.md        决策记录，不可变
```

**分层铁律**：Layer 4（研究代码）永不 import `mavros` / `gazebo_msgs` /
`sensor_msgs` 等硬件相关包。它只认 `air_ground_interfaces`。

---

## 5. 验证入口一览

| 命令 | 期望 | 需要 |
|------|------|------|
| `make smoke-phase1` | `通过 61 · 失败 0 · 跳过 0` | python3；全绿需 numpy+opencv+pymavlink |
| `bash src/deployment/validate.sh` | `全部通过` | 同上 |
| `python3 src/deployment/calibration/test/test_calib_pipeline.py` | `Ran 16 tests OK` + 实测值对比表 | numpy, opencv, pyyaml |
| `python3 src/deployment/test/test-serial-loopback.py --self-test` | `通过 13 · 失败 0` | 仅标准库 |
| `python3 src/deployment/test/test-observation-pipeline.py` | `Ran 10 tests OK` | numpy, opencv, pyyaml |
| `python3 -m pytest src/air_ground_car_bringup/test/host -q` | `81 passed` | pytest, pyyaml |
| `python3 src/deployment/test/test_server_deployment.py` | `Ran 9 tests OK` | 仅标准库 |
| `cd src/firmware/stm32_mecanum && make test` | Unity 全过 | gcc + make |
| `cd src/firmware/mspm0_diff && make test` | 70 用例全过 | gcc + make |
| `make test-unit` | `82 tests` | **仅 Ubuntu 20.04 + ROS Noetic** |
| `make test-all` / `make test-e2e` | 全套回归 / `33/33` | Ubuntu 20.04 + ROS Noetic + Xvfb |
| `python3 src/deployment/server/check_lab_server.py` | 5 个服务节点 + 本地 TCP 均健康 | 中关村服务器；常驻服务见 ADR-0017 |

CI 有 7 个 job。四个阻塞 lint 工具：shellcheck 0.10.0（钉版本）、
yamllint 1.38.0（钉）、flake8 7.1.1（钉）、cppcheck（**不钉**，理由见 ADR-0012）。

---

## 6. 已验证 / 未验证清单

**这一节是本文档存在的主要理由。** 不要把未验证项当成已完成。

### 已验证（有实测数据）

| 事项 | 证据 |
|------|------|
| 标定流水线数值正确性 | 合成真值：fx +0.18% / cx −0.06px / RMS 0.137px / 陀螺噪声密度 +1.0% |
| 串口帧协议三端一致 | 黄金帧逐字节，与 `mspm0_diff/test/test_protocol.c::test_pong_golden_frame` 同源 |
| Observation 数据流 | 真 `CarPreprocessor` + 真 `WorldModelStore`，10 条 |
| ROS 真环境基线 | Ubuntu 20.04.6 / Noetic：6 包构建成功；Phase 1 基线 80/80，新增服务器测试后当前 82/82（ADR-0014） |
| 部署配置静态正确性 | 系统 Python 跑 `validate.sh` 52 项全过；45 个健康检查 + 8 个入口模式 + 9 个服务器常驻部署用例 |
| 实机模式失败关闭 | real→real；mock→DEGRADED；sim→仿真；缺 real launch 不回退（ADR-0015） |
| UART / I²C 真实访问层 | pyserial / Linux SMBus 生产类；Host 全套 81/81；错误芯片 ID、半截握手、运行中拔线均有负向用例 |
| 真实 ROS 传感器入口 | mock 五节点持续运行 8s；real 在无设备服务器上整套关闭；同时抓出并修复 Catkin relay 同名自导入 |
| 实验室服务器入口 | `192.168.3.30` 真机：5 个服务端节点、`0.0.0.0:9090`，TCP heartbeat 解码到 `/server/car/state`（ADR-0016） |
| 服务器常驻与恢复 | `Linger=yes` 用户 systemd 已启用；required 节点故障注入后 `NRestarts=1`、五节点和 `127.0.0.1:9090` 自动恢复（ADR-0017） |
| cppcheck 对固件 C 代码零发现 | CI run #24/#25 步骤退出码均为 0 |
| numpy/OpenCV 版本差异不影响标定结论 | CI #25 用 numpy<2 + OpenCV 4.x 跑通全部 16 条 |

### 未验证（**明天的工作**）

| # | 事项 | 怎么验 | 没验的后果 |
|:--:|------|-------|-----------|
| U2 | `camera_info_manager` 是否接受 `air_ground_calibration` 额外键 | 加载一次标定 YAML 看有无 warning | 被拒则要改用 `--strict-camera-info` |
| U3 | PX4 是否支持 MAVLink 签名、参数名 | `nsh> param show MAV_*` | ADR-0010 遗留，签名以为开着其实没开 |
| U4 | MAVROS 签名参数入口 | `rosparam list \| grep -i sign` | 同上 |
| U5 | `/car/openmv/image_raw` 有没有发布者 | `rostopic hz /car/openmv/image_raw` | `openmv_bridge.py` 只发 `detections`，车机相机标定可能采不到图 |
| U6 | 棋盘格实际方格边长 | 卡尺量 10 格取平均 | 尺寸差 1%，外参平移差 1%（内参 K 不受影响） |
| U7 | 标定绝对精度 | 用标定好的相机测已知长度 | RMS 小只说明自洽，不说明正确 |
| U8 | IMU 静置面水平度 | 水平仪，或换 3 个朝向各采一段比零偏 | 倾斜 0.5° → 0.086 m/s² 假零偏，与真零偏同量级 |
| U9 | 串口回路对**真硬件** | 见 §7-B | 目前只有 13 条无硬件自测 |
| U10 | MSPM0 固件可烧录性 | 需接入 TI SDK + SysConfig，见其 README §7 | CI 产物是 `ci-link` 剖面，**烧进去电机不转** |
| U11 | 两套 OpenCV 的逐项数值差 | 读下一次 CI 里 `test_calib_pipeline` 打印的对比表 | 系统性偏移可以躲在 3~10 倍容差里 |
| U12 | UART / I²C 对**真实传感器** | 分别接 RPLIDAR、OpenMV、ICM42688，使用 §7-C 的单节点入口 | 当前证明了 OS 访问与失败语义，尚未证明具体线材/固件/电气连接 |
| U13 | 跨校区隧道与时钟 | 确认 VPN/SSH 隧道后只测 TCP，不把 ROS 暴露跨 WAN；记录三机相对同一参考源的偏差 | 服务器本地健康不能证明良乡端可达或时间戳一致 |

### U1 已关闭（2026-08-01）

仓库重命名后先保留并移走含旧绝对路径的 Catkin 生成缓存，再在当前目录全量构建。
6 个项目包构建成功，`catkin test --no-status` 原始退出码 0，80 个测试全部通过，
其中真 ROS 消息忠实度对照 24 条。CI 已移除 `|| echo` 并改为阻塞；见 ADR-0014。

---

## 7. 明天的上机顺序

按依赖排序。**每一步都有中止条件；触发就停下来记录，不要绕过去。**

### A. Ubuntu 20.04 服务器（零硬件风险，先做）

```bash
# A1 —— 已完成；换服务器或重命名后用于复核
make build
catkin test --no-status
```
当前基线：6 包构建成功、82/82、退出码 0。CI 已按 ADR-0014 阻塞。

```bash
# A2 —— 已安装的服务器常驻入口（不要用会启动本地边缘客户端的 launch-server）
systemctl --user status air-ground-lab-server.service
systemctl --user list-timers air-ground-lab-server-healthcheck.timer
python3 src/deployment/server/check_lab_server.py --json
ss -ltn 'sport = :11311 or sport = :9090'
```

期望只有 `tcp_server/world_model/slam_node/eqa_engine/coordinator` 五个项目节点，
不得出现 `edge_server_bridge` 或 `drone_car_bridge`；11311 与 9090 都只监听
`127.0.0.1`。当前已完成真实 heartbeat 解码和 required 节点故障恢复；跨校区只准
走后续批准的受控隧道，见 ADR-0017。

```bash
# A3 仿真基线仍然可用
make test-all && make test-e2e
```
中止条件：仿真基线跑不起来 → 先修基线，不要带着坏基线上硬件。

### B. 下位机（最便宜的硬件，先于整机）

```bash
# B1 STM32F407 麦轮
cd src/firmware/stm32_mecanum && make && make size     # 烧 build/*.bin
# 接 UART，然后：
CHASSIS=mecanum bash src/deployment/test/test-serial-loopback.sh /dev/ttyAMA0
```
期望：`✓ 收到 PONG: 固件 v0.1.0 · 板卡 STM32F407 · 底盘 mecanum`。
这一条同时验证 ADR-0003 的三份实现（STM32 / MSPM0 / Pi 端 Python）一致——解 U9。

中止条件：
- 收不到任何帧 → TX/RX 接反或没共地（脚本会这么提示）
- 有 `crc错` 计数 → 波特率不匹配或线太长
- 底盘类型不符 → **烧错固件或 `CHASSIS` 设错**，运动学会用错模型，必须停

```bash
# B2 MSPM0G3507 差速 —— 预期有摩擦
```
⚠ CI 产出的是 `ci-link` 剖面，**移植层为空实现，不可烧录**（ADR-0004 §决策-3）。
要接 TI MSPM0 SDK + SysConfig，见 `src/firmware/mspm0_diff/README.md` §7。解 U10。

### C. 车机树莓派

`EDGE_MODE` 已由 ADR-0015 固化为三种互斥语义：

- `real`：只传 `backend:=real`，后端未实现/硬件不可用时明确失败，绝不回退；
- `mock`：显式假硬件冒烟，健康检查必须返回 DEGRADED（退出码 4）；
- `sim`：仿真适配器。

实机联调只能用 `real`。UART / I²C Linux 访问层已经实现，但尚未接真实传感器；
GPIO 仍等待 ADR-0013，因此整车满配 real 入口现在会失败，这是正确中止条件。
逐件台架验收时用 `car_edge_real.launch` 的 `enable_*` 参数关闭其余传感器，
不要把该临时子集当成满配部署。

```bash
# 例：只验 RPLIDAR；其余节点不启动
roslaunch air_ground_car_bringup car_edge_real.launch \
  enable_icm42688:=false enable_hcsr04:=false enable_openmv:=false \
  enable_preprocessor:=false
```

```bash
sudo bash src/deployment/install.sh --role car --dry-run   # 先看要做什么
sudo bash src/deployment/install.sh --role car
# 编辑 /opt/air-ground/.env：ROLE / ROS_IP / HOST_GID_DIALOUT / HOST_GID_I2C
```
`ROS_IP` 写错的症状有迷惑性：`rosnode list` 看得到节点，`rostopic echo` 永远没数据。

顺序（`src/deployment/README.md` §4）：
静态 IP → `require-image.sh` → `systemctl enable --now air-ground-car-edge.service`
→ `enable --now air-ground-healthcheck.timer`（**是 .timer 不是 .service**）
→ **最后**才做 SSH 加固（§4.8，顺序反了会把自己锁在门外）。

```bash
# C3 解 U5 —— 逐个确认传感器话题真的在发
for t in /car/scan /car/imu/data /car/openmv/image_raw /car/openmv/detections; do
  echo "--- $t"; timeout 5 rostopic hz "$t"; done
```
`/car/openmv/image_raw` 很可能**没有发布者**。若确认没有，这是一个真实的接口缺口：
`car_edge.yaml` 的 `topics.image` 指着它，而 `openmv_bridge.py` 只发 `detections`。
处理方式是决策而非修补——写 ADR。

```bash
# C4 在真 ROS 下重跑数据流验证
python3 src/deployment/test/test-observation-pipeline.py   # 仍走替身
rostopic echo -n1 /car/observation                         # 真节点的实际输出
```
比对两者的 `modalities` 顺序与 `ultrasonic_ranges` 顺序（ICD §2.1 规定
`[front, rear, left, right]`）。

### D. 标定（依赖 C 完成）

```bash
# D1 相机内参
ROBOT=car ./src/deployment/calibration/record-calib-bag.sh camera
```
脚本会在开录前检查话题在线，不在线拒绝开录。举板方法在脚本输出里，照做。
先用卡尺量方格边长（解 U6），把实测值传给 `--square-size`。

```bash
python3 convert-bag-to-kalibr.py --bag <bag> --extract-images out/images --image-topic /car/openmv/image_raw
python3 calibrate-camera.py --input out/images --pattern 9x6 --square-size <实测> \
    --camera-name car_openmv --serial <序列号> --output camera_intrinsics.yaml
python3 validate-calibration.py camera_intrinsics.yaml
python3 generate-calib-report.py camera_intrinsics.yaml -o calibration_db/<日期>_car/REPORT.md
```

D2（解 U2）：把 YAML 喂给 `camera_info_manager` 或起相机节点，看 `air_ground_calibration`
额外段是否被接受。被拒就用 `--strict-camera-info` 重新生成。

D3 IMU（**要 2 小时，晚上挂着跑**）：
```bash
ROBOT=car ./src/deployment/calibration/record-calib-bag.sh imu     # 默认 7200s
```
为什么是 2 小时：Allan 曲线白噪声段与随机游走段的交点在 τ=√3·N/K ≈ 173 s，
要在 +1/2 段取到有统计意义的点，总时长得是它的几十倍。15 分钟只够解噪声密度和零偏，
`calibrate-imu.py` 会把 `quality.random_walk_reliable` 置 false 并退出码 1——**这是设计如此**。

解出来后把 `derived_for_driver` 段（**离散标准差**，不是密度）抄进
`src/air_ground_car_bringup/config/real_sensors.yaml` 的 `icm42688` 段，
commit message 里写清楚取自哪个归档目录。

D4 相机-IMU 外参：必须在 D1+D3 都通过之后。Phase 1 只交付接口，解算要装 Kalibr。

### E. 无人机 / PX4（风险最高，放最后）

```bash
# E1/E2 解 U3/U4
# QGC 或 nsh: param show MAV_*
rosparam list | grep -i sign
```

E3 签名 A/B 对照（ADR-0010 §决策-2 **强制**）：
**关签名通一次 → 开签名再通一次**。没有对照就上飞，等于把一个新引入的
"消息全丢"故障模式带到空中。签名失败的表现是链路正常、心跳正常、消息全被丢弃。

⚠ 3DR 数传只有 24 KB/s，**不能传图像**。VLM 要的图必须走 WiFi/TCP JSON 通道。

---

## 8. 未结欠账

| # | 内容 | 结的条件 | 位置 |
|:--:|------|---------|------|
| D1 | ✅ 已关闭：ROS 80/80，CI 改为阻塞 | ADR-0014 | `.github/workflows/ci.yml` |
| D2 | 超声波时序方案 A/B/C | 需实机测 GPIO 时序；**编号已预留 ADR-0013** | task-14 日记 |
| D3 | PX4/MAVROS 签名参数名 | §7-E1/E2 | ADR-0010 §影响 |
| D4 | `camera_info_manager` 额外键 | §7-D2 | ADR-0011 §决策-1 |
| D5 | `/car/openmv/image_raw` 发布者 | §7-C3 | 标定 README §5 第 5 条 |
| D6 | 两套 OpenCV 逐项数值差 | 读下次 CI 日志的对比表 | ADR-0011 §影响 |
| D7 | 本地从未跑过 cppcheck | 有条件就补一次本地全量 | ADR-0012 §影响 |
| D8 | ✅ 已关闭：real/mock/sim 显式分离，real 失败关闭，mock 报 DEGRADED | ADR-0015；当前 8 条入口模式用例 | `entrypoint.sh`；部署 README §5.4 |
| D9 | 跨校区受控隧道 + 两端时间偏差实测 | 需网络方案、两台 Pi 实机与现场时钟证据 | ADR-0017；`network_server.yaml` |

---

## 9. 环境陷阱

### 上一台开发机（Windows，可能已不适用）
- Git Bash；`python3` **不在 PATH**，用 `python`（/c/Python314，3.14.6）
- 全局只有 PyYAML / yamllint / flake8。numpy+opencv 在临时 venv `/tmp/t15venv`
  （`/tmp` = `C:\Users\Vista\AppData\Local\Temp`，**会被清**，需要就重建）
- **`numpy<2.0.0` 装不上**（无 cp314 轮子）→ 本地实测用的是 numpy 2.5.1 + OpenCV 5.0.0，
  与 CI 钉的 numpy 1.x + OpenCV 4.x 不是同一套
- 控制台是 GBK：跑任何输出中文的脚本前 `export PYTHONIOENCODING=utf-8:replace`
- shellcheck 0.10.0 二进制曾放在 `/tmp/sc/shellcheck.exe`

### 当前实验室服务器（2026-08-01 实测）

- Ubuntu 20.04.6、ROS Noetic、Docker 28.1.1；3 × RTX A6000（每张 49140 MiB）
- 当前有线地址 `192.168.3.30/24`；服务器与硬件分属中关村/良乡，旧同网
  `192.168.1.100` 规划不再作为跨校区默认
- `systemd-timesyncd` 当前显示 `System clock synchronized: yes`；`chronyc` 未安装，
  仍不能声称与两台 Pi 的相对时间偏差已经验证
- `air-ground-lab-server.service` 与五分钟健康 timer 已启用，用户 `Linger=yes`；
  ROS/TCP 只监听回环，日志位于 `/data2/air-ground-server/ros-log`
- 根分区使用率 97%（约 87 GiB 可用），`/data2` 约 1.1 TiB 可用；不要把项目日志迁回根分区
- 未接机器人 USB/串口设备；只有 `/dev/i2c-0`、`/dev/i2c-1` 等主机设备
- `python3` 默认指向 Anaconda 且没有 pytest；ROS/Catkin 用 `/usr/bin/python3`，
  Host 测试本次借用了已有 `knn_wsr` 环境，不要因此修改系统 Python

### 跨平台陷阱（长期有效）
| 陷阱 | 症状 | 对策 |
|------|------|------|
| `git ls-files` 不含未跟踪文件 | 新写的文件**没被 lint 检查**却显示 0 条 | 跑门禁前先 `git add -A` |
| Windows 工作区 `*.yaml` 是 CRLF | 本地 yamllint 报一堆 `new-lines`，CI 却干净 | 校验 `git show :<file>` 的内容 |
| MSYS grep 静默剥掉 `\r` | CRLF 检查在 Windows 上永远通过 | 用"剥 CR 前后字节数是否变化"判断（`validate.sh` §4 已如此） |
| 连字符文件名不能 import | `calibrate-camera.py` 等 | `importlib.util.spec_from_file_location` 按路径加载 |
| `mock_hardware.py` 在 `scripts/` 不在 `test/` | 它是运行时后端（`backend: mock`），不是测试件 | — |
| GitHub job **日志**要 admin | 403 | 但 `/actions/runs/{id}/jobs` 的 `steps[].conclusion` **免鉴权可读**，`continue-on-error` 不改写步骤自身结论——ADR-0012 就是靠这个拿到证据的 |

---

## 10. 交付纪律

- **Commit**：Conventional Commits + 中文描述，scope 用任务号。
  正文列关键改动与**实测数字**，末尾 `Ref:` 指任务文档与 ADR。
- **偏差**：写进对应 `task-NN-*.md` 末尾的「§与原方案的偏差」，正文不动。
- **决策**：写 ADR。必须包含「被否决的方案」及其**量化代价**——
  本仓库的 ADR 之所以有用，是因为它们记了不选那条路的理由。
- **日记**：`Research_Diary.md` 追加，**必须写日期**，写在
  「## 历史名称脚注」之前。记事故与认识，不记流水账。
- **文档**：中文；标识符英文（CONVENTIONS §4.2）。

---

## 11. Phase 2 入口

`project-prometheus-tasks/ROADMAP.md`：
- **Step 1（2026.08）** 实机组装 + 仿真对齐 ← 明天开始的就是这个
- **Step 2（2026.09~12）** EQA 系统 → 论文
- ROADMAP 末尾有「待重估的技术决策（Phase 2 动手前必查）」，动 Layer 4 之前先读

Step 1 的验收在 ROADMAP §本阶段目标，其中一条要求
**把仿真参数校准到与实机一致（传感器噪声、延迟、带宽）**——
这正是 task-15 标定工具链的下游用途：`imu_intrinsics.yaml` 解出的噪声密度
既填实机驱动，也该回填仿真的 `car_sensors.yaml`，让两边的噪声模型对齐。

---

## 12. 一句话总结

这个仓库的核心资产不是代码，是**一套让"看起来对"和"真的对"能被区分开的机制**。
到目前为止已经识别出五种"假证据"：假绿灯（不会失败的检查）、假报告（全是勾但没查）、
假状态（降级了却显示健康）、假配置（文件存在被当成功能启用）、假测试（测的是自己的模拟件）。

它们的共同结构是：**一个本该提供证据的东西，变成了它自己的证据。**

明天带着真硬件时，这条比任何时候都重要——因为实机会给你大量"看起来在工作"的现象。

---

*交接人：Claude Opus 5 · 2026-08-01 · HEAD `dc2b38e`*
