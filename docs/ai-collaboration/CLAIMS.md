# CLAIMS — 事项认领与基线记录

> 规则见 [PROTOCOL.md](./PROTOCOL.md)。动手前先登记；这里是活文档，状态变了就改。

## 认领表

| ID | 事项 | 认领者 | 分支 | 基线 SHA | 状态 | 备注 |
|----|------|--------|------|----------|------|------|
| T-000 | 接手整理 + 双 AI 协作协议文档 | Claude | `docs/ai-takeover-protocol` | 50751f7 | 完成 | 已合并到 `dev`（fd51611），分支已删除 |
| T-001 | 分支模型 ADR-0020（dev vs main） | — | — | — | 待负责人 | 需负责人先选方向，见 PROTOCOL §10 #1 |
| T-002 | 在 Ubuntu 服务器恢复可信基线（build / test-all / quick-smoke / test-e2e / smoke / validate） | Claude | `fix/ci-python-pin-and-baseline` | 1240643 | 进行中 | 2026-11-06 已在 Ubuntu 20.04 服务器跑出：catkin 单元 82/82，运行时测试 78/78，smoke 65/0/0，validate 55/0/0，明细见下方基线记录。没完成：test-drone / quick-smoke / test-e2e 因缺 PX4 v1.14 SITL 没运行（T-011），所以 `make test-all` 整体没跑完，不能写绿；GitHub CI 在 `dev` 头上是红的（T-009） |
| T-003 | MSPM0 SysConfig 工程化与 GPIO 双边沿正交解码接入 | — | — | — | 未认领 | 交接书 §6.3-1；默认 ci-link 产物不可烧录是故意的 |
| T-004 | 两 MCU 第二 UART 接入控制环 + 负向用例 | — | — | — | 未认领 | 交接书 §6.3-2 |
| T-005 | OpenMV `/car/openmv/image_raw` 契约 ADR | — | — | — | 阻塞 | 要上机确认是否有真实发布者；没有就不伪造话题 |
| T-006 | Phase 2 离线骨架 | — | — | — | 待负责人 | PROTOCOL §10 #5 |
| T-007 | 修复 Windows 上 `validate.sh` 的 10 个失败用例（test_entrypoint_modes.py 9 个、test_server_deployment.py 1 个） | — | — | 50751f7 | 未认领 | 干净检出 50751f7 同样失败，不是新改动引起。两个测试都是 `subprocess.run(["bash", str(路径)])`，报错里反斜杠被吞、返回码 127，疑为 Windows 路径传给 bash；尚未改代码验证。是否修、怎么修（如 `as_posix()`，或这两组在 Windows 上显式 SKIP）待定。修复前不要把本机 validate 当通过证据 |
| T-008 | 仓库卫生整理：开源常规文件、PR/issue 模板、分支清理 | DeepSeek | `chore/repo-hygiene` | fd51611 | 完成 | 新增 CHANGELOG / CODE_OF_CONDUCT / CONTRIBUTING / THIRD_PARTY_NOTICES、`.github/` 模板、`.editorconfig`，补强 SECURITY 报告流程；已把 `docs/ai-takeover-protocol` 合入 `dev` 并把 `main` 快进到 `dev`；远端推送与远端分支删除因 GitHub 不可达未执行 |
| T-009 | GitHub CI 在 `dev` 头 1240643 上红灯：只有 Phase 1 Smoke job 的 Install Python dependencies 步骤失败，其余 6 个 job 通过（上一个全绿的头是 50751f7） | Claude | `fix/ci-python-pin-and-baseline` | 1240643 | 进行中 | 本分支只把该 job 从 `ubuntu-latest` 固定到 `ubuntu-24.04`，**这是假设，没有 CI 验证**。依据：该步骤用了 135 s（绿的那次 13 s）；`ubuntu-latest` 正向 Ubuntu 26（默认 Python 3.14.4）迁移，`numpy<2` 在 CPython 3.13/3.14 没有 wheel。反例：同样带 `<2` 约束的 validate-deployment 在红的那次也变慢（216 s，之前 15 s）却通过了，也可能只是镜像或网络问题。job 日志要登录才能读，没读到。未推送。另外 5 个 `ubuntu-latest` job 没改，是否一并固定待负责人定 |
| T-010 | 服务器 user service `air-ground-lab-server` 反复崩溃：`server.env` 的 `AIR_GROUND_REPO` 指向已不存在的 `/data2/project/...`（仓库现在在 `/data2/projectDir/...`），11311 与 9090 没有监听 | Claude | —（仓库外的运维修复，无分支） | 1240643 | 进行中 | 2026-11-06 会话内确认后处理：干净重建 `build/ devel/ logs/`（旧的移到 `/tmp/prometheus-takeover/old-catkin-spaces`，重启会丢）；重建 `/data2/air-ground-server/ros-log`；`server.env` 只改 `AIR_GROUND_REPO` 一行，原 `server.env`、unit 与脚本备份在 `~/.config/air-ground/backup-20261106-takeover`。重启后 active，健康检查 OK（5 个服务节点），服务进程的 13 个监听口全在 127.0.0.1，NRestarts=0。**只观察了几分钟，长期稳定没验证。** `install_server.sh` 重复安装不覆盖已有 `server.env`，所以仓库换路径时必须手改；给安装器加检查没做 |
| T-011 | PX4 v1.14 SITL 未安装（`~/PX4-Autopilot` 不存在）：`test-drone`、`quick-smoke`、`test-e2e` 跑不了，8 月的 5/5 与 33/33 无法在本机复现 | — | — | 1240643 | 待负责人 | 需要从 GitHub 克隆并长时间源码编译；默认路径在根分区（97%），要把 `PX4_AUTOPILOT_DIR` 指到 `/data2`（约剩 67 GB）。步骤见 `project-prometheus-tasks/task-02-drone-sitl.md` 与 `setup_all.sh` 第 4 步；日记没记录 8 月是怎么装的。负责人确认前不装 |
| T-012 | 分支记录与实际不符：远端 `git ls-remote` 现在只有 `dev` 与 `main`；本机这份克隆的 `main` 与远端 `main` 都在 a0b7243，落后 `dev` 112 个提交，T-008 与交接书 §2.4 写的「main 已快进到 dev」没有体现在远端；`origin/task-old` 等是过期的本地引用 | — | — | 1240643 | 待负责人 | 本地 `origin/task-old`（b0fb48e）有 5 个、`origin/feat/task-XX` 有 3 个不在 `dev` 的提交，这些引用可能是它们唯一的副本。**不要** `git fetch --prune`，不要删本地 `origin/*` 引用，直到负责人决定。本地 `doc`、`feat/*`、`task-new` 分支没动 |

