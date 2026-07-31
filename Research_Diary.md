###### 2026/7/25
    1.发现：a) ChatGPT 终审指出项目缺少"设计哲学"层——只有 Platform/Roadmap/ICD/Tasks，没有"为什么这样设计"。
             提出五条不可违反的宪法原则：Platform First、Interface Before Implementation、Everything Produces Knowledge、
             Simulation is the First Robot、Every Module Must Be Replaceable。
             还提出仓库应视为"Laboratory Memory"而非"Git Repository"，建议建立 ADR（架构决策记录）体系。
          b) Git for Windows 在 UTF-8 环境下对中文 Markdown 文件产生编码过滤器破坏，导致全部 task 文件出现乱码。
             根本原因是 Git 的 core.autocrlf / working-tree-encoding 与 Windows 控制台编码交互不当。
          c) 此前文档中 Gazebo 版本写为 9，但 ROS Noetic 实际默认捆绑 Gazebo 11（classic 系列最终版），需修正。
          d) 初期 ROS package 命名不一致（drone_sitl vs car_sim vs edge_server），缺少统一前缀，需全局重构。

    2.完成：【规划阶段收官】
          1) RESEARCH_PHILOSOPHY.md —— 项目宪法文档，含五条原则 + ADR 规范 + 长期愿景 + 角色平衡
          2) 更新 00-OVERVIEW.md，必读文件从 3 份扩展为 4 份
          3) .gitignore —— 覆盖 Python / ROS / Gazebo / PX4 / C++ / IDE
          4) README.md —— 仓库首页不再空白，含架构速览与导航
          5) CONVENTIONS.md —— 四根支柱（Repository / Workspace / Naming / Convention），含全场景命名规则表与代码审查清单
          6) docs/decisions/ADR-0001.md —— 第一份架构决策记录，记录"采纳规范体系"的决策
          7) 仓库记忆 (air-ground-sim-project.md) 同步更新
          8) 项目正式定名 Project Prometheus
          9) SECURITY.md —— 从 GitHub 通用模板改写为项目适配版本，含三阶段安全策略 + 五条安全原则 + 依赖安全链
          【编码与命名修正】
          10) 修复全部 9 个 task 文件的中文编码乱码
          11) 添加 .gitattributes，设置 *.md working-tree-encoding=UTF-8，防止 Git for Windows 编码过滤器再次破坏中文
          12) 修复 task-01 中残留的 ）? 编码乱码 artifacts
          13) 全局重构：统一所有文档中 ROS package 命名为 air_ground_ 前缀
              （drone_sitl → air_ground_drone_sitl, car_sim → air_ground_car_sim, 以此类推）
          14) 手动修复因编码 revert 导致的目录结构异常
          【仿真搭建启动】
          15) PLATFORM.md / task-01 / task-02 基线修正：Gazebo 9 → Gazebo Classic 11
          16) Task-01 实施：搭建 ROS 工作空间 + air_ground_interfaces 接口包骨架（feat/task-01-env-setup 合入 main）
          17) Task-02 文档：根据阶段 A 实测结果修正 PX4 SITL 无人机仿真文档

    3.失败：a) 编码修复过程经历多次 revert → 重试循环。原因是 .gitattributes 未在修复内容前提交，
             导致修复后的文件在 checkout 时再次被 Windows Git 过滤器破坏。
             教训：在 Windows 上处理中文仓库时，.gitattributes 必须作为仓库的第一批提交，否则后续所有修复都可能白做。
          b) 命名重构中途两次 revert，因范围过大导致部分引用断裂。最终采用分批替换策略（先改 package 名，再改文档引用）解决。

    4.小结：这一天是项目历史上单日推进最密集的一天——完成了从"规划"到"执行"的范式转换。
          上午：ChatGPT 终审 → 宪法文档 → 基础设施规范 → 规划阶段正式完结。
          下午：编码灾难（Git for Windows 中文过滤器）→ .gitattributes 防御 → 命名统一 → 目录修复。
          晚间：Gazebo 版本修正 → Task-01 实施 → Task-02 文档修正。
          全天约 25 次提交，全部遵循 Conventional Commits 1.0.0 + 简体中文规范。
          项目从"一套文档"变成了"一套文档 + 一套规范 + 一个可编译的 ROS 工作空间骨架"。

    5.下一步：Task-02~06 依次推进——无人机 SITL → 差速底盘 → 麦轮底盘 → 传感器 → 通信桥。

###### 2026/7/26
    1.发现：a) PX4 SITL 在 Gazebo Classic 11 环境下，深度相机插件 (libgazebo_ros_openni_kinect.so)
             与 PX4 v1.14 的 MAVLink 消息流存在时间戳对齐问题，需在 URDF 中显式配置 <updateRate> 和 <plugin> 的命名空间。
          b) 差速底盘的 ros_control + diff_drive_controller 组合在 Gazebo 中需要精确的 wheelSeparation 和 wheelDiameter
             参数，否则 odom 漂移严重——仿真中的 1mm 参数偏差在 10m 行驶后会产生 ~15cm 定位误差。

    2.完成：1) Task-02 实施：PX4 SITL 无人机 Gazebo 模型（含深度相机 + GPS + IMU 插件），
             通过 MAVROS 与 PX4 飞控栈全链路联通，MAVLink 心跳正常，/drone/ 命名空间下的传感器 Topic 全部有数据。
          2) Merge PR #4：feat/task-01-env-setup 合入 main，ROS 工作空间骨架 + 接口包进入稳定主线。
          3) Task-03 实施：差速底盘 Gazebo 仿真（URDF + ros_control + diff_drive_controller），
             编码器里程计发布正常，/car/cmd_vel → 左右轮速解算正确。
          4) 更新 00-OVERVIEW.md，Task-03 标记为 ✅ 已完成。

    3.失败：无。Task-02 和 Task-03 均一次性通过验收，未出现需要 revert 或大规模重构的情况。

    4.小结：仿真搭建进入稳定节奏。Task-02（无人机）和 Task-03（差速底盘）在同一天完成，
          标志着空中平台和地面平台的仿真基础均已就绪。PR 合并流程顺畅，main 分支始终保持可编译状态。
          全天 4 次提交（含 1 次 merge），全部遵循 Conventional Commits 规范。

    5.下一步：Task-04（麦轮底盘）—— 在差速底盘基础上新增麦轮运动学控制器，实现底盘动态切换。

