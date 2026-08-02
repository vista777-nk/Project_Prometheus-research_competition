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

###### 2026/7/31（Phase 1 · task-13 CI 流水线）

    1.完成：全仓静态检查 job（lint-scripts）落地，CI 增至 6 个 job。
          shellcheck / yamllint / flake8 三项阻塞且钉死版本号，cppcheck 仅告警。
          修掉 4 条 shellcheck warning，删掉两处"永远不会失败的检查"。
          门禁分层由 ADR-0008 固化。

    2.最该记的一件事：**任务文档要求做的五件事，有四件已经做完了。**
          task-13 的文档写于 7/28，那时 CI 只有一个 ROS job。但 task-10/11/12
          在交付各自功能时，各自把自己那个 job 建好了 —— 而且比文档方案更强
          （task-12 的部署 job 有 ARM64 构建和 ROS_IP 缺失的反向验证，
          文档里只写了一个 `docker build` 干跑）。
          真正缺的只有 lint-scripts 一个。

          如果照着"可执行步骤"抄，会重复建四个 job 并**覆盖掉更强的已有实现**。
          这不是文档写错了，是文档必然会过期 —— 它描述的是写作时的仓库状态。
          所以在 task-13 顶部加了一个执行前必读的警告框，
          和 task-12 评审时加的那个"不要照抄"是同一类补救。

          推论：以后每个任务开工前的第一件事是核对现状，不是读步骤。
          实际耗时 1h 而不是预估的 3h，差额全部来自"发现不用做"。

    3.门禁强度这个问题比预想的有代价。两个方向都会坏事：
          全设阻塞 → 没实测过的工具第一轮就把 CI 拍红，代码没问题但得先证明这一点；
          全设非阻塞 → 就会得到仓库里当时已经有的那个东西。

          `validate-deployment` 里那个 shellcheck 步骤是 `continue-on-error: true`
          且末尾 `|| true`，范围只有 src/deployment。**它无论如何都是绿的。**
          一条永远不会失败的检查在 CI 面板上和一条真门禁长得一模一样，
          它把"没查出问题"和"没查"混成了同一个绿勾。已删除。

          最后定的标准是：**能不能阻塞，取决于提交的人有没有在本地跑过全仓。**
          跑过 → 阻塞（并且钉死版本号，否则"跑过"这个前提会随上游升级失效）；
          没跑过 → 告警，且在 job 注释里写明没跑过的原因。
          cppcheck 就是后者：本机既没有 cppcheck 也没有 make，它对固件 C 代码的
          输出一次都没被人看过。设成阻塞就是拿别人的 CI 当试验场。

    4.踩了一次已经被写进注释的坑。用 yamllint 扫 YAML，报了 9 条
          `wrong new line character`。第一反应是"全仓 YAML 都是 CRLF"，
          于是写了个脚本 `git show HEAD:$f | grep -q $'\r'` 去核对全仓 blob，
          结果只有 .gitattributes 一个文件命中 —— 两个结论互相矛盾。

          矛盾的来源恰好是 validate.sh §4 里那段注释写明的事：
          **MSYS 的 grep 读入时会静默剥掉 CR**，所以 `grep $'\r'` 在 Windows 上
          永远查不出 CRLF。那段注释是 task-12 做负向测试才换来的，我又原样踩了一遍。
          改用它推荐的"剥 CR 前后字节数差"法，确认 blob 侧全是 LF ——
          yamllint 那 9 条是 Windows 工作区假象（`.gitattributes` 给 `*.yaml`
          只声明了 `text` 没声明 `eol=lf`）。按 Linux 侧内容重跑，0 条。

          教训有两层。表层是"验证方法本身也需要被验证"。
          深一层是：**这段注释存在，但我是在得出错误结论之后才想起去读它。**
          写下来不等于会被读到。这也是为什么 ADR-0007 的三条触发条件要挂进
          ROADMAP 而不是只躺在 ADR 里 —— 同一个问题，7/31 这天遇到了两次。

    5.小结：这一轮没有写多少代码（净新增一个 job、四处单行修改），
          但删掉的东西比加上的重要 —— 一个 `|| true` 的假检查、
          一个硬编码的 `CI-passing` 假 badge、一份重复的 systemd 校验。

          把 7/30 以来的三条串起来看是同一件事的三种形态：
          假绿灯（CRLF 用 grep 查）、假报告（`|| true` 的 shellcheck）、
          假状态（硬编码 badge）。
          它们都不是 bug —— 每一条单独看都"有这个检查"。
          **它们是在声称已验证，而实际没验证。**
          这比没有检查更糟，因为没有检查的地方人还知道要小心。

    6.下一步：task-14（传感器驱动 + MAVLink 2 签名）与 task-15（标定 + 集成验证）。
          task-14 交付的新代码会第一次真正经过 lint 门禁 ——
          在此之前门禁是对着一份已经干净的仓库设的，它拦住东西才算被证明。
          另需在首轮 CI 后处理 cppcheck：升为阻塞或撤掉，不留在告警态过夜。

---