## 基线记录

只记实测。引用数字前复跑。

| 日期 | 环境 | commit | 结果 | 没验证 |
|------|------|--------|------|--------|
| 2026-11-06 | Windows 开发机，Git Bash，Python 3.14.7，MinGW gcc | 50751f7 | `make smoke-phase1`：通过 64 / 失败 0 / 跳过 1（缺 pymavlink，MAVLink 2 签名自测未跑）；STM32 Host 80/80；MSPM0 Host 71/71 | `validate.sh`、传感器 Host 81、ROS 82、E2E 33、ARM 交叉编译、shellcheck、docker；GitHub CI 状态（网络受阻）；远端分支实时状态（用的是本地缓存的 origin 引用） |
| 2026-11-06 | 同上，`bash src/deployment/validate.sh`；在带 4 个新文件的工作区和 `git worktree` 干净检出的 50751f7 上各跑一次 | 50751f7 | 两次结果相同：通过 51 / 失败 2 / 跳过 2。失败：entrypoint 模式测试（10 个用例 9 个失败）、服务器常驻部署测试（9 个用例 1 个失败）。跳过：MAVLink 签名自测（缺 pymavlink）、systemd-analyze（非 Linux） | 交接书记录的「部署校验 55/55」在本机未复现；上述失败的根因未经修复验证（见 T-007）；这些用例在本机没有检查到任何东西，部署校验以 Linux / CI 为准 |
| 2026-11-06 | Windows 开发机，Git Bash | 43c8933（文档/模板提交） | 纯文档与模板改动；`git diff --check` 通过（无输出） | 未跑 smoke / validate / Host 测试（本次未改代码）；未推送；GitHub CI 未验证 |
| 2026-11-06 | Ubuntu 20.04.6 服务器，ROS Noetic，系统 Python 3.8.10（PATH 里 `/usr/bin` 在前）；干净重建 `build/ devel/`；`dev` 干净检出 | 1240643 | catkin 单元 82/82；运行时测试 test-diff 7/7、test-mecanum 17/17、test-sensors 28/28、test-bridge 11/11、test-server 15/15（各目标单独跑，日志里没有 SKIP / FAIL 行）；`make smoke-phase1` 65/0/0；`validate.sh` 55/0/0；`git diff --check` 通过。T-007 的 10 个失败因此只出现在 Windows，但路径假设本身没复现、没验证 | `test-drone`、`quick-smoke`、`test-e2e`：缺 PX4 v1.14 SITL，没运行（不是 SKIP，也不是通过），所以 `make test-all` 整体没跑完，不能称绿；shellcheck、cppcheck、ARM 交叉编译没跑（本机没装）；Anaconda 的 Python 3.11 排在前面时没有 pymavlink，签名自测会 SKIP；GitHub CI 在这个提交上是红的（T-009），根因未确认；catkin 构建有 gazebo_msgs / gazebo_ros / gazebo_ros_control 的弃用警告（Gazebo Classic 已停止维护），不影响结果 |