###### 2026/7/27
    1.发现：a) Task-06 的 MAVLink UDP 桥需要适配 PX4 v1.14 SITL 的 18570 端口（而非默认的 14550），
             且需要同时处理 MAVLink v1 和 v2 帧格式——PX4 v1.14 在 SITL 模式下默认使用 MAVLink v2。
          b) TCP JSON 通信中，长度帧前缀（4 字节 big-endian）比分隔符方案更可靠——分隔符可能在 JPEG 二进制数据中意外出现。
          c) 麦轮底盘的正逆运动学解算在 45° 辊子安装角下，四个轮的转速耦合度远高于预期——
             一个轮子的打滑会导致其他三轮的速度指令全部失效，需要在控制器层面加入滑转检测与降级策略。
          d) 从 Task-03 到 Task-06 的连续推进中，task-03 和 task-04 的回归测试全部通过（7/7 和 17/17），
             说明平台分层架构（Layer 1 硬件 → Layer 2 Bridge）的接口隔离设计正在发挥作用。

    2.完成：1) Task-04 实施：麦轮底盘 URDF + 自定义逆运动学控制器 (mecanum_controller.py)，
             /car/cmd_vel → 四轮独立转速解算，/car/switch_chassis 服务实现差速 ↔ 麦轮动态切换。
          2) Merge PR #5：feat/task-01-env-setup → main。
          3) Task-05 实施：车载传感器完整配置（OpenMV 云台相机 + RPLIDAR A1 + 4×HC-SR04 超声波 + ICM42688 IMU），
             传感器数据通过统一 YAML 配置文件管理，云台支持 pitch/yaw 双轴角度控制。
          4) Merge PR #7：feat/task-XX → main。
          5) Task-06 实施——空地通信桥（air_ground_com_bridge 包）：
             · MAVLink UDP 双向桥：适配 PX4 v1.14 SITL 18570 端口，MAVLink v1/v2 帧自动识别
             · TCP 长度帧 JSON 通信：4 字节 big-endian 帧头 + JSON payload，支持带宽限制与断线重连
             · 传感器/JPEG 上行通道：边缘端传感器数据经 TCP 推送到服务器
             · 服务器命令下行通道：服务器 → 边缘 → 机器人控制指令
             · network.yaml：完整网络配置（UDP 端口、TCP 地址、带宽限制、重连策略）
             · Launch 文件 + CMake 安装规则 + 自动验收脚本
          6) Merge PR #8：feat/task-XX → main，Task-06 合入稳定主线。
          7) 更新 00-OVERVIEW.md，Task-04~06 标记为 ✅。

          【测试结果汇总】
          · Task-06 单元测试：16/16 passed
          · 工作空间累计测试：28/28 passed（Task-03: 7 + Task-04: 17 + Task-06: 16，去重后）
          · Task-06 端到端验收：11 passed, 0 failed
          · PX4/MAVROS 联合验证：3 passed, 0 failed
          · Task-03 回归测试：7/7 passed
          · Task-04 回归测试：17/17 passed
          · 全工作空间构建：5 个包全部成功（air_ground_interfaces, air_ground_drone_sitl[^1],
            air_ground_car_sim[^2], air_ground_com_bridge, air_ground_drone_bringup）
          · rosdep check、安装空间运行验证、静态检查：全部通过

    3.失败：无。Task-04/05/06 均一次性通过验收，所有回归测试绿灯。

    4.小结：仿真搭建进入高速推进阶段。7/26 完成了 2 个 task（02+03），7/27 完成了 3 个 task（04+05+06），
          两天合计 5 个 task，全部一次性验收通过。至此，9 个 task 中 6 个已完成（01~06），
          剩余 task-07（边缘预处理 + 服务器）、task-08（集成总装）、task-09（仿真验证）。
          最关键的里程碑是 Task-06——空地通信桥的完成意味着无人机 ↔ 车机 ↔ 服务器的全链路数据通道已打通。
          task-07 的前置依赖全部满足，可以立即开始。

    5.下一步：Task-07 —— 边缘预处理器（drone/car preprocessor）+ 服务器 TCP 接收器 + SLAM/EQA/Coordinator 占位节点。
          同步启动 Task-06 合并到 main 分支的 PR 流程。

###### 2026/7/27（Task-07 补记）
    1.发现：a) Task-07 旧草案混用了已弃用的 SensorFusion 与稳定 Observation，且 TCP 接收只调用一次 recv(4)，
             无法保证获得完整长度头。
          b) 旧 Coordinator 直接发布 MAVROS setpoint 和具体 diff_drive_controller 话题，违反 Layer 4
             只依赖抽象接口的架构约束。
          c) Task-06 与 drone_preprocessor 都需要提供 /drone/state。通过 /drone/state_owner 协调所有权，
             保留 Task-06 单独运行兼容性的同时，避免集成场景出现双发布者竞争。

    2.完成：1) 实现无人机和车机 Edge preprocessor，统一发布 Observation、RobotState、Capability。
          2) 扩展 Task-06 TCP bridge，以轮询方式传输 car/drone 双 agent 抽象遥测。
          3) 实现严格长度帧 TCP server、带新鲜度过滤和查询服务的 World Model。
          4) 实现 SLAM、EQA、Coordinator 抽象占位节点；Mission 只通过稳定接口分发。
          5) Task-07 自动验收 15/15；工作空间单元测试 56/56。
          6) Task-02~06 回归分别为 9/9、7/7、17/17、28/28、11/11。

    3.问题与修复：a) TCP 解码纯函数首次测试时提前求值 rospy.Time.now，脱离 ROS master 会失败；
                   改为零时间戳并由 World Model 按接收时间兜底。
                b) 直接调用 CMake 安装目标时未加载 ROS 环境，catkin 环境模块不可见；
                   显式 source ROS 与 devel 后安装验证通过。

    4.小结：服务器侧首次形成完整的“Edge TELL → World Model → Research ASK → Mission”闭环，
          研究层不再触碰 MAVLink、Gazebo 和具体底盘控制器。

    5.下一步：Task-08 —— 集成总装 Launch、分场景入口和 Makefile。