###### 2026/7/31 晚（Phase 1 · task-14 传感器驱动骨架 + MAVLink 签名）

    1.完成：4 个实机驱动骨架 + 硬件抽象层 + 69 个宿主机用例；
          MAVLink 2 签名脚本 5 条断言全过。决策落在 ADR-0009 / ADR-0010。
          门禁本地实测：flake8 0 · shellcheck 0 · yamllint 0 · pytest 69 passed。

    2.最该记的一件事：**任务文档给的接口，和仓库里已经跑起来的东西对不上，
          三处，而且 ROS 一处都不会报错。**

          | 项 | 文档 | 仓库实际 | 症状 |
          |---|---|---|---|
          | IMU 话题 | /car/imu | /car/imu/data | Observation 永远没有 imu 模态 |
          | 超声波类型 | Range | LaserScan | 订阅端**永不连接** |
          | 雷达角度 | 0..2π | -π..π | 整张地图沿前后轴镜像 |

          这三条照抄的话，CI 会全绿：py_compile 过、单元测试过、catkin build 过。
          没有任何自动化能发现，因为每一条单独看都是合法的 ROS 代码。
          发现它们靠的是开工前把 car_preprocessor.py 的订阅、URDF 的 Gazebo
          插件、car_edge.yaml 三份逐行比了一遍 —— 也就是 7/31 早上刚写下的那条
          "每个任务开工前的第一件事是核对现状，不是读步骤"。

          这一次它值 40 分钟的比对，省下的是实机联调时的一整天。

    3.把这条契约变成了测试而不是注释。
          test_topic_contract.py 交叉比对 real_sensors.yaml × car_edge.yaml ×
          car_sensors.yaml × car_preprocessor.py 的 DIRECTIONS × URDF 的 link 名。
          做法直接抄 validate.sh §7「交叉引用」——
          那一节当初防的是 systemd 单元引用了不存在的文件，同一个形状。

          **注释会过期，交叉引用检查不会。** 7/31 早上那条教训（"注释存在，
          但我是在得出错误结论之后才想起去读它"）的直接对策就是这个。

    4.HC-SR04 那个 10μs 脉冲：文档给的 rospy.sleep(0.000010)，
          在非实时内核上实际精度毫秒级，**差 100 到 1000 倍**。
          文档自己也标了这个问题（ADR-0013 待定），但它把 write/sleep/pulse_in
          三件套留在了接口上。

          处置不是加注释警告，是**改接口形状**：只暴露一个
          trigger_and_measure(trig, echo, pulse_us, timeout_us)，
          Python 层拿不到半成品原语，实现它的人必须真的解决时序。

          接口的形状决定了实现者能犯什么错。这一条比写十行警告有用。

    5.MAVLink 自测脚本原文是"恒假"的：
          两次 pack() 之间只设了 secret_key，没设 sign_outgoing，
          实测 17 → 17，于是 `assert signed_len == unsigned_len + 13` 永远失败。
          外加用了 MAVLink 1 的方言去解 MAVLink 2 的签名帧，报错指向完全错误的方向。

          改对之后顺手把断言从 1 条扩到 5 条。理由：只测"签名后长度 +13"
          证明不了签名有用 —— 一个把 13 个零字节贴在帧尾的实现也能过。
          要测的是**错的进不来**（错误密钥、重放、未签名帧全部被拒），
          不是对的出得去。

    6.两个交付物刻意留成了"未完成"状态：px4-signing.params 的签名段留空，
          mavros_signing.yaml 整份标为核实清单。
          因为参数名一个都没能核实（PX4 v1.14 的签名文档页打不开，
          检索到的官方页只在 main 分支下；本机无 ROS 跑不了 rosparam list）。

          纠结过要不要照抄一个名字填上去 —— 成本 0，交付看起来更完整。
          没填的理由是这两类文件的失败方式：param load 会**跳过未知参数并继续**，
          rosparam 会老实把没人读的键塞进参数服务器。两种情况下
          **配置文件的存在本身，成了"功能已启用"的假证据。**

          这是 7/30-7/31 那串假绿灯的第四种形态：假配置。
          前三种是假绿灯 / 假报告 / 假状态，这一种更隐蔽，
          因为它连 CI 都不经过 —— 它骗的是三个月后在实机现场的人。

    7.小结：这一轮写的代码不少（17 个新 py 文件），但真正决定质量的两处
          都不在代码里 —— 一处是开工前 40 分钟的契约比对，
          一处是把"能犯的错"从接口上删掉。

          补一条现世报：密钥生成脚本的冒烟测试抓到了它自己的两个 bug，
          其中一个是**我刚写的仓库守卫恒不生效** ——
          拿 `git rev-parse --show-toplevel`（`e:/Vista/...`）和 `pwd`
          （`/e/Vista/...`）做前缀比较，Windows 下两者格式不同，比较永远为假。
          实测时它眼睁睁把密钥写进了仓库目录。

          这和 7/30 的 `grep $'\r'` 是同一个成因：**跨平台的路径/行尾差异，
          会把守卫悄悄变成装饰。** 上一次的教训是"验证方法本身也需要被验证"，
          这一次学到的更具体：**写完守卫必须做一次负向测试** ——
          不要只测"该过的过了", 要测"该拦的真的拦住了"。
          今天两次踩同一族问题，隔了不到十二小时。

    8.下一步：task-15（标定 + 集成验证）。它要处理本轮欠下的三笔：
          ① ADR-0013 超声波时序方案 A/B/C；
          ② PX4/MAVROS 签名参数名上机核实；
          ③ /car/openmv/detections 目前没有消费者 —— 实机 OpenMV 在板上做检测、
             不出图像流，Observation 会缺 rgb 模态，这是接口层面的真实缺口。
          另：cppcheck 仍在告警态，已过一夜，需要处理。

---

