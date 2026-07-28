# src/firmware/ —— 下位机固件

> Layer 1 硬件层。**不是 ROS package，不参与 `catkin build`。**

本目录下是独立的交叉编译工程，各自用 `make` 构建：

| 目录 | 主控 | 底盘 | 任务 | 状态 |
|------|------|------|------|:---:|
| [`common/`](common/) | — | — | task-13 §13.1 权威布局 | ✅ |
| [`stm32_mecanum/`](stm32_mecanum/) | STM32F407VET6 (Cortex-M4F) | 麦轮 | [task-10](../../project-prometheus-tasks/task-10-stm32-mecanum-firmware.md) | ✅ |
| `mspm0_diff/` | MSPM0G3507 (Cortex-M0+) | 差速（TI 电赛合规） | [task-11](../../project-prometheus-tasks/task-11-mspm0-diff-firmware.md) | 🔴 待开始 |

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

> 改 `common/` 里的任何东西，都要同时跑两个工程的 `make test`。
> 帧格式与 CRC 还牵连树莓派端的 Python 解析器（task-14/15），见 ADR-0003 的三方一致性要求。

## 快速上手

```bash
cd stm32_mecanum
make test    # 只要有 gcc 就能跑，不需要 ARM 工具链
make         # 需要 arm-none-eabi-gcc
```

各工程的引脚定义、协议细节、上板检查清单见其自身的 `README.md`。
