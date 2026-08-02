# Task-10: STM32F407 麦轮固件

> **状态：✅ 已完成（2026-07-28）** | **优先级：🥇 最高** | **实际耗时：约 5h**
>
> **适用环境**：任意 OS（Windows / macOS / Linux） + ARM GCC Toolchain
> **硬件依赖**：无（CI 编译 + 单元测试 Mock 电机反馈）
> **ROS 依赖**：无（独立固件项目，非 ROS Package）
>
> **产出**：`src/firmware/common/`（共享库）· `src/firmware/stm32_mecanum/`（固件）
> · [ADR-0003](../docs/decisions/ADR-0003.md)（协议规范）· CI job `build-stm32-firmware`
> **验收结果**：Host 单元测试 **63/63** 通过（初版 52，2026-07-29 移植层重构后 +11）。
> 落地说明与设计偏差见文末 [§实施记录](#实施记录2026-07-28)，结构调整见 [§重构记录](#重构记录2026-07-29)。
>
> **2026-08-02 实机 BOM 覆盖**：麦轮模块现为 80 mm 轮、MC520P30 ×4、
> DRV8871 ×4、HC-SR04 ×4。固件 v0.2 已切换 DRV8871 IN1/IN2，增加超声波
> `0x14`，电流字段为 NaN；64 个 Host 用例通过。正文中的 TB6612、ADC 电流采样、
> 33 mm 轮与 v0.1 黄金帧是历史方案，以 ADR-0013/0018 和固件 README 为准。

---

## 前置条件

- 已安装 `arm-none-eabi-gcc` (ARM GCC Toolchain for Cortex-M4)
- 了解 STM32F407VET6 基本参数：168MHz、512KB Flash、192KB SRAM、FPU
- 熟悉本项目 ICD.md §二～§三 的抽象接口边界
- **不需要**：真实 STM32 硬件、Ubuntu 20.04、ROS 环境

---

## 目标

为麦轮底盘构建一套 **可编译、可测试、可 CI 验证** 的 STM32F407 下位机固件，实现：

1. **逆运动学解算**：`vx, vy, ω → 四轮转速 (RPM)`（与仿真 `mecanum_controller.py` 算法一致）
2. **四轮独立 PID 速度环**：每轮 520 编码器电机闭环控制
3. **串口通信协议**：与树莓派5 的 UART 通信（二进制帧协议，CRC 校验）
4. **CI 交叉编译**：GitHub Actions 自动编译 + 固件 artifact 产出
5. **单元测试**：Mock 编码器反馈，验证运动学解算和 PID 逻辑

---

## 架构定位

```
树莓派5 (Layer 2 Edge Node)
  │  rosnode: car_preprocessor.py
  │  发布 /car/observation, /car/state
  │
  │  UART (/dev/ttyAMA0, 115200 8N1)
  │  协议: 二进制帧 + CRC16
  ▼
STM32F407VET6 (Layer 1 Hardware)  ← 本任务
  │  接收: vx, vy, ω (float, m/s, rad/s)
  │  逆运动学 → 四轮目标 RPM
  │  四轮独立 PID 速度环 (1kHz)
  │  编码器读取 (AB 相, 4× 倍频)
  │  上报: 实际四轮 RPM + 电机电流 + 故障码
  ▼
4× 520 编码器电机 (麦轮底盘)
```

**铁律**：STM32 固件只做运动控制，不做任何感知/决策/通信路由。感知数据由树莓派直接从传感器读取。

---

## 架构影响

| 维度 | 内容 |
|------|------|
| **Affected Capability** | Locomotion: 麦轮逆运动学 + 四轮独立速度PID |
| **Modified Interface** | 新增 Layer 1 内部接口: UART 二进制帧协议 (CMD_SET_VELOCITY / TELEMETRY) |
| **New Dependency** | `arm-none-eabi-gcc` (交叉编译工具链) · Unity Test (单元测试框架) |
| **ADR Required** | ADR-0003: 串口二进制帧协议统一设计 (与 MSPM0 共享帧结构) |
| **Risk Level** | 🟢 Low — 纯固件，不影响已有 ROS 系统 |

> **铁律回顾 (RESEARCH_PHILOSOPHY.md §五)**：  
> 真正稳定的是 Capability (麦轮运动控制)，不是 Algorithm (STM32 或未来的其他 MCU)。  
> 换 MCU 时，`kinematics.c` 的数学逻辑不变，`protocol.c` 的帧结构不变。

## 未来演进

| 维度 | 今天 (Phase 1) | 明天 (Phase 2+) |
|------|---------------|-----------------|
| **Replaceable Component** | STM32F407VET6 | ESP32-S3 / Teensy 4.1 / 其他 Cortex-M4/M7 MCU |
| **Permanent Interface** | 串口二进制帧协议 (SOF/CMD/CRC/EOF) · 运动学输入 `{vx, vy, ω}` | 保持不变 — 换 MCU 只需重新实现 HAL 层 (`encoder.c`, `motor.c`, `uart.c`) |
| **Temporary Implementation** | STM32 HAL 库 (`stm32f4xx_hal`) · 手动 PID 整定 | v2: FreeRTOS 任务调度 · 自动 PID 整定 (Ziegler-Nichols) · CAN 总线替代 UART |

---

## 可执行步骤

### 10.1 创建固件项目骨架

在仓库中创建独立固件目录（不是 ROS Package）：

```bash
cd /path/to/research_compitition
mkdir -p src/firmware/stm32_mecanum
cd src/firmware/stm32_mecanum
```

目录结构：

```
src/firmware/
├── common/                          ← 共享库 (task-13 §13.1 权威布局, task-10/11 共用)
│   ├── pid.h / pid.c                # 通用 PID 控制器
│   ├── crc16.h / crc16.c            # CRC-16/CCITT-FALSE (ADR-0003 标准)
│   ├── protocol_frame.h / .c        # 帧打包/解包 (SOF/EOF/转义)
│   └── unity.h / unity.c            # Unity Test 框架
│
├── stm32_mecanum/                   ← 本任务目录
│   ├── README.md                    # 引脚定义表 + 协议帧格式 + 编译命令
│   ├── Makefile                     # 顶层构建 (arm-gcc, 引用 ../common/)
│   ├── linker/
│   │   └── STM32F407VETx_FLASH.ld   # 链接脚本
│   ├── src/
│   │   ├── main.c                   # 入口：初始化 + 主循环 (1kHz)
│   │   ├── kinematics.c/.h          # 麦轮逆运动学解算 (项目特有)
│   │   ├── encoder.c/.h             # AB 相编码器读取 (TIM 编码器模式, HAL 相关)
│   │   ├── motor.c/.h               # PWM 输出 + 电机控制 (HAL 相关)
│   │   ├── protocol.c/.h            # 协议命令分发 + 遥测组装 (引用 common/protocol_frame)
│   │   ├── uart.c/.h                # UART DMA 收发 (HAL 相关)
│   │   └── stm32f4xx_conf.h        # HAL 库配置
│   └── test/
│       ├── test_kinematics.c        # 运动学解算单元测试 (纯 C, 无 HAL 依赖)
│       ├── test_pid.c               # PID 单元测试 (引用 common/pid.c + common/unity.c)
│       ├── test_protocol.c          # 协议帧打包/解包测试 (引用 common/protocol_frame.c)
│       └── test_crc16.c             # CRC 测试向量验证 (引用 common/crc16.c + 测试向量 b"123456789"→0x29B1)
└── mspm0_diff/                      ← task-11 项目, 同样引用 ../common/
    └── ...
```

> **注意**：
> - `src/firmware/` 不是 ROS Package，不参与 `catkin build`。它是独立交叉编译项目。
> - `pid.c`、`crc16.c`、`protocol_frame.c`、`unity.c` 在 `common/` 中**只存一份**。task-10 和 task-11 通过 Makefile `-I ../common` 和链接 `../common/*.c` 引用。
> - 项目特有的 `protocol.c` 只做命令分发（switch-case），底层帧打包/解包调用 `common/protocol_frame.c`。

### 10.2 逆运动学解算 (`kinematics.c`)

**输入**：`vx` (m/s), `vy` (m/s), `ω` (rad/s)  
**输出**：`wheel_rpm[4]` — 四轮目标转速 (RPM)

麦轮逆运动学公式（与仿真 `mecanum_controller.py` 保持一致）：

```
// 轮序号约定 (俯视图):
//   前方
//  [0]  [1]    ← 前排
//  [2]  [3]    ← 后排
//  (0=左前, 1=右前, 2=左后, 3=右后)
//
// Lx = 轮距半长 (前后方向, m)
// Ly = 轮距半宽 (左右方向, m)
// R  = 轮半径 (m)
//
// 逆运动学矩阵:
// wheel_omega[0] = (vx - vy - ω*(Lx+Ly)) / R
// wheel_omega[1] = (vx + vy + ω*(Lx+Ly)) / R
// wheel_omega[2] = (vx + vy - ω*(Lx+Ly)) / R
// wheel_omega[3] = (vx - vy + ω*(Lx+Ly)) / R
```

**API 设计**：

```c
// kinematics.h
#ifndef KINEMATICS_H
#define KINEMATICS_H

#include <stdint.h>

#define NUM_WHEELS 4

/** 麦轮底盘几何参数 */
typedef struct {
    float wheel_radius;    // 轮半径 (m)
    float lx;              // 轮距半长, 前后方向 (m)
    float ly;              // 轮距半宽, 左右方向 (m)
    float max_rpm;         // 电机最大转速 (RPM)
} MecanumGeometry;

/** 机器人速度指令 (右手坐标系, x=前, y=左, ω=逆时针正) */
typedef struct {
    float vx;   // m/s
    float vy;   // m/s
    float omega; // rad/s
} RobotVelocity;

/** 初始化运动学参数 (参数从 chassis_params.yaml 对应值) */
void kinematics_init(const MecanumGeometry *geo);

/** 逆运动学解算: 机器人速度 → 四轮目标 RPM
 * @param cmd  输入: 期望机器人速度
 * @param rpm  输出: 四轮目标转速 (RPM), 正=前进方向
 * @return 0=成功, -1=超限 (某轮超过 max_rpm 并已钳位)
 */
int inverse_kinematics(const RobotVelocity *cmd, float rpm[4]);

#endif
```

### 10.3 PID 控制器 (`pid.c`)

每个轮子一个独立 PID 速度环，1kHz 控制频率。

```c
// pid.h
#ifndef PID_H
#define PID_H

typedef struct {
    float kp, ki, kd;
    float integral_limit;    // 积分限幅
    float output_limit;      // 输出限幅 (0~1, 占空比)
    // 内部状态 (调用方不直接访问)
    float integral;
    float prev_error;
} PIDController;

void pid_init(PIDController *pid, float kp, float ki, float kd,
              float integral_limit, float output_limit);

/** 计算 PID 输出
 * @param setpoint  目标值 (RPM)
 * @param measured  实测值 (RPM)
 * @param dt        控制周期 (秒), 典型 0.001 (1kHz)
 * @return PWM 占空比 [-1.0, 1.0], 正=前进, 负=后退
 */
float pid_update(PIDController *pid, float setpoint, float measured, float dt);

void pid_reset(PIDController *pid);

#endif
```

### 10.4 串口通信协议 (`protocol.c`)

**物理层**：UART, 115200 8N1  
**数据链路层**：二进制帧，CRC16-CCITT 校验  
**应用层**：命令-响应模式

#### 帧格式

```
┌────────┬────────┬──────────┬──────────────────┬──────────┬────────┐
│  SOF   │  LEN   │   CMD    │      DATA        │  CRC16   │  EOF   │
│ 1 byte │ 1 byte │  1 byte  │   0~251 bytes    │ 2 bytes  │ 1 byte │
│  0xA5  │ n+4    │          │                  │ (LSB→MSB)│  0x5A  │
└────────┴────────┴──────────┴──────────────────┴──────────┴────────┘
SOF = 0xA5, EOF = 0x5A
LEN  = CMD(1) + DATA(n) + CRC(2) + EOF(1) = n + 4, n ∈ [0, 251], LEN ∈ [4, 255]

CRC 覆盖范围: CMD + DATA (即紧接 LEN 之后、CRC 之前的 payload 字节)
SOF / LEN / CRC / EOF 本身不参与 CRC 计算
```

#### 命令定义

| CMD | 方向 | 名称 | DATA | 响应 |
|-----|------|------|------|------|
| `0x01` | Pi→STM32 | `SET_VELOCITY` | `vx(f32) vy(f32) ω(f32)` = 12 bytes | `ACK` |
| `0x02` | Pi→STM32 | `EMERGENCY_STOP` | 无 (0 bytes) | `ACK` |
| `0x03` | Pi→STM32 | `PING` | 无 | `PONG` (含固件版本+板卡类型) |
| `0x11` | STM32→Pi | `TELEMETRY` | 四轮 RPM(f32×4) + 电流(f32×4) + 故障码(u16) = 34 bytes | — |
| `0x12` | STM32→Pi | `ACK` | 被确认的 CMD(u8) = 1 byte | — |
| `0x13` | STM32→Pi | `PONG` | 固件版本 major(u8).minor(u8).patch(u8) + board_type(u8=0x01) + chassis_type(u8=0x01) = 5 bytes | — |
| `0xFF` | STM32→Pi | `ERROR` | 错误码(u8) + 错误详情(变长) | — |

> **TELEMETRY 上报频率**：STM32 每 50ms (20Hz) 主动上报一次。Pi 不需轮询。
>
> **board_type 编码**: `0x01`=STM32F407 · `0x02`=MSPM0G3507  
> **chassis_type 编码**: `0x01`=麦轮(mecanum) · `0x02`=差速(differential)  
> Pi 端启动时发送 PING，从 PONG 中校验 board_type + chassis_type 是否与 `CHASSIS` 环境变量一致，不匹配则拒绝启动。

#### CRC 实现

```c
// CRC-16/CCITT-FALSE (非反射)
// 多项式: x^16 + x^12 + x^5 + 1 = 0x1021
// 初始值: 0xFFFF
// 不反射输入/输出, 无输出异或
//
// 标准测试向量: crc16_ccitt(b"123456789", 9) == 0x29B1
// 在线验证: https://crccalc.com/?crc=123456789&method=CRC-16/CCITT-FALSE

#include <stdint.h>

uint16_t crc16_ccitt(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}
```

> **铁律 (ADR-0003)**：本协议的所有实现——STM32 固件、MSPM0 固件、Pi 端 Python 测试脚本——必须使用完全相同的 CRC 算法和测试向量。task-10/11/15 三方以此为准。

### 10.5 主循环逻辑 (`main.c`)

```c
// 伪代码 (1kHz SysTick 驱动)
void main(void) {
    HAL_Init();
    SystemClock_Config();  // 168MHz, APB1=42MHz, APB2=84MHz
    kinematics_init(&geo);
    pid_init(&pid[0..3], ...);
    encoder_init();   // TIM2/3/4/5 编码器模式
    motor_init();     // TIM1/8 PWM 输出
    uart_init();      // USART1, 115200, DMA

    uint32_t last_telemetry = 0;
    RobotVelocity cmd = {0};

    while (1) {
        // 1. 处理串口接收 (DMA 环形缓冲, 帧解析)
        Frame frame;
        if (protocol_parse(&frame)) {
            switch (frame.cmd) {
                case CMD_SET_VELOCITY:
                    memcpy(&cmd, frame.data, 12);
                    protocol_send_ack(CMD_SET_VELOCITY);
                    break;
                case CMD_EMERGENCY_STOP:
                    cmd = (RobotVelocity){0};
                    motor_all_stop();
                    protocol_send_ack(CMD_EMERGENCY_STOP);
                    break;
                case CMD_PING:
                    protocol_send_pong(FW_MAJOR, FW_MINOR, FW_PATCH);
                    break;
            }
        }

        // 2. 读取各轮编码器 (当前 RPM)
        float actual_rpm[4];
        for (int i = 0; i < 4; i++)
            actual_rpm[i] = encoder_get_rpm(i);

        // 3. 逆运动学 → 目标 RPM
        float target_rpm[4];
        inverse_kinematics(&cmd, target_rpm);

        // 4. PID 控制 → PWM 占空比
        for (int i = 0; i < 4; i++) {
            float duty = pid_update(&pid[i], target_rpm[i], actual_rpm[i], 0.001f);
            motor_set_duty(i, duty);
        }

        // 5. 遥测上报 (20Hz)
        if (HAL_GetTick() - last_telemetry >= 50) {
            protocol_send_telemetry(actual_rpm, motor_get_currents(), fault_code);
            last_telemetry = HAL_GetTick();
        }

        // 6. 故障检测
        fault_code = check_faults(actual_rpm, target_rpm);
        if (fault_code & FAULT_OVERCURRENT) motor_all_stop();

        HAL_Delay(1);  // ~1kHz
    }
}
```

> **ⓘ 实现注意事项（混元3 评审建议）**：
>
> **① PID 控制周期应由定时器中断驱动**：上例 `HAL_Delay(1)` 仅用于示意。实机代码中 PID 闭环必须在定时器中断服务例程 (ISR) 中运行（如 TIM6 1kHz），主循环仅负责串口解析与遥测上报。避免串口 DMA 阻塞导致 PID 周期抖动。
>
> **② 字节序约定：统一小端 (Little-Endian)**：帧内所有多字节数据（float32、uint16）均采用小端字节序，与 STM32 Cortex-M4 和树莓派 ARM64 原生一致。`struct.pack('<H', crc)` 中的 `<` 即小端标记。
>
> **③ Flash 参数持久化 (Phase 2 规划)**：PID 参数、底盘几何参数建议存入 STM32 内部 Flash 的末页，支持运行时通过串口指令调整后写入 Flash，掉电不丢失。当前 Phase 1 参数硬编码在固件中即可。
>
> **④ 硬件急停冗余**：预留一个 GPIO 引脚（如 PB0）作为硬件急停输入，低电平有效，接入物理急停开关。在定时器 ISR 中检测该引脚，一旦触发立即 `motor_all_stop()` 并进入不可恢复的 STOP 状态，不依赖串口通信。这是安全的最后一道防线。

### 10.6 单元测试 (`test/`)

使用 Unity Test 框架（单头文件，零依赖），在 Host 机器上编译运行：

```bash
# 在任意 OS 上（Windows/macOS/Linux），用 GCC/Clang 编译测试
cd src/firmware/stm32_mecanum
gcc -o test_runner test/test_kinematics.c src/kinematics.c -I src -I test
./test_runner
```

**最低测试覆盖**：

| 测试用例 | 输入 | 期望输出 |
|----------|------|----------|
| `test_forward` | vx=0.5, vy=0, ω=0 | 四轮均正转，RPM 相等 |
| `test_strafe_left` | vx=0, vy=0.5, ω=0 | 左前+右后→反转，右前+左后→正转 |
| `test_rotate_cw` | vx=0, vy=0, ω=1.0 | 四轮同向（左侧正转，右侧反转） |
| `test_zero_input` | (0,0,0) | 四轮 RPM=0 |
| `test_saturation` | vx=max_speed | 所有 RPM 被钳位到 max_rpm |

### 10.7 CI 编译配置

在 `.github/workflows/ci.yml` 中新增 job（详见 Task-13）：

```yaml
  build-stm32-firmware:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ARM GCC
        run: |
          sudo apt update
          sudo apt install -y gcc-arm-none-eabi
      - name: Build STM32 Mecanum Firmware
        run: |
          cd src/firmware/stm32_mecanum
          make -j$(nproc)
      - name: Run Unit Tests (Host)
        run: |
          cd src/firmware/stm32_mecanum
          make test
      - name: Upload Firmware Artifact
        uses: actions/upload-artifact@v4
        with:
          name: stm32-mecanum-firmware
          path: src/firmware/stm32_mecanum/build/*.bin
```

---

## 验收标准

- [x] `kinematics.c` 可通过逆运动学单元测试（5 个用例全部 PASS，实际做了 12 个）
- [x] `pid.c` 可通过阶跃响应测试（超调 < 20%, 稳态误差 < 5%）
- [x] `protocol.c` 可通过帧打包/解包往返测试 + CRC 错误注入测试
- [x] `make` 或 `cmake --build` 成功生成 `stm32_mecanum.bin`（Makefile 就绪，交叉编译由 CI 执行）
- [x] CI 交叉编译 job 绿灯（`build-stm32-firmware` 已加入 `.github/workflows/ci.yml`）
- [x] `README.md` 包含：引脚定义表（4 路 PWM + 8 路编码器 + UART）+ 协议帧格式 + `make` 编译命令

---

## 实施记录（2026-07-28）

### 交付清单

| 路径 | 内容 |
|------|------|
| `src/firmware/README.md` | 固件目录总览 |
| `src/firmware/common/` | `crc16` · `protocol_frame` · `pid` · `unity`（与 task-11 共享，只存一份） |
| `src/firmware/stm32_mecanum/README.md` | 引脚表 · 协议 · 整定说明 · **上板检查清单** |
| `src/firmware/stm32_mecanum/Makefile` | `make` / `make test` / `make size` / `make flash` / `make clean` |
| `src/firmware/stm32_mecanum/linker/` | STM32F407VETx 链接脚本 |
| `src/firmware/stm32_mecanum/src/` | 15 个文件：算法层 + 裸机 HAL 层 |
| `src/firmware/stm32_mecanum/test/` | 5 个文件，52 个用例 |
| `docs/decisions/ADR-0003.md` | 串口二进制帧协议统一设计 |
| `.github/workflows/ci.yml` | 新增 `build-stm32-firmware` job（含版本注入） |
| `.gitignore` | 修复：原规则会把固件 `Makefile` 一并忽略 |

### 与任务文档的偏差（均为有意为之）

| # | 文档原文 | 实际实现 | 理由 |
|:---:|----------|----------|------|
| 1 | §10.6 用例名 `test_rotate_cw`，输入 `ω=1.0` | 拆成 `test_rotate_ccw`（ω=+1.0）与 `test_rotate_cw`（ω=−1.0） | 右手系下 ω>0 是**逆时针**，原文的命名与输入不自洽。以数学与仿真实现为准，两个方向都覆盖 |
| 2 | §10.2 超限时"钳位到 max_rpm" | 四轮**等比缩放** | 逐轮硬钳位会改变各轮转速比例，使实际运动方向偏离指令方向。等比缩放与仿真侧 `mecanum_controller.py` 一致 |
| 3 | task-13 §13.1 提到帧层含"转义" | **不做字节填充** | LEN 已界定边界，转义会让帧长不可预测且使 LEN 失去意义。代价与缓解措施写入 ADR-0003 §决策-3 |
| 4 | §10.5 主循环 `HAL_Delay(1)` 驱动 PID | PID 在 **TIM6 1kHz 中断**中执行 | 采纳混元3 评审建议①。主循环只做串口与遥测 |
| 5 | 目录结构含 `stm32f4xx_conf.h`（HAL 库配置） | 保留该文件名，但作为**编译目标切换点**（裸机 / HAL / Host 测试三选一） | 不引入 STM32Cube，保证"克隆下来就能编"。需要官方 HAL 时定义 `USE_HAL_DRIVER` 即可切换 |
| 6 | §10.6 每个 `test_*.c` 各自编译运行 | 统一由 `test/test_main.c` 提供 `main()`，各测试文件导出 `run_*_tests()` | task-13 §13.4 的 Makefile 模板把所有 `test_*.c` 链进同一个二进制，多个 `main()` 会冲突 |
| 7 | `pid.c` / `crc16.c` 列在 `stm32_mecanum/src/` | 放在 `common/` | 遵循 task-13 §13.1 的权威布局 |

### 实现中发现并修复的设计缺陷

1. **积分限幅过小**：初版 `PID_INTEGRAL_LIMIT = 20`，而 `ki = 0.030` → 积分项最多只能贡献
   0.6 占空比。负载扰动测试暴露出"带载后转速永远差一截"。改为 40（`ki × limit ≥ output_limit`），
   并把这条整定准则写进注释与 README。
2. **拆帧器失同步窗口**：噪声插入一个杂散 `0xA5` 时，真实 SOF（0xA5 = 165）会被当成 LEN，
   解析器空等 165 字节，期间正常帧全被吞掉。这是所有"不转义 + 定长头"协议的固有缺陷。
   新增 `frame_parser_reset()` + USART IDLE 中断触发的空闲重同步作为解药，
   并用 `test_parser_desyncs_on_stray_sof_and_idle_reset_recovers` 把行为钉死。
3. **故障位只置不清**：`FAULT_STALL` 与 `FAULT_UART_ERROR` 初版设置后无法清除，
   一次瞬时堵转或开机噪声会让故障灯一直亮到复位。改为跟随实际状态；
   链路故障改用"距上次遥测的增量"判断而非累计值。

### 已知限制

- **HAL 层（`bsp.c` / `encoder.c` / `motor.c` / `uart.c`）尚未在真实硬件上验证。**
  Phase 1 的目标是"CI 编译通过 + Host 单元测试通过"。首次上板必须走 README §七 的检查清单。
- ~~交叉编译在本地开发机上未执行~~ —— **已解除（2026-07-29）**：CI `build-stm32-firmware` job
  首次运行即通过，`arm-none-eabi-gcc` 交叉编译 + 链接 + artifact 产出全部绿灯。
  本地无 `arm-none-eabi-gcc`，交叉编译始终由 CI 承担。
- PID 默认增益由一阶电机模型（τ≈80ms）整定，实车需按 README §4.2 复整定。

### 对下游任务的影响

| 任务 | 影响 |
|------|------|
| **task-11** | `common/` 已就位，可直接 `-I../common` 复用 `pid` / `crc16` / `protocol_frame` / `unity`。帧结构照搬，只改命令表与载荷长度 |
| **task-13** | CI 已有一个可参照的固件 job 模板（含版本注入）。`common/` 布局已落地，无需再做提取重构 |
| **task-14/15** | 树莓派端解析器按 ADR-0003 实现；README §3.4 的黄金帧可直接用作跨端自测向量 |

---

## 重构记录（2026-07-29）

task-11 在实现过程中发现了一条本任务当初漏掉的分层：**寄存器访问与板级逻辑
混在同一个文件里，会让最容易出错的逻辑变得不可测**。评审确认后回溯应用到本任务。
之所以现在动手，是因为本固件尚未上板 —— 不存在"已经能跑的不敢动"的顾虑，成本最低。

### 改了什么

| | 重构前 | 重构后 |
|---|---|---|
| 寄存器访问 | 散在 `bsp.c` / `encoder.c` / `motor.c` / `uart.c` / `main.c` 五个文件 | 全部收拢到 `port_stm32f407.c`，**唯一碰寄存器的文件** |
| 移植层接口 | 无 | `common/mcu_port.h`，与 MSPM0 固件**共用同一份接口** |
| 故障判定 | `main.c` 内的 `static` 函数 | `common/faults.c` 纯函数 |
| 故障位定义 | 各板 `protocol.h` 各写一份 | `common/faults.h` 唯一一份（线上契约） |
| `bsp.c/.h` | 存在 | 删除，职责并入移植层 |
| Host 用例数 | 52 | **63** |

新增的 11 个用例（`test_encoder.c`）全部是重构前**测不了**的：
16 位计数器正反向回绕、测速窗口保持、EMA 系数、四轮方向符号各自生效、四轮独立、
`encoder_reset()` 后不把复位前后的计数差算成一次巨大位移。

> 当时的 52 个用例里，没有一个能覆盖"计数器从 `0xFFF0` 走到 `0x0005`
> 到底被算成 `+21` 还是 `-65515`"。这正是分层不彻底的代价。

### 行为变更

**无。** 本次是纯结构重构：

- 时钟树、PWM 载频、编码器模式、UART 波特率与中断配置逐字节搬运，未做修改
- 故障判定逻辑与原 `main.c` 等价，但位间优先级（过流/急停期间 `STALL` 保持旧值）
  现在由 `faults.c` 的早退顺序保证并被测试钉死
- 原有 52 个用例全部保持通过

### 代价

- 每个控制周期多约 10 次函数调用，@168MHz 约 0.2µs，占 1kHz 周期的 0.02%
- `port_stm32f407.c` 单文件 ~400 行，比原来任何一个 HAL 文件都大 ——
  但它是**唯一**需要在换 MCU 时重写的文件，集中比分散好

### 已知限制（不变）

`port_stm32f407.c` 仍未在真实硬件上验证。首次上板必须走 README §七 的检查清单。
重构没有改变这一点，只是把待验证的范围从四个文件收敛到了一个。

---

## 参考资料

| 文件 | 内容 |
|------|------|
| `src/air_ground_car_bringup/scripts/mecanum_controller.py` | 仿真版麦轮逆运动学（算法需与固件一致） |
| `src/air_ground_car_bringup/urdf/mecanum_chassis.urdf.xacro` | 麦轮底盘几何参数 |
| `src/air_ground_car_bringup/config/chassis_params.yaml` | 底盘运动学参数（轮径、轮距等） |
| `src/air_ground_car_bringup/config/mecanum_chassis_control.yaml` | ros_control PID 参数（参考用于 STM32 PID 调参） |
| `project-prometheus-tasks/ICD.md` §三 | 控制接口抽象定义 |
| `project-prometheus-tasks/PLATFORM.md` §三 | 物理部署映射 |

---

## 给 Subagent 的执行建议

1. **先写 `kinematics.c` + 测试**：这是纯数学，最快有成果
2. **再写 `protocol.c` + 测试**：帧格式定义是后续所有通信的基础
3. **再写 `pid.c` + 测试**：PID 是独立模块
4. **最后写 `main.c`**：胶水代码，整合以上模块
5. **HAL 相关代码（`encoder.c`, `motor.c`, `uart.c`）写骨架即可**：用 `#ifdef STM32F407xx` 条件编译隔离 HAL 调用，Host 测试时 Mock 掉
6. **不追求固件在真 STM32 上跑通**：当前目标 = CI 编译通过 + Host 单元测试通过
7. **参数从 chassis_params.yaml 提取**：R5 麦轮底板几何参数，与仿真一致

---

*版本: v1.0 · 日期: 2026-07-28 · Phase 1 首个任务*