###### 2026/7/31 深夜（Phase 1 收官 · task-15 标定工具链 + 集成验证）

    1.完成：标定工具链 7 个脚本 + 归档规范；集成验证三件（串口回路 / Observation
          数据流 / Phase 1 冒烟）。决策落在 ADR-0011。
          本地实测：标定流水线 16/16 · 串口协议 13/13 · Observation 数据流 10/10 ·
          task-14 既有 host 用例 69/69 无回归 · 冒烟 60 项全绿 0 SKIP ·
          门禁 flake8 0 / shellcheck 0(v0.10.0, 与 CI 同版本) / yamllint 0(按 index 内容)。
          **Phase 1 六个任务全部完成。**

    2.任务文档过期，这次是七处，其中一处会烧掉两小时。

          task-15 原文的采集脚本要录五个话题：/drone/rgb/image_raw、
          /drone/rgb/camera_info、/drone/depth/image_raw、/drone/imu/data_raw、
          /car/imu/data_raw。**仓库里一个都不存在。**

          而 `rosbag record` 订阅一个没有发布者的话题**不报错** ——
          它就那么等着，录出一个 0 条消息的 bag。相机标定要人举着标定板站两分钟；
          IMU 标定要静置**两小时**。这一类错误的代价不是"重跑一次脚本"。

          7/31 早上写下"每个任务开工前的第一件事是核对现状"，晚上 task-14 靠它
          抓到三处；深夜这一轮抓到五处。同一条纪律，三次都值回票价。

          现在话题名由 validate_consistency.py 的交叉引用测试守着（负向测试跑过：
          改回 /car/imu/data_raw 立刻红），脚本另在开录前跑 rostopic list 逐个确认在线。
          静态检查防"抄错名字"，运行时检查防"名字对但节点没起来" —— 两种都会录出空 bag。

    3.假绿灯的第五种形态：**假测试** —— 测试测的是它自己的替代实现。

          原文的 §15B.2 写了一个二十行的 preprocess_to_observation()，
          注释是"模拟 car_preprocessor.py 的核心逻辑"。三个用例一定会通过，
          因为它们测的就是那二十行。

          而真的 build_messages() 里有五件模拟件一件都没有的事：
          新鲜度窗口、LiDAR 降采样与 angle_increment 同步放大、无效距离写 -1.0、
          超声波限幅与缺失填 max_range、modalities 由新鲜度生成。
          全是实机上真正会出问题的地方。举一个：雷达线松了、节点不报错，
          没有新鲜度窗口的话 Observation 会一直带着几秒前的旧 lidar_ranges，
          下游拿它当当前障碍物。

          改成用 task-14 那套 ROS 替身跑**真的** CarPreprocessor 和
          **真的** WorldModelStore。为此给替身补了 15 个消息类；cv2 用真的，
          所以 JPEG 压缩那一段是真跑的。

          从这里长出一条评审红线（ADR-0011 §方案 G）：
          **替身可以替环境（ROS、硬件、时钟），不能替被测对象。**

          现世报又来了：补替身的第一次运行就崩在
          `rospy.Duration(1.0 / publish_rate)` —— car_preprocessor.py 的原话 ——
          因为替身只收关键字参数，而真 rospy.Duration 收位置参数。
          **在那之前没有任何测试构造过 Duration。** 替身写了两天，
          今天第一次让真节点跑上去，缺陷当场掉出来。
          现在 test_ros_stub_fidelity.py 拿真 rospy 逐项比对行为，不只比字段名。

    4."RMS 小"不等于"标定对"，这是本轮最该记住的一条方法论。

          原文要求 sample_calib_data/ 放 5 张真实棋盘格照片。问题是
          **真实照片的真值是未知的** —— 拿一组照片标出 fx=520，没有任何办法
          判断这个 520 对不对，只能看 RMS。而 RMS 衡量的是"这组解与这组观测
          有多自洽"，不是"这组解对不对"：一个把所有角点坐标同时缩放 1.1 倍的 bug
          会让 fx 变成 572，**RMS 纹丝不动**。

          改成合成真值：按已知 K 与畸变反向渲染 12 张棋盘格，CI 里现生成现标定，
          断言"解出来的 fx 与设进去的 fx 差多少"。实测 fx +0.18%、cx -0.06px、
          RMS 0.137px、gyro 噪声密度 +1.0%。

          代价写进了脚本 docstring：合成数据证明不了 plumb_bob 拟合得了真实镜头
          （图就是按那个模型画的，自己考自己），也没有运动模糊和卷帘快门。
          那些只能上机验。**知道一个测试证明不了什么，和知道它证明了什么同样重要。**

    5.两个"照抄就会错"的实现细节，都是实跑才发现的：

          · `cv2.norm(img_points[i], projected, cv2.NORM_L2)` 在 OpenCV 5.0 上
            **直接抛异常**：4.x 的 findChessboardCorners 返回 (N,1,2)，5.0 返回 (N,2)，
            而 projectPoints 一直是 (N,1,2)。改成 reshape(-1,2) 两版都对。
          · 手算的 RMS 与 cv2.calibrateCamera 的返回值 ret **是同一个数**
            （实测比值 0.999997）。原文把它们当两个指标分别打印。

          顺带一个数值上的发现：原文的焦距合理性区间 0.5w < fx < 2.0w 换算过来是
          HFOV ∈ (28.1°, 90.0°)，而 **D435i 的深度流标称 87°±3°，上沿正好压在边界上** ——
          一次完全正常的标定有可能被判成"焦距不合理"。改成按视场角定义 [20°, 120°]。

    6.把"为什么要静置两小时"算清楚了，而不是当成仪式。

          Allan 曲线白噪声段 σ=N/√τ、随机游走段 σ=K√(τ/3)，交点在 τ=√3·N/K。
          本项目 IMU 的量级代进去约 173 秒；要在 +1/2 段上取到有统计意义的点，
          τ 得是交点的几倍，重叠式 Allan 方差每个 τ 还要留够聚类数（总时长 ≥ 10τ）。
          于是落在小时量级。

          15 分钟能解出噪声密度和零偏，解不出随机游走。所以 calibrate-imu.py 在
          数据不够长时把 quality.random_walk_reliable 置 false 并退出码 1，
          validate-calibration.py 因此判失败 —— 这是设计如此：一个凭空外推出来的
          随机游走系数会被 robot_localization 原样采信，而它的错法是让滤波器
          **看起来在工作**。

          加速度计零偏还有一层：静置水平假设下，摆放倾斜 0.5° 就会造出
          0.086 m/s² 的假零偏 —— 与真零偏同量级。所以 YAML 里带
          accel_bias_method: static_level_assumption 自曝，六面翻转留给 Phase 2。

    7.欠账清点（task-14 §8 那三笔 + 新增）：

          ① 超声波时序方案 A/B/C —— **未处理**。另：那条日记里写的编号"ADR-0013"
             现在不成立了，0011 已被标定格式占用；下一个可用编号是 0012。
          ② PX4/MAVROS 签名参数名 —— **仍未核实**（本机无 ROS、无 PX4），
             已随其余三条一起写进标定 README §5 的上机核实清单。
          ③ /car/openmv/detections 没有消费者 —— 这一轮把它变成了**可执行的检查项**：
             车机相机标定要录 /car/openmv/image_raw，而 openmv_bridge.py 只发 detections。
             record-calib-bag.sh 会在开录前发现没有发布者并拒绝开录，
             README §5 第 5 条写明了这一点。缺口没堵上，但不会再让人白站两分钟。
          ④ cppcheck 仍在告警态 —— **未处理**，已过两夜。首轮 CI 输出出来后必须决断。
          ⑤ 新欠：本机是 Windows + Python 3.14，numpy<2.0.0 没有 cp314 轮子，
             **装不上 CI 钉的版本**。所有标定数值实测于 OpenCV 5.0.0 / numpy 2.5.1，
             与 CI 的 4.8~4.12 / 1.x 不是同一套。代码只用两版都稳的 API 并做了形状归一
             （第 5 条那个 reshape 正是这个差异咬出来的），但"CI 上的数与本地一致"
             这件事，第一次 CI 运行之前没有被验证过。ADR-0011 §影响里写了这一条。

    8.小结：Phase 1 结束。回看 7/30 到 7/31 这两天，真正反复出现的不是技术问题，
          是**同一个认识论问题的五种化身**：假绿灯（CI 永远不会失败的检查）、
          假报告（全是勾但什么都没查）、假状态（降级了却显示健康）、
          假配置（文件存在被当成功能启用）、假测试（测的是自己的替代实现）。

          五种形态的共同结构是：**一个本该提供证据的东西，变成了它自己的证据。**
          对应的解法也只有一条 —— 每写一个守卫，就做一次负向测试，
          确认它该拦的时候真的会拦。这一轮把它写进了 CI：
          smoke_test_phase1 job 里有一步专门删掉一个交付物，
          冒烟测试必须因此失败。一个永远绿的"毕业证书"比没有证书更糟。

    9.下一步：Phase 2（EQA 论文）之前，先清 ⑦ 里的四笔欠账；
          实机到手第一天按标定 README §5 逐条核实那七项，结论写回日记。