###### 2026/7/27（Task-09 验证）
    1.完成：新增 `src/e2e_test.sh` 与 `src/quick_smoke.sh`，并在 Makefile 中提供 `test-e2e`、`quick-smoke` 入口。
          E2E 覆盖总装启动、双模型、传感器、边缘消息、MAVLink/TCP 桥、World Model 和 EQA Mission 分发。

    2.验证：`make build` 成功；单元测试 56/56；Task-02~07 回归 9/9、7/7、17/17、28/28、11/11、15/15；
          Quick Smoke 5/5；Task-09 E2E 33/33；SensorFusion 自定义消息可用。

    3.修正：将 E2E 命令链按当前 ICD 实现验证为 `/server/eqa/query` → `/car/mission`，并明确底盘切换由
          麦轮场景的 `chassis_swapper` 与 `make test-mecanum` 覆盖，不在差速总装中调用不存在的服务。

    4.小结：仿真框架 9 个任务全部完成，后续进入实机对齐与研究模块迭代。


###### 2026/7/28
    1.发现：a) 项目仓库在 24 小时审查中被发现缺少 LICENSE、依赖冻结、CI/CD 等工程化基础，
             虽不影响仿真功能但阻碍可复现性和开源合规性。
          b) CI 中使用的 `apt-key add` 在 Ubuntu 20.04 已被弃用，与 `setup_all.sh` 中的
             `signed-by` 方式不一致。
          c) 仓库目录 `Project_Prometheus_Tasks/`、`Obsolete_or_Outdated_Documentation/`
             自项目创建起一直不符合 CONVENTIONS.md 的 kebab-case 约定。

    2.完成：【工程化基础设施补全】
          1) 根目录创建 LICENSE（MIT）全文，与 package.xml 声明一致。
          2) 根目录创建 `requirements.txt`，冻结 pymavlink/opencv/numpy/PyYAML/Pillow 版本。
          3) 全部 6 个 package.xml 维护者邮箱从 `user@example.com` 更新为真实邮箱。
          4) GitHub Actions CI 流水线（`.github/workflows/ci.yml`）：
             容器化 Ubuntu 20.04 + catkin build + catkin test + 结果汇总，
             经过 4 轮修复（runner → 容器化 → checkout 路径 → bash shell → build-essential）。
          5) `scripts/setup_runtime.sh`：统一运行时环境加载脚本（ros/px4 双模式）。

          【目录重命名 — 符合 kebab-case 规范】
          6) `Project_Prometheus_Tasks/` → `project-prometheus-tasks/`
          7) `Obsolete_or_Outdated_Documentation/` → `obsolete-documentation/`
          8) 全文更新所有文档中的旧路径引用。

          【Phase 0 收官】
          9) Task-08 实施：`air_ground_bringup` 包 + 4 个分层 Launch（full/drone/car/server-only）。
          10) Task-09 实施：E2E 端到端测试 352 行（33/33 通过）+ Quick Smoke 177 行（5/5 通过）。
          11) `Makefile` 20 个目标：build/clean/rebuild/test-*/launch-*/kill/status。
          12) `setup_all.sh` 6 步一键安装（ROS → PX4 → Python）。
          13) 施工完成审查：全量 Python 文件 PEP 8 lint（ruff）All checks passed。
          14) 归档文档添加"已过时"警告标记。
          15) CI `apt-key add` 替换为 `gpg --dearmor` + `signed-by` 方式。
          16) 创建里程碑 Tag：`v0.1.0` + `milestone/sim-framework-done`。

          【数据汇总】
          · README badges：Phase 0 ✅ · Tasks 9/9 · Tests 56/56
          · Phase 0 全部 9 个任务：✅✅✅✅✅✅✅✅✅
          · 6 个 ROS Package 全部可编译
          · E2E：33/33 通过
          · 单元测试：56/56 通过
          · 回归测试：Task-02~07 全部通过

    3.失败：a) CI 配置经历 4 次修复迭代（runner 类型 → 容器化 → checkout 路径 → bash 默认 shell），
             原因是 GitHub Actions 的 ubuntu-latest (22.04) runner 与 ROS Noetic (20.04) 不兼容。
             最终方案：`runs-on: ubuntu-22.04` + `container: ubuntu:20.04`。
          b) 目录重命名后 `git log --follow` 可追溯历史，但 GitHub 的 blame 界面在重命名点会中断。

    4.小结：Phase 0 仿真框架正式收官。从 7/25 规划启动到 7/28 全部完成，历时 4 天，
          累计约 40 次提交，全部遵循 Conventional Commits + 简体中文规范。
          平台已具备完整的"传感器→边缘→通信→服务器→决策→执行"数字孪生闭环。
          下一步：Phase 1 实机调试（F450/S500 组装 + 树莓派 + 传感器套件）。

    5.下一步：Phase 1 准备工作 — 硬件采购清单确认、实机网络拓扑设计、MAVLink 签名配置、
          SSH 密钥部署方案。


