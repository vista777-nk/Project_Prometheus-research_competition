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

| 模块 | 内容 | 单元测试位置 |
|------|------|------|
| `crc16.c/.h` | CRC-16/CCITT-FALSE（[ADR-0003](../../docs/decisions/ADR-0003.md) 统一标准） | `stm32_mecanum/test/` |
| `protocol_frame.c/.h` | 帧打包 / 拆包状态机（SOF/LEN/CRC/EOF） | `stm32_mecanum/test/` |
| `pid.c/.h` | 条件积分抗饱和 PID 速度环 | `stm32_mecanum/test/` |
| `faults.c/.h` | 故障位图（线上契约，两板逐位一致）+ 故障状态机 | `mspm0_diff/test/` |
| `mcu_port.h` | **移植层接口** —— 固件里唯一碰寄存器的地方，每块板一份实现 | 各板用假外设替身 |
| `unity.c/.h` | 极简 Unity 风格测试框架（**仅 Host 测试链接，不进固件**） | — |

> 共享模块的单元测试**只写一遍**，落在哪个工程见上表。同一份代码测两遍不增加信息，
> 只增加两处要同步维护的用例。

各工程通过 `-I../common` 引用头文件、在 Makefile 里直接编译 `../common/*.c`。

> task-11 完整复用了 `common/` 的原有四个模块，**一行没改** —— task-10 声称的
> "帧层板无关"至此才算被证明，而不只是被声称。
> `faults` 是 task-11 新增的第五个模块，理由见下。

> 改 `common/` 里的任何东西，都要同时跑两个工程的 `make test`。
> 帧格式与 CRC 还牵连树莓派端的 Python 解析器（task-14/15），见 ADR-0003 的三方一致性要求。

### 为什么故障处理属于 `common/`

故障位图是**线上契约**：树莓派端按这些位解读下位机状态，两块板逐位一致。
位定义分别写在两个 `protocol.h` 里迟早会漂移，所以只存一份。

判定逻辑一并抽出来的理由更实际：它原先写在各板 `main.c` 的 `static` 函数里，
**一个单元测试都覆盖不到**（依赖 1kHz 中断、真实编码器、ADC）。
而 task-10 评审阶段用真实缺陷换来的三条契约恰恰都在这里 ——
`STALL` 必须跟随实际状态、`UART_ERROR` 必须按增量判定、
停机期间 `STALL` 必须保持旧值。抽成纯函数后由 `test_faults.c` 逐条钉死。

**迁移状态**：两块板均已改用 `faults_evaluate_*()`，`main.c` 只负责采集输入与执行处置。

## 分层结构（两个工程已对齐）

```
kinematics / protocol / encoder / motor / uart / main   ← 板级逻辑，纯 C，可 Host 测试
                    ↓ 只调用十几个原语
            common/mcu_port.h                            ← 移植层接口，两板共用
                    ↓ 每块板一份实现
port_stm32f407.c            port_driverlib.c · port_stub.c
```

| | `stm32_mecanum` | `mspm0_diff` |
|---|---|---|
| 移植层实现 | `port_stm32f407.c`（裸机寄存器） | `port_driverlib.c`（TI SDK）· `port_stub.c`（CI） |
| 发送策略 | TXE 中断驱动 | 就地忙等 FIFO |
| 故障判定 | `common/faults.c` | `common/faults.c` |
| Host 用例数 | 64 | 71 |

发送策略的差异被完全挡在移植层里：两块板共用同一份 `uart.c` 环形缓冲逻辑，
差异只体现在各自的 `port_uart_tx_start()` 实现中。

**移植层分离与故障状态机外提最初是 task-11 的架构收获，随后回溯应用到了 task-10。**
回绕处理、测速窗口、EMA、故障位生命周期这些真正容易出错的逻辑，
因此在两块板上都能脱离硬件验证。

## 快速上手

```bash
cd stm32_mecanum      # 或 mspm0_diff
make test    # 只要有 gcc 就能跑，不需要 ARM 工具链
make         # 需要 arm-none-eabi-gcc
```

各工程的引脚定义、协议细节、上板检查清单见其自身的 `README.md`。