---

###### 2026/8/1 凌晨（task-15 收尾 · 三笔悬账清掉两笔）

    1.首轮 CI 回来了，而且是能直接读到的 —— 仓库有 origin，commit 已经在远端，
          GitHub 的公开 REST API 不需要 token 就能读 run 列表和**每一步的结论**。
          run #25 (fe0cf66)：冒烟 ✅ 部署校验 ✅ lint ✅ 两个固件 ✅ ROS ✅。

          部署校验那个 job 装了 CI 钉的 numpy<2 + opencv 4.x 并真跑了标定流水线 ——
          也就是说 7/31 深夜记的那笔"版本差异要等 CI 首轮"，答案是**通过**。

    2.但"通过"只说明在容差内，说明不了两套版本的数是不是真的一致。
          容差留了 3~10 倍余量，**系统性偏移完全可以躲在里面**。

          所以给 test_calib_pipeline.py 加了一张实测值对比表，跑完就打印
          本次的 fx/cx/RMS/噪声密度 和 7/31 的本地基准并排。
          下一次 CI 的日志里能直接读到逐项相对差，不必再靠推断。

          这条其实是"知道一个测试证明不了什么"的续集：
          断言回答"在不在范围内"，对比表回答"和另一套环境差多少"。
          两个问题不一样，只有第一个有断言的时候，第二个就没人会问。

    3.cppcheck 的悬账，用一个没想到的办法结掉了。

          job log 的下载接口要 admin 权限，读不了。但**不需要读日志**：
          那一步带 --error-exitcode=1，而 Actions 的 jobs API 会单独记录
          每一步的 conclusion，continue-on-error 只改 job 的成败、
          不改写步骤自己的结论。

          查出来：run #24 (task-14) 和 #25 (task-15) 两次，
          cppcheck 步骤退出码都是 0 —— **两次零发现**。

          于是升为阻塞。ADR-0008 当初留的条件就是"等它的输出被观测到"，
          现在观测到了。零发现的检查设成阻塞没有代价；
          而"没噪声所以撤掉"是说不通的 —— 有噪声才是撤掉的理由。

    4.升的时候撞上一个取舍：ADR-0008 要求阻塞工具全部钉死版本，
          而 cppcheck **没有官方预编译 Linux 二进制**，钉版本只能源码构建，
          每次 CI 加 3~6 分钟；apt 指定版本号在 runner 换镜像时会直接消失，
          失败点从"lint 报错"搬到"装不上"，同样是一次与代码无关的红灯。

          回去看 ADR-0008 那条要求的**目的**：让看红灯的人能区分
          "代码退化了"和"工具升级了"。钉版本只是达成它的一种手段。
          所以改用另一种手段 —— 运行前打印版本号，失败时把两种可能
          直接写进报错文案。代价写明：判断从"自动"降级成"人读一行"。

          这算是学到的一条：**规则写下来是为了达成某个目的，
          换环境时该问的是"目的还成立吗"，不是"规则还能照抄吗"。**
          直接照抄会得出"必须源码构建"，直接无视会得出"不用管版本"，
          两个都不对。

          报错文案最后一句是刻意写的：「不要把这一步改回 continue-on-error」。
          一个被降级回告警态的门禁，就是这次要解决的那个问题本身。

    5.欠账更新：
          ① 超声波时序方案 A/B/C —— 仍未处理，编号确定为 ADR-0013
             （0011 给了标定格式，0012 给了 cppcheck，正好接回 task-14 日记的原编号）。
          ② PX4/MAVROS 签名参数名 —— **仍未核实**，本机没有 ROS 也没有 PX4，
             这一笔只能等实机。已在标定 README §5、ADR-0010、ADR-0011 三处挂着。
          ③ /car/openmv/image_raw 有没有发布者 —— 同上，等实机。
          ④ cppcheck —— **已结**（本条第 3、4 点）。
          ⑤ numpy/OpenCV 版本差异 —— **已结**（CI #25 通过），
             逐项数值对比等下一次 CI 的日志。

          另：本机的 cppcheck 到最后也没装上（Windows 安装包 20 MB，
          实测下载速率约 10 KB/s）。所以升级依据完全来自 CI 的步骤退出码，
          没有本地实测 —— 这一点写进了 ADR-0012 §影响，不假装有。

