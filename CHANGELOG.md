# Changelog

本文件记录项目的重要变更。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

> 2026-08-03 之前的逐次提交见 `git log`；按日的决策、失败与教训见 [Research_Diary.md](./Research_Diary.md)。
> 架构决策见 [docs/decisions/](./docs/decisions/)；每条 ADR 只增不改。

## [Unreleased]

### Added

- `docs/AI_OWNERSHIP_HANDOFF.md`：项目级 AI 接手章程（状态分级、安全红线、运行手册、已知坑）。
- `docs/ai-collaboration/PROTOCOL.md` 与 `CLAIMS.md`：DeepSeek × Claude 双 AI 协作协议与事项认领表。
- `AGENTS.md` / `CLAUDE.md`：AI 协作者入口。
- 开源项目常规文件：`CHANGELOG.md`、`CODE_OF_CONDUCT.md`、`CONTRIBUTING.md`、`THIRD_PARTY_NOTICES.md`、PR 与 issue 模板、`CODEOWNERS`。
- `.editorconfig`：统一 UTF-8/LF 与缩进规则；`SECURITY.md` 补充私密漏洞报告流程。

### Changed

- 仓库卫生整理：合并 `docs/ai-takeover-protocol` 到 `dev`，清理已合并的临时分支，`main` 快进到与 `dev` 一致。
- 集成分支事实为 `dev`（`CONVENTIONS.md` 仍写 `main`，待 ADR-0020 固化）。

### Fixed

- `validate.sh` 在 Windows 上的失败用例已登记为 CLAIMS T-007，根因待定位；修复前不以本机结果作为部署校验通过证据。

## [0.1.0] - 2026-07-28

### Added

- Phase 0 仿真框架 9/9：PX4 SITL 无人机、差速/麦轮双底盘、传感器、MAVLink/TCP 通信桥、边缘服务器、集成总装、E2E 验证。
- 6 个 ROS 包：`air_ground_interfaces`、`air_ground_drone_bringup`、`air_ground_car_bringup`、`air_ground_com_bridge`、`air_ground_lab_server`、`air_ground_bringup`。
- E2E 33/33、单元测试 56/56、Quick Smoke 5/5。
- MIT `LICENSE`、`requirements.txt` 依赖冻结、GitHub Actions CI 流水线。
- 目录重命名为 kebab-case，归档 `obsolete-documentation/`。
- tag `v0.1.0` 与 `milestone/sim-framework-done`。

## Phase 1 — 基础设施（2026-07-28 ~ 2026-08-03，未打 tag）

### Added

- STM32F407 麦轮固件：逆运动学、PID、CRC-16 帧协议、编码器、DRV8871、TIM8 四路 HC-SR04。
- MSPM0G3507 差速固件：差速运动学、编码器、iBUS/RC 安全状态机、软件正交解码。
- `src/firmware/common/`：两块板共享的帧层、PID、CRC、故障状态机、移植层接口。
- 树莓派部署：Docker 镜像、systemd、udev、chrony、SSH 加固、健康检查、离线镜像更新。
- CI 扩展至 7 个 job；门禁分层见 ADR-0008 / ADR-0012。
- 传感器驱动骨架 + MAVLink 2 签名工具链；标定工具链与 Phase 1 集成验证。
- ADR-0003 ~ ADR-0013。

### Verified

- ROS 单元测试 82/82、E2E 33/33、固件 Host 151/151、传感器 Host 81/81。
- Phase 1 冒烟 65/65、部署校验 55/55（Linux/CI 环境）。

## Phase 1.5 — 实机接入（进行中）

### Added

- ADR-0014 ~ ADR-0019：ROS 测试阻塞化、边缘模式失败关闭、服务器地址分离、跨校区 TCP 隧道、五模块 BOM、地面车参数冻结。
- 中关村服务器常驻服务与健康 timer；11311/9090 仅监听回环。
- Raspberry Pi 5 宿主预检、部署安装器、稳定设备名规则。
- 2026-08-03 地面车参数冻结：MC520P30 1560 counts/rev、差速/麦轮几何、ICM42688 0x69、HC-SR04 四路。

### Known gaps

- MSPM0 SysConfig 与 GPIO 双边沿中断未接入，`ci-link` 产物不可烧录。
- 两 MCU 第二 UART 未接入控制环，三套 IA6B 未验收。
- 无人机未到货，PX4/ESC/GPS/数传参数与推重比未验证。
- 跨校区受控隧道未获批，三机时钟偏差无实测。