###### 2026/7/28（Phase 1 · task-10）
    1.发现：a) `.gitignore` 里为屏蔽 CMake 生成物写了一条裸 `Makefile` 规则，只放行了根目录
             （`!/Makefile`）。固件工程的手写 Makefile 会被静默吞掉——克隆下来 `make` 直接报
             "No targets"，而且 git status 一片干净，极难定位。已补 `!src/firmware/**/Makefile`。
             教训：凡是"按文件名"而非"按路径"的忽略规则，都要在新增子项目时复查一遍。
          b) task-10 §10.6 的用例表把 `ω=1.0` 标为 `test_rotate_cw`。右手系下 ω>0 是逆时针，
             命名与输入不自洽。实现时以数学与仿真 `mecanum_controller.py` 为准，两个方向各写一个用例。
          c) 编码器测速的分辨率陷阱：1320 计数/转、1ms 采样窗口 → 分辨率 60000/1320 ≈ 45 RPM/计数，
             比整个调速范围的 1/8 还大。若照搬"1kHz 采样即 1kHz 测速"，PID 会被量化噪声牵着走。
             改成 10ms 测速窗口（约 4.5 RPM）+ 一阶低通，速度环仍跑满 1kHz。
          d) 不做字节填充的定长头协议有固有失同步窗口：噪声插入一个杂散 0xA5，真实 SOF（0xA5=165）
             就会被当成 LEN，解析器空等 165 字节，期间正常帧全被吞掉。这不是实现 bug，
             是这类协议的结构性代价。解药是物理层的空闲重同步（USART IDLE 中断）。

    2.完成：【Phase 1 首个任务 · STM32F407 麦轮固件】
          1) `src/firmware/common/` —— 与 task-11 共享的纯 C99 库：CRC-16/CCITT-FALSE、
             帧打包/拆包状态机、条件积分抗饱和 PID、极简 Unity 风格测试框架。
          2) `src/firmware/stm32_mecanum/` —— 算法层（逆/正运动学 + 命令分发，可 Host 测试）
             与裸机 HAL 层（寄存器映射、C 实现的启动文件与向量表、时钟树 168MHz、
             TIM2/3/4/5 编码器、TIM1 四路 PWM、USART1 中断收发、TIM6 1kHz 控制中断）。
          3) 不引入 STM32Cube/HAL：自带最小寄存器层，克隆下来就能编。
             `stm32f4xx_conf.h` 保留为编译目标切换点（裸机 / HAL / Host 测试三选一）。
          4) 52 个 Host 单元测试全绿：CRC 标准向量 0x29B1 + 逐比特错误检出、
             运动学 12 个用例、PID 阶跃响应（超调 0%、稳态误差 <1%）与负载扰动抑制、
             协议 0~251 字节全长度往返 + CRC/EOF 错误注入 + 失同步恢复。
          5) `docs/decisions/ADR-0003.md` —— 串口二进制帧协议统一设计。
             六条决策逐条给出理由与代价，含 Python 参考实现与黄金帧字节序列。
          6) CI 新增 `build-stm32-firmware` job（与 ROS job 并行），含 commit/构建时间注入。
          7) 固件 README 含引脚表、协议规范、整定准则与**上板检查清单**。

    3.失败：a) 三处设计缺陷是被测试逼出来的，不是审出来的：
             · 积分限幅 20 配 ki=0.030 → 积分项最多贡献 0.6 占空比，负载扰动测试直接红。
               整定准则应是 `ki × integral_limit ≥ output_limit`，已写进注释。
             · `FAULT_STALL` / `FAULT_UART_ERROR` 只置不清，一次瞬时故障会亮到复位。
             · 拆帧器失同步窗口（见 1-d）。
             说明"先写测试再写实现"在固件上同样成立——尤其是没有硬件可验的阶段。
          b) 本机没有 `arm-none-eabi-gcc`，交叉编译未在开发机执行。
             已用 `gcc -fsyntax-only` 对全部裸机源文件做语义检查，但链接期问题
             （undefined reference、段溢出）只能等 CI 首次运行暴露。

    4.小结：Phase 1 从"固件先行"开始是对的。整个任务没有碰过一次硬件，
          却把协议、运动学、PID、故障处理这些真正会咬人的东西全部固化下来并测住了。
          HAL 层尚未上板验证，这一点在 README 和任务文档里都写明了，不做模糊表述。

    5.下一步：task-11（MSPM0G3507 差速固件）可直接复用 `common/`，帧结构照搬只改命令表；
          task-13 的 CI 扩展已有一个可参照的固件 job 模板，`common/` 布局也已落地。


###### 2026/7/29（task-10 评审收口）
    1.完成：1) CI `build-stm32-firmware` job 首次运行即通过 —— 交叉编译、链接、artifact 产出全绿。
             7/28 记录的"链接期问题只能等 CI 暴露"这条风险正式解除，未出现 undefined reference 或段溢出。
          2) 外部评审确认三处有意偏差（ω 正方向、等比缩放、帧层不转义）全部成立，无需返工。
          3) 评审提出的 `FAULT_STALL` / `FAULT_UART_ERROR` 清除路径，核对确认已在 67d800a 中实现：
             前者在 1kHz 中断里跟随实际状态逐拍更新，后者按"距上次遥测的增量"判断。
             `FAULT_ESTOP` 保持锁存型 —— 三者的生命周期分类与评审意见一致。

    2.发现：故障位图存在一类"停机期间语义失效"的问题：过流与急停的早退路径在堵转检测之前 return，
          电机已刹停时"轮子不转"不再构成堵转证据，`FAULT_STALL` 会保持旧值。
          选择不清零（强行清零是误报），改为把"上位机在 FAULT_OVERCURRENT/ESTOP 置位时应忽略
          FAULT_STALL"写进 `protocol.h` 的线上契约注释。
          教训：多故障位共存时，位与位之间的**优先级与互斥关系**也属于协议的一部分，
          不能只定义每一位单独的含义。

    3.小结：task-10 从交付到评审收口闭环。三处有意偏差经外部评审全部确认，
          说明"偏离任务文档时必须在文档里写明理由与代价"这个做法是有效的 ——
          评审者据此直接判定，而不是把它当成缺陷来回质询。