###### 2026/8/1（Phase 1.5 接手 · 实验室服务器基线与部署失败关闭）

    1.接手后的第一件事不是接硬件，而是验证前任留下的「已完成」到底有多少真证据。

          当前机器正是 Ubuntu 20.04.6 实验室服务器，ROS Noetic、catkin、Docker 都在。
          `make smoke-phase1` 得到通过 60、失败 0、跳过 1；跳过项仅是本机没装 pymavlink，
          没把它写成通过。

          第一次 `make build` 失败不是代码错：仓库从 `research_compitition` 重命名后，
          Catkin 的 build/devel/install 缓存仍嵌着旧绝对路径。把四个生成目录完整保留到
          `/tmp/project-prometheus-catkin-cache-before-rename/` 后重建，6 个项目包全部成功。
          这说明工作区改名不只影响编辑器设置；生成缓存也是路径状态的一部分。

    2.交接清单 U1 关闭：真实 `catkin test --no-status` 原始退出码为 0，
          80 个测试、0 error、0 failure、0 skip。其中真 ROS 消息对照是 24 条，
          不再是「替身与替身互相证明」。

          有了测量，才删除 CI 里的 `|| echo`；同时去掉 `source devel/setup.bash || true`。
          测试失败或构建后没有运行环境，现在都会真阻塞。决策见 ADR-0014。

    3.交接欠账 D8 关闭，但没有假装真实驱动已经完成。

          原链路是 `EDGE_MODE=real → car_edge_real.launch 默认 mock → 假数据 → 健康`。
          改成三个互斥模式：real 只传 real，mock 必须显式且状态为 DEGRADED，
          sim 才走仿真 launch。launch/YAML/节点三层缺省也全部从 mock 改为 real，
          防止绕过 Docker 后重新出现同一问题。

          关键不是正向三条，而是负向两条：未知模式必须失败；real launch 缺失时
          必须失败、不得回退。`test_entrypoint_modes.py` 运行的是真 entrypoint，
          只替换 rospack/roslaunch 环境，随后补上缺 `ROS_IP` 的负向路径，共 8 条。
          决策见 ADR-0015；ADR-0013 仍保留
          给需要实测 GPIO 时序的超声波 A/B/C 方案。

    4.部署校验在这台服务器第一次真跑 systemd-analyze 时又抓到校验器环境漂移：
          它扫描了 `/etc/systemd/system` 的宿主机单元，把 snapd 的版本告警算到项目头上；
          受限环境的 Varlink socket 失败也被当成 unit 语法错。

          现在 `SYSTEMD_UNIT_PATH` 只含项目与发行版目录，并只过滤具体的 Varlink/部署路径
          环境信息，通用 parse 错误仍保留。最终 validate.sh：通过 46、失败 0、跳过 1。

    5.下一步严格按硬件风险递增：先完成服务器仿真/E2E 基线，再做 STM32 串口真回路，
          随后逐个落 UART/I2C 真后端；HC-SR04 必须先测时序再写 ADR-0013；
          最后才处理 PX4 签名 A/B 对照与飞行。当前 `EDGE_MODE=real` 因真后端未实现而失败，
          这是正确的红灯，不是新的阻塞策略缺陷。

