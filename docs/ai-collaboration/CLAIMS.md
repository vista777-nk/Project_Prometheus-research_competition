# CLAIMS — 事项认领与基线记录

> 规则见 [PROTOCOL.md](./PROTOCOL.md)。动手前先登记；这里是活文档，状态变了就改。

## 认领表

| ID | 事项 | 认领者 | 分支 | 基线 SHA | 状态 | 备注 |
|----|------|--------|------|----------|------|------|
| T-000 | 接手整理 + 双 AI 协作协议文档 | Claude | `docs/ai-takeover-protocol` | 50751f7 | 进行中 | 新增 AGENTS.md、CLAUDE.md、本目录；尚未提交 |
| T-001 | 分支模型 ADR-0020（dev vs main） | — | — | — | 待负责人 | 需负责人先选方向，见 PROTOCOL §10 #1 |
| T-002 | 在 Ubuntu 服务器恢复可信基线（build / test-all / quick-smoke / test-e2e / smoke / validate） | — | — | — | 阻塞 | 需要能登录中关村服务器的环境；本机只能做 Windows 部分，见下表 |
| T-003 | MSPM0 SysConfig 工程化与 GPIO 双边沿正交解码接入 | — | — | — | 未认领 | 交接书 §6.3-1；默认 ci-link 产物不可烧录是故意的 |
| T-004 | 两 MCU 第二 UART 接入控制环 + 负向用例 | — | — | — | 未认领 | 交接书 §6.3-2 |
| T-005 | OpenMV `/car/openmv/image_raw` 契约 ADR | — | — | — | 阻塞 | 要上机确认是否有真实发布者；没有就不伪造话题 |
| T-006 | Phase 2 离线骨架 | — | — | — | 待负责人 | PROTOCOL §10 #5 |
| T-007 | 修复 Windows 上 `validate.sh` 的 10 个失败用例（test_entrypoint_modes.py 9 个、test_server_deployment.py 1 个） | — | — | 50751f7 | 未认领 | 干净检出 50751f7 同样失败，不是新改动引起。两个测试都是 `subprocess.run(["bash", str(路径)])`，报错里反斜杠被吞、返回码 127，疑为 Windows 路径传给 bash；尚未改代码验证。是否修、怎么修（如 `as_posix()`，或这两组在 Windows 上显式 SKIP）待定。修复前不要把本机 validate 当通过证据 |

## 基线记录

只记实测。引用数字前复跑。

| 日期 | 环境 | commit | 结果 | 没验证 |
|------|------|--------|------|--------|
| 2026-11-06 | Windows 开发机，Git Bash，Python 3.14.7，MinGW gcc | 50751f7 | `make smoke-phase1`：通过 64 / 失败 0 / 跳过 1（缺 pymavlink，MAVLink 2 签名自测未跑）；STM32 Host 80/80；MSPM0 Host 71/71 | `validate.sh`、传感器 Host 81、ROS 82、E2E 33、ARM 交叉编译、shellcheck、docker；GitHub CI 状态（网络受阻）；远端分支实时状态（用的是本地缓存的 origin 引用） |
| 2026-11-06 | 同上，`bash src/deployment/validate.sh`；在带 4 个新文件的工作区和 `git worktree` 干净检出的 50751f7 上各跑一次 | 50751f7 | 两次结果相同：通过 51 / 失败 2 / 跳过 2。失败：entrypoint 模式测试（10 个用例 9 个失败）、服务器常驻部署测试（9 个用例 1 个失败）。跳过：MAVLink 签名自测（缺 pymavlink）、systemd-analyze（非 Linux） | 交接书记录的「部署校验 55/55」在本机未复现；上述失败的根因未经修复验证（见 T-007）；这些用例在本机没有检查到任何东西，部署校验以 Linux / CI 为准 |