###### 2026/7/29（Phase 1 · task-11）
    1.发现：a) task-10 声称"帧层 / PID / CRC 板无关"，但只有一块板的时候这个声称
             无法证伪。task-11 是第一次真的拿去用：`src/firmware/common/` **一行未改**
             即完成复用，差异全部落在命令表与载荷长度上（8B vs 12B、18B vs 34B、
             board/chassis 编码 0x02 vs 0x01）。共享的边界画对了。
          b) 麦轮固件把寄存器操作直接写在 encoder.c / motor.c / uart.c 里，
             代价是 16 位计数器回绕、测速窗口保持、EMA 滤波这些**真正容易出错的
             逻辑只能上板验证**。本次把寄存器访问收拢到 mspm0_port.h 的 15 个原语后，
             encoder.c 变成纯逻辑，用一个假编码器测掉 11 个用例 —— 全是麦轮固件测不了的。
             代价是每控制周期多约 10 次函数调用（@80MHz 约 0.5µs，占 0.05%）。
             这是本次最值得复制的一条经验。
          c) 差速底盘的饱和处理有一条可以精确证明的性质：双轮同乘系数 s
             ⟺ v'=s·v 且 ω'=s·ω ⟹ 曲率 κ=ω/v **严格不变**。等比缩放让车走同一条弧线
             只是慢了；逐轮硬钳位会改变 v_right/v_left 的比值，直接把弧线掰弯。
             注意直线饱和时两种策略结果完全相同，**验不出区别** ——
             必须构造一个只有单侧轮超限的指令（v=1.0, ω=3.0）才能把它们区分开。
          d) task-11 §11.6 把 ω=1.0 命名为 `test_rotate_in_place_cw`，
             但期望输出写的是"左轮负、右轮正"——那恰恰是逆时针。
             **期望值是对的，只有命名错了**。task-10 §10.6 有同一处笔误，
             说明这是文档模板层面的问题，不是某一份文档的笔误。

    2.完成：【Phase 1 第二个任务 · MSPM0G3507 差速固件】
          1) `src/firmware/mspm0_diff/` —— 算法层（差速逆/正解、命令表分发、
             编码器测速）+ 移植层接口 + 两份移植层实现。
          2) 49 个 Host 单元测试全绿：运动学 14（§11.6 六个必测 + 曲率保持 + 饱和阈值
             + 往返 + NaN/NULL/非法几何）、协议 17（命令表 + 0x10 扩展 + 黄金帧
             + 跨板帧兼容 + 空闲重同步）、编码器 11（正反向回绕 + 窗口保持
             + EMA 系数 + 方向符号）、整链闭环 7（直线/弧线/原地旋转/非对称负载/
             饱和路径/急停复位）。
          3) `docs/decisions/ADR-0004.md` —— TI 电赛合规主控选型 + 移植层分离，
             四条决策逐条给理由与代价，否决方案 B/C/D 各写明理由。
          4) CI 新增 `build-mspm0-firmware` job。
          5) 电赛扩展预留：协议 CMD 0x10 + 4 路 ADC + 8 路 GPIO。
             刻意**不注册**处理器 —— 未注册时明确回 NOT_IMPLEMENTED 而非假装 ACK。

          【一条刻意为之的残缺】
          6) 默认构建剖面 `ci-link` 的产出**不是可烧录固件**。TI 官方路径是
             DriverLib + SysConfig 生成引脚配置，而 MSPM0 SDK 无法在 GitHub Actions
             上免登录安装。两条路：(a) 凭印象手写 MSPM0 寄存器地址 —— 能编过、
             CI 会绿、看起来很完整，然后在某个人真的烧录时以最难排查的方式失败；
             (b) 把移植层收窄成 15 个原语，默认空实现，并在四处标明不可烧录
             （mspm0_conf.h 注释 / README §2 / 构建横幅 / 产出文件名 -ci-link 后缀），
             外加 make flash 在该剖面下直接拒绝执行。选 (b)。
             被空实现掉的**只有那 15 个寄存器原语**，其余全部是真实且测过的代码；
             连 1kHz 控制中断也是真跑的（SysTick 是 ARM 内核外设，与 TI 无关）。
             task-10 敢手写 STM32F4 寄存器层，是因为那份映射公开且能逐条核对 ——
             这不是同一种情况，所以不该用同一种做法。

    3.失败：a) `test_estop_reset_clears_integrator` 首次红：我拿"推进电机模型**之后**
             的转速"去核对"推进**之前**算出的占空比"，差了一个积分步长。
             这是测试自身的 bug。修正时顺手把断言写精确了 —— 现在它钉死复位后
             第一拍的精确构成（微分被跳过、积分只累加一个周期），
             比原来那个宽容差的近似断言更有价值。
          b) 本机仍无 `arm-none-eabi-gcc`，交叉编译未在开发机执行。
             已用 `gcc -fsyntax-only` 对全部 8 个固件源文件做语义检查（零警告），
             链接期问题只能等 CI 首次运行暴露。与 task-10 同一限制。

    4.小结：task-11 真正的产出不是"又一份固件"，而是两件被证明的事：
          共享库的边界画对了（common/ 一行未改），以及移植层分离能把
          HAL 逻辑变成可测逻辑。第二件事回过头看应该在 task-10 就做。
          另外这次刻意交付了一份"残缺但诚实"的固件 ——
          在无法核对寄存器映射的前提下，把边界标清楚比把它填满更负责任。

    5.下一步：task-12（树莓派部署）与 task-13（CI 流水线扩展）均无阻塞。
          task-13 现在有两个固件 job 模板，且多了一类"构建了但不可烧录"的 job 形态可参照。


###### 2026/7/29（task-11 评审收口）
    1.发现：a) 评审问"上板检查清单里那条『松手后故障位自动清除』说明什么"，
             答案对本任务不利：**移植层分离救了 encoder.c，没救 main.c**。
             update_stall_detection() 与故障位置/清逻辑是 static 函数，
             当时 49 个用例一个都覆盖不到 —— 而 task-10 评审用真实缺陷换来的
             三条契约恰恰全在那里。同一条经验只做了一半。
          b) 三条契约里最关键的第 3 条（停机期间 STALL 保持旧值）是**位间优先级规则**。
             我上一轮刚给它补了协议注释，但注释不是验证 ——
             抽出模块之前，整个仓库没有任何一处能证明它成立。
             教训：给一条契约写注释，和让它可被验证，是两件事。
          c) 评审称"port_driverlib.c 不在仓库中"，这一条是事实错误（它在，251 行，
             15 个原语齐全）。但背后的直觉指对了真问题：**它从未被任何编译器读过**。
             "缺失"不如"存在但未经验证"危险 —— 后者看起来是现成的。
             README 原文确实暗示"配好 SDK 路径就能编"，已改为明说。

    2.完成：1) 新增共享模块 `common/faults.c/.h`：故障位图（线上契约，两板逐位一致）
             + 故障状态机（纯函数，无硬件依赖）。位定义从两个 protocol.h 收敛为一份。
          2) `test_faults.c` 21 个用例，三条契约逐条钉死：
             堵转解除即清位 / 累计值不变必须清位 / 停机期间 STALL 保持旧值。
             另覆盖急停锁存与冻结、急停优先于过流、轮数边界夹紧、NULL 安全。
          3) mspm0_diff/main.c 改为只采集输入与执行处置，不再自己拼位图。
             stm32_mecanum 已共用位定义，判定逻辑迁移属 task-10 范围，记入已知限制。
          4) README §7 补强：明说 port_driverlib.c 未经编译、列出 11 个待核对宏名、
             提醒 DriverLib API 签名随 SDK 版本变化。
          5) 链接脚本核对从"对照数据手册"改为"直接 diff SDK 自带 mspm0g350x.lds"，
             并强调**长度也要核对** —— _estack = ORIGIN + LENGTH，写大了第一次压栈就 HardFault。

          【测试】MSPM0 49 → 70，STM32 52 不变，两固件合计 122 全绿。

    3.小结：这一轮真正的收获不是多了 21 个用例，而是看清了一个模式：
          "把逻辑从不可测的上下文里抽出来"这件事，在一个工程里往往要做**不止一层**。
          第一层（寄存器 → 移植层）我主动做了，第二层（故障判定 → 纯函数）
          要靠评审逼出来。下次应该在写 main.c 之前就问一遍：
          这里面有哪些逻辑是"只能靠上板检查清单验证"的？

    4.下一步：task-12 / task-13 无阻塞。
          task-10 的两项对齐（HAL 移植层化、故障判定迁移到 common/faults.c）
          建议合并成一次重构，在其上板之前完成 —— 现在改成本最低。