###### 2026/8/1（Phase 1.5 接手续 · UART/I²C 与实验室服务器真入口）

    1.完成 UART / I²C Linux 访问层：pyserial 串口、SMBus 寄存器读写、依赖与镜像安装
          同时落地。配置里的 ttyUSB0/ttyAMA2 也改成 udev 稳定名，并由 YAML / compose /
          udev 三方契约测试守住。错误 ICM42688 WHO_AM_I 会在写寄存器前停止；RPLIDAR
          半截握手不会再被当成连接成功；运行中拔线进入退避重连。

    2.真实 roslaunch 抓到一个 Host 测试完全看不到的问题：catkin_install_python 在 devel
          空间生成 relay，驱动顶层 import hardware_interface 时先命中同目录的同名 relay，
          导入到的模块只有 relay 外壳，没有任何接口类。于是 81 条 Host 测试可以全绿，
          真 ROS 节点却在 import 阶段全死。修复为驱动优先 relay 注入的真实源码目录；
          mock 五节点持续运行 8 秒，real 无设备时由 required 节点让整套关闭。

          这再次说明：纯 Python 测试验证的是逻辑，catkin relay / roslaunch / 参数装载属于
          另一层环境事实，不能由前者代替。好在 required=true 让这次故障直接停机，
          没留下只会发预处理空消息的假健康进程。

    3.服务器侧也发现 Phase 0 入口不能直接当部署入口：server_only.launch 会同时启动
          TCP 服务端与 edge_server_bridge 客户端连回 127.0.0.1。连接成功只证明机器能连
          自己。新增 lab-server-real.launch，只保留五个服务端节点；同时把客户端通告地址
          server_ip 与服务端监听地址 server_bind_ip 分开。决策见 ADR-0016。

          在真实服务器以 ROS_IP=192.168.3.30 启动后，roslaunch 对外注册到该地址，
          tcp_receiver.py 真监听 0.0.0.0:9090；从现网地址发送一帧 4-byte 长度前缀 JSON
          heartbeat，/server/car/state 成功收到 robot_id=car。确认没有本地 bridge 客户端，
          实验结束后 ROS 进程与端口均已清理。

    4.真实 heartbeat 首帧还显示 RobotState 的 orientation.w=0：没有 pose 时 ROS 消息
          的全零默认值不是合法四元数。修为单位四元数并加入同形态回归。

          最终验证结果：6 包构建成功；catkin test 82/82；传感器 Host 81/81；
          validate.sh 46 通过、0 失败、1 跳过；make test-all 全套通过；最终 E2E 33/33。
          E2E 最终复测第一次在模型已成功 spawn 后遇到一次 Ogre AABB 断言，gzserver
          随即退出；确认无残留后从干净环境重跑全绿，因此记为仿真图形栈偶发故障，
          没有把失败尝试从实验记录里抹掉。新增 ruff 检查与 git diff --check 均无发现。

    5.仍不能关闭 Phase 1.5：服务器当前只有 192.168.3.30，规划的机器人隔离网
          192.168.1.100 未获授权配置；chronyc 未安装；没有任何机器人 USB/串口设备接入；
          GPIO/HC-SR04 仍需 ADR-0013 实测；PX4、MAVROS 签名与真实标定也都没有硬件证据。
          已完成的是可执行入口与失败边界，不是整机联调。

###### 2026/8/1（Phase 1.5 接手续 · 中关村服务器常驻与跨校区边界）

    1.负责人补充了一个会改变部署假设的事实：硬件在良乡组装，服务器留在中关村，
          两地链路还可能不稳定。这不是把 192.168.1.100 换成另一个 IP 就能解决。
          ROS 1 除 Master 11311 外还有每个节点随机绑定的 XML-RPC/TCPROS 端口，直接跨
          校区会把短时断线变成半失效 ROS 图；现有 TCP JSON 又没有 TLS/对端认证，
          不能把 9090 直接开放到校园网。ADR-0007 的外网重估条件已经触发。

          ADR-0017 因此把边界改成：服务器 ROS 永远本地；跨校区只跨一个带重连的 TCP
          会话，而且必须先经过经批准的 VPN/SSH 隧道。具体产品和地址等网络/硬件参数
          确认后再填，不把猜测写进生效配置。

    2.服务器审计还有两个容易在远程阶段出事故的事实。根分区已用 97%，只剩约 87 GiB，
          而 /data2 还有约 1.1 TiB；真实服务器入口又只能依赖当前 SSH 终端手工启动。
          时间侧则比“没有 chronyc”更好一点：systemd-timesyncd 当前明确显示
          System clock synchronized=yes，所以没有为了装 chrony 而替换一个正在工作的
          时间服务；仍需等两台 Pi 到位后实测三机相对偏差。

    3.新增服务器 systemd 安装器、生产启动脚本、五节点+TCP 联合健康检查，以及
          system/user 两套 unit。生产默认把 ROS Master 与 TCP 都钉在 127.0.0.1，
          五个服务节点全部 required；roslaunch 即使返回 0，Restart=always 仍会恢复。
          ROS 日志固定写 /data2/air-ground-server/ros-log。

          系统级安装需要交互 sudo 密码，不能由当前自动会话取得；但 bit118 的
          loginctl 状态是 Linger=yes，因此改用等价的用户 systemd 路径完成实机安装，
          不是依赖登录会话的临时后台进程。服务和五分钟 timer 都已 enabled + active。

    4.做了两次真实负向/正向证据：先 rosnode kill /world_model，required 节点让整套
          roslaunch 停止，systemd 的 MainPID 从 1910133 变为 1910669、NRestarts=1，
          随后五节点与 127.0.0.1:9090 全部恢复；再向常驻端口发送一帧真实长度前缀
          heartbeat，/server/car/state 收到 robot_id=car、orientation.w=1.0。
          11311 与 9090 经 ss 核对都只监听回环，没有新增校园网暴露面。

    5.服务器 Python 实际用 /usr/bin/python3，里面已有 pymavlink/pyserial/OpenCV，
          先前 SKIP 是 Anaconda 默认 python3 的 PATH 偏差，不是服务器缺包。显式使用
          系统 Python 后 validate.sh 为 52 通过、0 失败、0 跳过，其中服务器常驻部署
          9/9、systemd 10 个单元全过。暂停常驻服务后完成最终回归：Catkin 82/82，
          Task-02~07 分别 9/7/17/28/11/15 全绿，E2E 33/33，Host 81/81，Phase 1
          冒烟 61/61。之后恢复服务与 timer，最终健康 JSON 为 ok=true，11311/9090
          仍只监听回环。

