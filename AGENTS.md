# AGENTS.md — AI 协作者入口（DeepSeek / Claude 通用）

> 本文件会被自动读入上下文，刻意保持简短；细则在链接里，不要往这里堆内容。

## 项目

空地联合具身智能研究平台：PX4 无人机 + 差速/麦轮双底盘地面车 + 实验室服务器，ROS Noetic。Phase 0、Phase 1 已完成，Phase 1.5（实机接入）进行中，硬件侧未闭环。

由 DeepSeek 与 Claude 共同维护。人类负责人 @vista777-nk 保留硬件上电、飞行、经费、发布、对外投稿，以及修改治理规则（分支模型、门禁强度、既有 ADR 结论）的最终决定权。

## 每次会话开始

1. `git status --short --branch` 与 `git log --oneline -5`
2. 读 [docs/AI_OWNERSHIP_HANDOFF.md](docs/AI_OWNERSHIP_HANDOFF.md) 的 §2 状态、§4 规则、§7 下一步
3. 读 [docs/ai-collaboration/CLAIMS.md](docs/ai-collaboration/CLAIMS.md)，确认要做的事没有被对方认领；要做就先登记
4. 涉及硬件时，再读 [docs/experiments/AI_HANDOFF.md](docs/experiments/AI_HANDOFF.md) 对应章节

协作细则（认领、分支锁、交接、审查、完成定义）见 [docs/ai-collaboration/PROTOCOL.md](docs/ai-collaboration/PROTOCOL.md)。

## 硬规则（摘要，全文见交接书 §4）

1. 没有实测就不写「完成」；写「已实现 / 未上机验证」。证据分级 A~D 沿用交接书 §2.2。
2. SKIP 不是 PASS。查不了的项必须显式报 SKIP 并说明原因。
3. 新增校验必须做一次负向测试，确认它会变红。
4. 失败记录（CI 红、硬件异常、失败尝试）不得删除。
5. 分层铁律：Layer 4 不得 import mavros、gazebo_msgs、sensor_msgs/LaserScan 等硬件相关包。
6. ADR 只增不改；旧 ADR 只能被新 ADR 取代或补充。当前最大编号 ADR-0020（提议中，待负责人采纳），下一个是 ADR-0021。
7. 安全红线：不替负责人决定上电、拆装桨、飞行；EDGE_MODE=real 缺设备时失败关闭，不回退 mock；ROS Master 11311 与 TCP 9090 只监听 127.0.0.1；不提交密钥、.env、序列号、MAC、校园网 IP。
8. 提交信息用 Conventional Commits + 简体中文，正文写实测数字和仍未验证项。标识符与分支名用英文。
9. obsolete-documentation/ 与六个厂商硬件资料目录只读。

## 当前开发机环境（Windows，Git Bash）

- 用 `python`，不是 `python3`；输出中文前 `export PYTHONIOENCODING=utf-8:replace`。
- 跑 lint / 冒烟类门禁前先 `git add -A`，否则 `git ls-files` 看不到新文件，会出现「没被检查却显示 0 条」。
- 本机没有 shellcheck、docker、arm-none-eabi-gcc、gh；ROS 测试、E2E、Gazebo 必须在 Ubuntu 20.04 服务器上跑。
- 2026-11-06 实测基线（commit 50751f7）：`make smoke-phase1` 通过 64 / 失败 0 / 跳过 1（缺 pymavlink）；STM32 Host 80/80；MSPM0 Host 71/71。跳过项没有被检查，完整校验以 CI 为准。
- `bash src/deployment/validate.sh` 在本机不能当通过证据：干净检出 50751f7 也是通过 51 / 失败 2 / 跳过 2（entrypoint 模式测试、服务器常驻部署测试），疑为测试把 Windows 反斜杠路径传给 bash，尚未修复（见 CLAIMS.md T-007）。部署校验以 Linux 或 CI 为准。
- 本机访问 GitHub 可能被拦截（git ls-remote 与 api.github.com 曾失败）。无法确认 CI 时写「CI 状态未验证」，不要写成绿。
