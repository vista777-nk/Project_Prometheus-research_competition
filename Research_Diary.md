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


---

## 历史名称脚注

[^1]: **air_ground_drone_sitl** 是 `air_ground_drone_bringup` 的早期命名。2026-07-25 全局重构中统一改名，旧名称保留在日记中仅供历史追溯。

[^2]: **air_ground_car_sim** 是 `air_ground_car_bringup` 的早期命名。同上，于 2026-07-25 重构中统一为 `air_ground_car_bringup`。