###### 2026/8/2（确认 BOM 后的全项目同步）

    1.负责人确认了两台 Pi、两套底盘控制模块、共享车载智能载荷和无人机本体/视觉载荷的
          完整 BOM。它推翻了旧计划里 A1、TB6612/BTS7960、F450/S500、KonKer、M8N、
          3DR 经两台 Pi 等假设。最关键的系统事实是：车载 Pi+ICM42688+A2M12+云台/OpenMV
          只有一套，在两底盘间人工换装，不能把软件 SwapChassis 理解成两台完整车同时在线。

    2.固件按真实模块边界调整。每个 MC520P30 独占一片 DRV8871，PWM/IN2 控制替代旧双方向
          H 桥；DRV8871 只有内部 ILIM，没有电流反馈脚，所以遥测电流字段保留但发送 NaN，
          软件过流位暂不可用。差速/麦轮轮半径改为 32.5/40 mm。两板协议升到 v0.2，新增
          0x14 四路超声波快照；135 条 MCU Host 用例和 10 条串口自测通过。

    3.ADR-0013 最终选择“HC-SR04 由各底盘 MCU 定时”，删除 Pi 直接 GPIO 驱动。新增的
          chassis_bridge 使用同一生产帧解析器下发速度、核对 PONG 板型/底盘/最低版本，并在
          收不到首帧超声波时失败关闭。最终 TRIG/ECHO 引脚、电平转换和防串扰间隔仍等电子组，
          所以两份移植层现在明确返回不可用，而不是伪造实机完成。

    4.无人机部署改成 Pi↔Pixhawk TELEM2、915 MHz 空中端↔TELEM1、地面端↔地面站；电台不再
          是两台 Pi 容器的公共必需设备。新增 MAVROS+D435i 真实 launch 和 DRONE_VISION 选择器；
          双 Pi Camera 的具体型号/CSI/libcamera profile 未确认时失败关闭。Pi 5 样机只有
          ttyAMA10，但官方资料表明它是 3 针调试口；40 针 GPIO14/15 要 uart0-pi5 overlay，
          生产预检因此要求 ttyAMA0 并拒绝把 ttyAMA10 当作就绪证据。

    5.ADR-0018 冻结五个模块与资源所有权；ROADMAP、PLATFORM、ICD、任务索引、能力矩阵、
          部署手册和交接文档同步更新。仍不能关闭 Phase 1.5：MC520P30 参数、组装几何、
          两 MCU pinmux、IA6B、USB 身份、双 CSI、供电/电池/推重比、PX4 参数和跨校区隧道
          都必须用良乡实物与中关村链路证据关闭。

