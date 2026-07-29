# src/firmware/ —— 下位机固件

> Layer 1 硬件层。**不是 ROS package，不参与 `catkin build`。**

本目录下是独立的交叉编译工程，各自用 `make` 构建：

| 目录 | 主控 | 底盘 | 任务 | 状态 |
|------|------|------|------|:---:|
| [`common/`](common/) | — | — | task-13 §13.1 权威布局 | ✅ |
| [`stm32_mecanum/`](stm32_mecanum/) | STM32F407VET6 (Cortex-M4F) | 麦轮 | [task-10](../../project-prometheus-tasks/task-10-stm32-mecanum-firmware.md) | ✅ |
| [`mspm0_diff/`](mspm0_diff/) | MSPM0G3507 (Cortex-M0+) | 差速（TI 电赛合规） | [task-11](../../project-prometheus-tasks/task-11-mspm0-diff-firmware.md) | ✅ |

> ⚠ **`mspm0_diff` 的默认构建产出不是可烧录固件。** MSPM0 的移植层默认为空实现，
> 因为 TI MSPM0 SDK 无法在 CI 上免登录安装，而手写 MSPM0 寄存器地址会产出
> "CI 全绿但上板不动"的固件。算法层是完整且测试覆盖的。
> 详见 [`mspm0_diff/README.md` §2](mspm0_diff/README.md) 与 [ADR-0004](../../docs/decisions/ADR-0004.md) §决策-3。

## common/ —— 共享库

两个固件工程共用，**只存一份**。纯 C99，无 HAL 依赖，
在 `arm-none-eabi-gcc`（M4F 硬浮点 / M0+ 软浮点）与 Host gcc 下都能编译。

| 模块 | 内容 |
|------|------|
| `crc16.c/.h` | CRC-16/CCITT-FALSE（[ADR-0003](../../docs/decisions/ADR-0003.md) 统一标准） |
| `protocol_frame.c/.h` | 帧打包 / 拆包状态机（SOF/LEN/CRC/EOF） |
| `pid.c/.h` | 条件积分抗饱和 PID 速度环 |
| `unity.c/.h` | 极简 Unity 风格测试框架（**仅 Host 测试链接，不进固件**） |

各工程通过 `-I../common` 引用头文件、在 Makefile 里直接编译 `../common/*.c`。

> task-11 完整复用了 `common/`，**一行没改** —— task-10 声称的"帧层板无关"
> 至此才算被证明，而不只是被声称。

> 改 `common/` 里的任何东西，都要同时跑两个工程的 `make test`。
> 帧格式与 CRC 还牵连树莓派端的 Python 解析器（task-14/15），见 ADR-0003 的三方一致性要求。

## 两个工程的分层差异

| | `stm32_mecanum` | `mspm0_diff` |
|---|---|---|
| 寄存器访问位置 | 直接写在 `encoder.c` / `motor.c` / `uart.c` | 收拢到 `mspm0_port.h` 的 ~15 个原语 |
| Host 可测范围 | 运动学 + 协议 | 运动学 + 协议 + **编码器测速逻辑** |
| Host 用例数 | 52 | 49 |

移植层分离是 task-11 的主要架构收获：回绕处理、测速窗口、EMA 这些
**真正容易出错的逻辑**因此可以脱离硬件验证。若日后重构 `stm32_mecanum`，
应当照此办理。

## 快速上手

```bash
cd stm32_mecanum      # 或 mspm0_diff
make test    # 只要有 gcc 就能跑，不需要 ARM 工具链
make         # 需要 arm-none-eabi-gcc
```

各工程的引脚定义、协议细节、上板检查清单见其自身的 `README.md`。