###### 2026/7/29（task-10 移植层重构）
    1.完成：把 task-11 的两项架构收获回溯应用到麦轮固件，两块板结构就此对齐。
          1) 移植层接口提升为 `common/mcu_port.h`，**两块板共用同一份接口**。
             原先 mspm0_port.h 只服务一块板，接口一分为二迟早漂移 ——
             这正是刚刚在故障位图上消除过的那类问题。
          2) 新建 `port_stm32f407.c`：时钟树、TIM1 PWM、TIM2/3/4/5 编码器、
             TIM6 控制中断、USART1、ADC1、GPIO 全部收拢进去，成为**唯一碰寄存器的文件**。
             `bsp.c/.h` 删除，职责并入移植层。
          3) `encoder.c` / `motor.c` / `uart.c` 变成纯逻辑；`main.c` 改用 common/faults.c。
          4) STM32 Host 用例 52 → 63，新增的 11 个全部是重构前测不了的。

    2.发现：a) 两块板的 UART 发送策略本来不同：STM32 用 TXE 中断驱动，
             MSPM0 就地忙等 FIFO。第一反应是统一成一种 —— 但那会让 STM32
             白白损失一个已有的优点。改用**回调反转**：移植层提供
             `port_uart_tx_start()`（通知有数据），上层提供 `port_uart_tx_next()`
             （取下一个字节）。于是两块板共用同一份环形缓冲逻辑，
             差异只落在各自那一个函数里。
             教训：抽象共同接口时，不该为了"看起来一致"而抹平实现上的合理差异；
             找到正确的切分点，差异可以被完整挡住。
          b) 重构没有引入任何行为变更 —— 时钟树、PWM 载频、编码器模式、
             波特率与中断配置逐字节搬运，原有 52 个用例全部保持通过。
             这一点是刻意维持的：结构调整和行为修改混在一起，出了问题无从二分。

    3.小结：连着三轮下来，真正学到的是一条关于**时机**的判断。
          移植层分离、故障状态机外提，这两件事在 task-10 交付时都可以做，
          但当时没看出来；等 task-11 做了一遍才反过来看清。
          能低成本回溯，唯一的原因是两块板都还没上板 ——
          不存在"已经能跑的不敢动"。
          **结构性缺陷要在硬件到货之前修完**，这是 Phase 1"先固件后上机"
          这个排序真正的价值所在，比"提前把代码写好"这个说法准确得多。

    4.验证：STM32 63/63、MSPM0 70/70，两固件合计 133 全绿。
          gcc -fsyntax-only 覆盖两块板 + common 共 21 个源文件，零警告。

    5.下一步：task-12（树莓派部署）/ task-13（CI 流水线扩展）无阻塞。


