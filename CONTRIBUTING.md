# 贡献指南（Contributing）

> 本仓库由人类负责人（[@vista777-nk](https://github.com/vista777-nk)）与两个 AI 协作者（DeepSeek、Claude）共同维护。
> 人类贡献者与 AI 贡献者适用同一套规则。

## 1. 开始之前

按顺序阅读：

1. [`README.md`](./README.md) — 项目定位与导航
2. [`CONVENTIONS.md`](./CONVENTIONS.md) — 分支、提交、命名、编码规范
3. [`AGENTS.md`](./AGENTS.md) — AI 协作者入口与硬规则
4. [`docs/AI_OWNERSHIP_HANDOFF.md`](./docs/AI_OWNERSHIP_HANDOFF.md) — 当前状态、证据分级、安全红线
5. [`docs/ai-collaboration/PROTOCOL.md`](./docs/ai-collaboration/PROTOCOL.md) — 认领、分支锁、审查与完成定义

涉及硬件时，再读 [`docs/experiments/AI_HANDOFF.md`](./docs/experiments/AI_HANDOFF.md)。

## 2. 开发环境

| 任务 | 环境 |
|------|------|
| ROS / Gazebo / E2E | Ubuntu 20.04 + ROS Noetic |
| 固件 Host 测试 | 任意系统 + gcc |
| 固件交叉编译 | `arm-none-eabi-gcc` |
| 部署与静态校验 | 任意系统；Linux 结果才可作为证据 |
| Pi / 服务器验收 | Raspberry Pi 5（Debian 13）/ Ubuntu 20.04 |

完整命令见 README「快速开始」与交接书 §5。

## 3. 分支与提交

- 集成分支：`dev`（当前事实；`main` 为稳定线，待 ADR-0020 固化分支模型）。
- 工作分支：`feat/<desc>`、`fix/<desc>`、`docs/<desc>`、`exp/<desc>`，名称用英文。
- 不直接在 `dev` 上提交，不 force-push 共享分支。
- 提交信息使用 Conventional Commits + 简体中文，正文写：
  - 改了什么；
  - 实测数字与运行环境；
  - **仍未验证什么**。

示例：

```
fix(firmware): 修复 TIM8 计数器读取的静态分析误判

- 将两次易失寄存器读取封装为显式硬件访问函数
- STM32 Host 80/80，ARM 语法检查通过
- 未上板验证；TIM8 回绕语义不变

Ref: CLAIMS T-004
```

## 4. 合并前的门禁

声称"完成"或请求合并前，必须在本次改动上实际运行并记录：

1. `git diff --check`
2. `make smoke-phase1`
3. `bash src/deployment/validate.sh`（依赖缺失时会 SKIP，必须如实写出）
4. 受影响模块的 Host 测试（STM32 / MSPM0 / 传感器）
5. 涉及 ROS 的改动：Ubuntu 服务器上的 `make test-all`
6. 推送后确认 GitHub CI 实际出现且全绿；无法确认时写"CI 状态未验证"

新增任何校验脚本时，必须做一次**负向测试**：故意注入错误，确认它会变红。

## 5. 架构与文档

- 分层铁律：Layer 4 不得 import `mavros`、`gazebo_msgs`、`sensor_msgs/LaserScan` 等硬件包。
- 架构决策写 ADR（`docs/decisions/`），编号递增，**不修改已有 ADR**。
- 接口变更先改 [`ICD.md`](./project-prometheus-tasks/ICD.md)，再改实现。
- 与任务文档的偏差，记在对应任务文档的"与原方案的偏差"一节。
- 失败记录不得删除。

## 6. 报告问题

使用 `.github/ISSUE_TEMPLATE/` 中的模板，至少提供：

- 环境（系统、ROS/Python 版本、分支与 commit SHA）；
- 完整命令与原始输出/退出码；
- PASS / FAIL / SKIP 数；
- 期望行为与实际行为。

安全漏洞不要开公开 issue，见 [`SECURITY.md`](./SECURITY.md)。

## 7. 提交 Pull Request

使用 `.github/PULL_REQUEST_TEMPLATE.md`，写清"验证了什么"与"没有验证什么"。
PR 由另一个 AI 或负责人审查；合并按钮由负责人或授权者执行。