###### 2026/8/3（英文硬件答复审查与地面车参数冻结）

    1.电子/机械组英文答复补齐 MC520P30、底盘几何、ICM42688 安装轴、HC-SR04 分压、
          OpenMV/云台、IA6B 和电池。MC520P30 冻结为 30:1、13 PPR、AB 四倍频，即
          1560 counts/输出轴转，12V 空载 360±20 RPM；差速有效半径/轮距为 31/166 mm，
          麦轮有效半径/轴距/轮距为 39.5/124/166 mm。此前 32.5/40 mm 只是 BOM 阶段
          占位，已从仿真、ROS 参数、URDF、固件与任务计划同步替换。

    2.答复不能原样当接线权威：其中把 MSPM0 的 PB4/PB1 写成 TIMA0_C0/C1，并把
          TIMG7 当第二路 QEI；官方数据手册表明这些复用不成立且只有 TIMG8 支持 QEI。
          ADR-0019 记录纠正后的 LaunchPad 候选表。真实 driverlib 剖面继续用编译期
          #error 阻止误烧，直到 SysConfig GPIO 双边沿中断接好；纯 C Gray 码解码器已完成。

    3.STM32 侧实现 PD8..PD11 顺序 Trigger 与 PC6..PC9/TIM8_CH1..4 Echo 捕获，单路
          50 ms 间隔、10 µs 脉冲、30 ms 超时、失败值 0xFFFF。两 MCU 共用 iBUS 解析器、
          CH5 低→高 + 连续 3 s 中位解锁、100 ms 失联、MANUAL/AUTO 与杆量接管状态机；
          特别增加“中间丢帧不能冒充连续 3 s”的负向用例。第二 UART 接入控制环仍待上板。

    4.ICM42688 生产配置改为 0x69，并按实物 Y 前/X 左/Z 下转换为 REP-103 的
          (sensor_y, sensor_x, -sensor_z)。Host 传感器套件 81/81；固件 Host 为
          STM32 80/80 + MSPM0 71/71，共 151/151。Phase 1 冒烟 64/64、部署校验
          55/55，均为零失败零跳过；flake8 7.1.1、yamllint 1.38.0、ARM 目标语法和
          Clang 静态分析无发现。服务器服务/timer 均 active，健康 JSON ok=true，
          11311/9090 仍只监听 127.0.0.1。

    5.仍需良乡实物关闭：MSPM0 SysConfig/中断、两板 IA6B 第二 UART、三套接收机端点与
          failsafe、供电模块满载/纹波/瞬态、云台独立 BEC、编码器方向/漏计数、USB 身份与
          外参。无人机本体未到货，PX4/ESC/GPS/数传和飞行参数继续失败关闭；跨校区隧道
          地址也不能从校园网临时 IP 推断。

###### 2026/8/3（服务器离场收口 · CI 分支触发修复 · Pi 交接）

    1.负责人发现切到 `task-new` 后 GitHub Actions 从未自动运行。不是 GitHub 偶发故障：
          本地 workflow 的 `push.branches` 只允许 main、feat/*、fix/*，而 GitHub 公共 API
          对 task-new 返回的 CI run 数正好是 0；最近的 CI push run 仍停在 feat/task-XX。

          只补一个 task-new 会在下次改名时复发，而且旧白名单本来就漏了规范允许的
          docs/* 和 exp/*。因此改成任意分支 push 都触发，PR 仍只针对 main；冒烟脚本
          新增契约守卫，要求 push 下不得再出现 branches/branches-ignore。用合成的
          `push.branches: [main]` 做负向测试，守卫能正确拦住。

    2.仓库本身也残留一次改名债：origin 仍指向 GitHub 会重定向的旧拼写 URL，README、
          SECURITY、部署手册和边缘 systemd 也有旧链接。origin 已改为当前正式仓库，
          `git ls-remote` 确认 task-new 指向 1317718；live 文档/配置同步新 URL。
          任务书正文中的旧路径按“任务书是历史方案”规则保留，不回写过去。

    3.负责人已把旧 AI_HANDOFF/CLAUDE 移到 obsolete-documentation。本次保留该归档，
          不恢复 live CLAUDE；重新写一份只面向良乡两台 Pi 的 AI_HANDOFF。它按风险排序
          64GB 卡/恢复、I2C/UART、部署不自启、udev 稳定名、车机逐件台架、无人机无桨，
          并用 H1~H13/N1~N2 列出缺失硬件与网络事实。另建日期明确的服务器收口报告，
          避免下一位把“服务器本地健康”误读成“跨校区完成”。

    4.服务器离场审计：service 与 health timer 均 enabled+active，五个项目服务节点齐全，
          健康 JSON ok=true；11311/9090 只监听 127.0.0.1。系统时间同步为 yes，503GiB
          内存中约 480GiB 可用。真正需要带走的运维风险仍是根分区 97%（约剩 80GiB）；
          /data2 还有约 1.1TiB，因此 bag、镜像、模型和日志继续只写 /data2。

    5.最终复测没有把 SKIP 写成绿灯：使用临时隔离依赖让 MAVLink 签名真实执行，Phase 1
          冒烟 65/65、部署 55/55，均零失败零跳过；STM32 80/80、MSPM0 71/71、传感器
          Host 81/81；6 个 Catkin 包构建成功，ROS 82/82。ShellCheck 0.10.0、yamllint
          1.38.0、flake8 7.1.1 零发现，345 个 live Markdown 本地链接零缺失。
          测试后再次检查服务/timer 和回环监听，服务器基线未被构建测试扰动。

    6.全分支触发修复生效后，task-new 的 CI run 30762939491 自动启动；其中 Cppcheck
          2.13.0 把 `TIM8->CNT - start` 两次易失寄存器读取误判成同一表达式相减，导致
          Lint Scripts & Configs 红灯。没有缩窄规则或加入抑制，而是把 1 MHz TIM8 计数器
          读取封装成显式硬件访问函数；10 µs Trigger 脉冲和 16 位回绕语义不变。STM32
          Host 80/80，ARM 目标语法检查和 Clang 静态分析均通过；Ubuntu 24.04 / Cppcheck
          2.13.0 按 CI 原命令扫描 25 个固件 C 文件，零发现。

---

## 历史名称脚注

[^1]: **air_ground_drone_sitl** 是 `air_ground_drone_bringup` 的早期命名。2026-07-25 全局重构中统一改名，旧名称保留在日记中仅供历史追溯。

[^2]: **air_ground_car_sim** 是 `air_ground_car_bringup` 的早期命名。同上，于 2026-07-25 重构中统一为 `air_ground_car_bringup`。