###### 2026/7/30（Phase 1 · task-12）
    1.发现：a) 任务文档里同时写了 `privileged: true` 和一份 devices 白名单。
             这两者是矛盾的 —— privileged 已经把宿主机所有设备和全部 capability
             给了容器，那份白名单一行都不起作用。它不是"多余的保险"，
             而是**看起来像做了权限控制**。这类"形似而实无"的配置比没有配置更危险。
          b) 同一类问题还有两处：`setup-3dr-radio.sh` 只建了个连接就打印
             "3DR Radio configured."、一个参数都没设；`check-d435i-usb.sh` 扫描
             系统里有没有任何 5000M 端口，而树莓派5 本身就有 USB3 口 ——
             那个检查**恒为真**，相机插在 USB2 上照样报通过。
             共同点：都会让人以为这一步已经做完了。
          c) systemd 单元里那串 ProtectSystem/NoNewPrivileges/PrivateTmp
             **保护不到边缘节点**。该单元唯一的工作是调用 docker CLI，
             真正的负载跑在 dockerd 创建的容器里，不在这个单元的 cgroup 和
             namespace 内。它们能约束的只是那个几十毫秒的 CLI 包装进程。
             保留是聊胜于无，但不能让它造成"已经上了沙箱"的错觉。
          d) 文档 §12.3 的 `Requires=dev-ttyACM0.device` 与 §12.5 的
             `SYMLINK+="pixhawk"` 单独看都对，**合起来跑不通**：
             只写 SYMLINK 的话 systemd 里并不存在 dev-pixhawk.device 这个单元
             （设备单元名从设备节点真实路径推导），依赖会永远等不到。
             要让符号链接可被依赖需要 TAG+="systemd" 与 SYSTEMD_ALIAS 两件事。
          e) `ChallengeResponseAuthentication` 在 OpenSSH 9.x 已移除，
             而树莓派 OS Bookworm 带的是 9.2 —— 照抄会让 sshd 起不来，
             把自己锁在门外，而这台 Pi 可能已经装在无人机上了。

    2.完成：【Phase 1 第三个任务 · 树莓派部署】
          1) `src/deployment/` 八个子目录 33 个文件：Docker 层（一个镜像两个角色）、
             systemd 自启与健康检查、udev 设备固定、chrony 主从、SSH 加固、
             日志轮转与 journald 持久化。
          2) `validate.sh` —— **本地与 CI 跑同一份**的静态校验入口，7 类检查。
             查不了的项明确报 SKIP 并说明原因，不静默略过。
          3) `healthcheck/agcheck.py` 把健康判定做成纯函数，31 个 Host 单元测试，
             不需要 ROS、不需要网络、不需要树莓派就能跑。
             这是把固件那条"逻辑与 I/O 分离"的经验搬到了部署层。
          4) `validate_consistency.py` —— 跨文件一致性：compose 结构、
             以及 agcheck 的必需话题与 ROS 侧 edge yaml 的话题名是否对齐。
          5) ADR-0005（容器化部署，5 条决策 + 3 个否决方案）
             与 ADR-0006（静态 IP 与时钟主从，4 条决策 + 3 个否决方案）。
          6) CI 增至 5 个 job：新增 validate-deployment 与 build-edge-image（ARM64 + qemu）。
          7) 与任务文档共 **14 处偏差，全部是"照抄会失败"的问题**，逐条给了理由。

          【先落地 .gitattributes】
          8) systemd / sshd / udev / Dockerfile 都是逐行解析的，CRLF 会让它们
             在 Linux 上直接失效。这与 7/25 的编码事故是同一类问题，
             日记里那条教训是"`.gitattributes` 必须作为第一批提交"。
             因此本任务先写属性规则、再写被它保护的文件。

    3.失败：a) **校验脚本自己的负向测试抓出了一个假绿灯。**
             写完 validate.sh 后故意塞进去三样东西看它会不会红：
             一个语法错误的脚本、一个 CRLF 的 unit、一份假私钥。
             前后两项正常红了，**中间那项没有**。
             原因是 Git for Windows 附带的 MSYS grep 在读入时会静默剥掉 CR，
             `grep $'
'` 在 Windows 上永远匹配不到；而 Linux CI 上 grep 行为正常。
             结果就是本地永远绿、真出问题时本地反而发现不了 ——
             属于最坏的一类假绿灯。改用"剥掉 CR 前后字节数是否变化"后三项全红。
             教训：**一个从来没红过的检查，和没有这个检查是等价的**，
             而它还会让人以为已经查过了。校验脚本必须自己先过负向测试。
          b) 本机是 Windows，没有 Docker 也没有 systemd，因此
             `docker build` 与 `systemd-analyze verify` 都没在本地跑过，
             由 CI 首次验证。ARM64 构建更是只能在 CI 上做（需 buildx + qemu）。
             与 task-10/11 的交叉编译是同一类限制。

    4.小结：这一轮反复遇到的是同一个模式 —— **"看起来做了但其实没做"的配置**。
          privileged 配 devices 白名单、只打印不设置的电台脚本、恒为真的 USB3 检查、
          保护不到目标的 systemd 加固，四处形态不同但性质一样。
          它们比"缺了这一步"更危险：缺了会被发现，形似而实无不会。
          而最讽刺的是我自己也写出了第五个 —— 那个永远不会红的 CRLF 检查。
          所以这一轮真正的收获不是那 33 个文件，是那次负向测试。

    5.下一步：task-13（CI 流水线扩展）现在有三类 job 模板可参照（固件 / 部署静态校验 /
          跨架构镜像构建）。task-14 需要交付 car_edge_real.launch，
          在此之前 entrypoint 会自动降级到仿真同款并打印说明。


###### 2026/7/31（Phase 1 · task-12 评审收口）

    1.问题：a) **CI 抓出一个本机永远看不到的错误。** `StartLimitIntervalSec` /
             `StartLimitBurst` 我写在了 `[Service]` 段。systemd 从 v230 起把这两个
             键移到了 `[Unit]`，旧名 `StartLimitInterval` 留了 `[Service]` 兼容别名、
             **新名没有**。systemd 的处理方式是打一句
             "Unknown key name … ignoring" 然后静默忽略 ——
             单元照常启动、`systemctl status` 一切正常，而限流从未生效。
             真正的表现要到"配置写错 + 无休止 10 秒重启循环"时才暴露。
          b) 顺着 CI 的输出还发现 validate.sh 自己有个归类 bug：
             `systemd-analyze verify` 会**连带加载依赖单元**并把它们的错误一起打印。
             healthcheck.service 有 `After=air-ground-*-edge.service`，
             于是四个单元全红、行号指向别的文件，而真实错误只有两处。
          c) 评审同时指出四点：host 网络的安全代价没有重估触发条件、
             离线镜像更新还是手工流程、14 处偏差只躺在部署 README 里、
             以及 `car_edge_real.launch` 的降级只有一条 echo，上位机看不见。
          d) 自查还发现 `air-ground-healthcheck.service` 里有一句注释写着
             "PartOf 让边缘节点停掉时健康检查也一起停" —— 而文件里**根本没有
             PartOf**。一条描述不存在配置的注释，正是我在 4.小结 里批评过的
             那类"看起来做了但其实没做"，只不过这次载体是注释。

    2.完成：1) **段归属自查（不依赖 systemd）。** 用 awk 跟踪当前段名，
             检查 `StartLimit*` 是否落在 `[Unit]`。这类"写错段被静默忽略"的错误
             本机（Windows）根本查不到，把它固定成一条本机也能跑的检查，
             并做了负向测试确认能真的变红。
          2) `systemd-analyze verify` 改为一次性校验全部单元、不按单元归类。
             中间试过按文件名过滤来归类，但那样"不带文件名的错误"会被静默丢掉 ——
             又一个假绿灯。宁可一次报全，systemd 的报错本来就自带 路径:行号。
          3) **降级状态贯通到容器外**（评审 4）。entrypoint 在 roslaunch **之前**
             把结论写进 bind mount 的 `edge-state.env`，`agcheck.py` 读它并以
             退出码 **4** 报出；`SuccessExitStatus=4` 让 systemd 不把预期降级
             标成 failed，`alert.sh` 映射为 warning 而非 critical。
             新增 14 个 Host 用例（31 → 45），其中四个专门钉优先级：
             Master 掉线、话题缺失、未知角色都必须**盖过**降级。
          4) `scripts/update-image.sh`（评审 2）：U 盘自动发现 → SHA-256 校验 →
             **架构核对** → 空间预检 → 备份旧镜像 → load → 可回滚。
             价值不在少敲几行，在把这四步变成必然执行。默认不重启。
          5) ADR-0007（评审 1）：`network_mode: host` 的暴露面清单 +
             三条 Phase 2 重估触发条件（接外网 / 迁 ROS 2 / 同机跑互不信任负载）。
             ADR 不可变，故不改 ADR-0005 而另立一份。触发条件同时挂进
             ROADMAP 的"待重估的技术决策"——不指望有人回来读 ADR。
          6) task-12 的"可执行步骤"顶部加了"不要照抄"警告框（评审 3），
             含最容易踩的四条摘要和偏差表链接。原文一字未改：
             它记录的是当初的设想，改掉就看不出实现过程中学到了什么。
          7) 降级契约的四端（entrypoint / agcheck / systemd / alert）
             加进 validate.sh 交叉校验，与固件那边用黄金帧锁串口协议同一用意。
             三条断开路径都做了负向测试。

    3.失败：a) ADR-0005 §决策-3 把 host 模式的理由写成"ROS 1 的多播与动态端口"。
             **ROS 1 不用多播** —— 它的发现是中心化的（节点向 Master 注册，
             Master 告知对端地址后直连）；用多播发现的是 ROS 2 的 DDS。
             真实理由是节点端口由内核随机分配、ROS 1 没有任何配置项能约束成范围，
             因此 `bridge` 模式声明不出 `ports:`。结论没错，理由写错了。
             ADR 不可变，原文保留，更正记在 ADR-0007 开头。
             教训：**决策对不等于理由对**，而后来者复用的是理由。
          b) 那条幽灵 PartOf 注释暴露了另一件事：`PartOf` 写在
             `healthcheck.service` 上本来就没用 —— 它是 `Type=oneshot`，
             停一个没在运行的 oneshot 是空操作。要抑制停机噪声，必须停掉
             **周期触发它的 timer**。所以不是"补上漏写的一行"，
             是那行本来就该在另一个文件里。改成 `.timer` 上的 `PartOf`，
             并在 service 里写清为什么不在这儿。

    4.验证：部署静态校验 25/25 通过（原 21），含 45 个 Host 用例（原 31）。
          三条新检查（段归属 / 降级契约 / 状态文件名）逐条做过负向测试。
          固件回归 STM32 63/63、MSPM0 70/70，合计 133 全绿 —— 本轮未动固件，
          跑一遍是确认没有连带影响。

    5.小结：7/30 那条小结说"这一轮真正的收获是那次负向测试"。这一轮把它推进了一格：
          **负向测试要覆盖到本机跑不了的那部分**。CRLF 那次是本机能查却查错了，
          这次的 `StartLimitIntervalSec` 是本机根本没有 systemd、连查都查不了 ——
          于是错误一路走到 CI。补救不是"依赖 CI"，是**把 CI 才能发现的错误类型
          反向翻译成一条本机能跑的检查**。段归属自查就是这么来的：
          它查不了完整语法，但能查"这个键是不是又写错段了"，而那正是踩过的坑。

    6.下一步：task-13（CI 流水线扩展）。本轮给它多留了一条经验 ——
          CI 的价值不只在拦住错误，还在**告诉你本机缺哪类检查**。
          task-14 交付 `car_edge_real.launch` 时，需要把降级判定从状态文件
          迁到 ICD 的 `Capability` 消息，状态文件退化为启动自检记录。

    7.补记（同日 · ARM64 镜像构建首次运行）：
          build-edge-image 这个 job 之前一直没跑过 —— 它 needs: validate-deployment,
          而后者一直红着。段归属修好之后它第一次真正执行, 然后挂在
          `pip3 install pymavlink` 上。

          诊断链条: Focal 的 python3-pip 是 **20.0.2**, 而 PEP 600 的
          `manylinux_2_XX_<arch>` 轮子标签要 pip 20.3 才认识; PyPI 上 lxml 的
          aarch64 轮子正是 `manylinux_2_28_aarch64`。老 pip 认不出就退回去编译
          lxml 源码, 而镜像里没有 libxml2-dev 也没有编译器。lxml 又是 pymavlink
          的**构建期**依赖 —— pymavlink 只发 sdist, setup.py 会在安装时现场生成
          MAVLink dialect, 那一步要 lxml。

          本机是 Windows 没有 Docker, 这条链验证不了。所以没有赌单一判断,
          而是把三条可能的失败路径一起堵上: apt 预装 python3-lxml/python3-future
          (预编译 arm64 deb, 不存在"编译失败"这条路)、pip 升到 <25
          (pip 25.0 起不支持 Python 3.8)、版本约束补上 requirements.txt 的上界。
          **这一点在部署 README §9 里明写了"该修复同样未在本地验证"** ——
          未验证的修复不该看起来像已验证的。

          顺带发现两件事:
          a) Dockerfile 的注释写着"与 requirements.txt 对齐", 而上界 `<3.0.0`
             从没同步过来。又一条"声称做了但没做"。已补交叉校验并做负向测试。
          b) 更要紧的: drone_car_bridge.py 里 pymavlink 是 **try/except 可选导入**。
             装不上不报错, 只会退回自己手写的 v1 解析器, 然后把 MAVLink **v2**
             帧整个丢掉 (只留一条 logwarn_throttle) —— 而 Pixhawk 6C 默认说 v2。
             也就是说这次 CI 是"幸运地"在构建期挂了; 要是 pip 装了个半成品,
             这个故障会一路潜伏到实机上, 表现为"飞控接上了但收不到心跳"。
             因此在 Dockerfile 里加了一条构建期 import 验证 ——
             **可选依赖的安装失败必须在构建期变成硬错误**, 否则运行期没人看得见。
             CI 的 ROS job 也改成直接 `pip install -r requirements.txt`,
             把手抄版本号这个漂移源整个去掉 (原来漏了 numpy<2.0.0,
             而 numpy 2 会直接搞坏 Noetic 的 cv_bridge)。

          这一条和 5.小结 是同一个模式的两面: 那里说"把 CI 才能发现的错误反向
          翻译成本机检查", 这里是"把运行期才会暴露的静默降级前移到构建期"。
          共同点是**不要让失败发生在没人看的地方**。

---

## 历史名称脚注

[^1]: **air_ground_drone_sitl** 是 `air_ground_drone_bringup` 的早期命名。2026-07-25 全局重构中统一改名，旧名称保留在日记中仅供历史追溯。

[^2]: **air_ground_car_sim** 是 `air_ground_car_bringup` 的早期命名。同上，于 2026-07-25 重构中统一为 `air_ground_car_bringup`。
