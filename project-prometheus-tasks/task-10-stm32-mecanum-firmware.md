# Task-10: STM32F407 麦轮固件

> **状态：🔴 待开始** | **优先级：🥇 最高** | **预计耗时：6h（编码+测试）/ 3h（仅骨架+CI）**
>
> **适用环境**：任意 OS（Windows / macOS / Linux） + ARM GCC Toolchain
> **硬件依赖**：无（CI 编译 + 单元测试 Mock 电机反馈）
> **ROS 依赖**：无（独立固件项目，非 ROS Package）

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
  │  发布 /car/observation, /car/robot_state
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
src/firmware/stm32_mecanum/
├── README.md                    # 引脚定义表 + 协议帧格式 + 编译命令
├── Makefile                     # 顶层构建（调用 CMake 或直接 arm-gcc）
├── CMakeLists.txt               # CMake 构建（arm-none-eabi-gcc）
├── linker/
│   └── STM32F407VETx_FLASH.ld   # 链接脚本
├── src/
│   ├── main.c                   # 入口：初始化 + 主循环 (1kHz)
│   ├── kinematics.c/.h          # 麦轮逆运动学解算
│   ├── pid.c/.h                 # 通用 PID 控制器
│   ├── encoder.c/.h             # AB 相编码器读取 (TIM 编码器模式)
│   ├── motor.c/.h               # PWM 输出 + 电机控制
│   ├── protocol.c/.h            # 串口二进制帧协议 (打包/解包/CRC)
│   ├── uart.c/.h                # UART DMA 收发
│   └── stm32f4xx_conf.h        # HAL 库配置
├── test/
│   ├── test_kinematics.c        # 运动学解算单元测试 (纯 C, 无 HAL 依赖)
│   ├── test_pid.c               # PID 单元测试
│   ├── test_protocol.c          # 协议帧打包/解包测试
│   └── unity.c/.h               # Unity Test 框架 (单头文件)
└── .github/
    └── (CI 由仓库根 .github/workflows/ci.yml 统一管理, 见 Task-13)
```

> **注意**：`src/firmware/` 不是 ROS Package，不参与 `catkin build`。它是独立交叉编译项目。

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
│ 1 byte │ 1 byte │  1 byte  │   0~252 bytes    │ 2 bytes  │ 1 byte │
│  0xA5  │ n+4    │          │                  │ (LSB→MSB)│  0x5A  │
└────────┴────────┴──────────┴──────────────────┴──────────┴────────┘
SOF = 0xA5, EOF = 0x5A
LEN  = CMD(1) + DATA(n) + CRC(2) + EOF(1) = n + 4
```

#### 命令定义

| CMD | 方向 | 名称 | DATA | 响应 |
|-----|------|------|------|------|
| `0x01` | Pi→STM32 | `SET_VELOCITY` | `vx(f32) vy(f32) ω(f32)` = 12 bytes | `ACK` |
| `0x02` | Pi→STM32 | `EMERGENCY_STOP` | 无 (0 bytes) | `ACK` |
| `0x03` | Pi→STM32 | `PING` | 无 | `PONG` (含固件版本) |
| `0x11` | STM32→Pi | `TELEMETRY` | 四轮 RPM(f32×4) + 电流(f32×4) + 故障码(u16) = 34 bytes | — |
| `0x12` | STM32→Pi | `ACK` | 被确认的 CMD(u8) = 1 byte | — |
| `0x13` | STM32→Pi | `PONG` | 固件版本 major(u8).minor(u8).patch(u8) = 3 bytes | — |
| `0xFF` | STM32→Pi | `ERROR` | 错误码(u8) + 错误详情(变长) | — |

> **TELEMETRY 上报频率**：STM32 每 50ms (20Hz) 主动上报一次。Pi 不需轮询。

#### CRC 实现

```c
// CRC16-CCITT (x^16 + x^12 + x^5 + 1), 初始值 0xFFFF
uint16_t crc16_ccitt(const uint8_t *data, uint8_t len);
```

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

- [ ] `kinematics.c` 可通过逆运动学单元测试（5 个用例全部 PASS）
- [ ] `pid.c` 可通过阶跃响应测试（超调 < 20%, 稳态误差 < 5%）
- [ ] `protocol.c` 可通过帧打包/解包往返测试 + CRC 错误注入测试
- [ ] `make` 或 `cmake --build` 成功生成 `stm32_mecanum.bin`
- [ ] CI 交作业编译 job 绿灯
- [ ] `README.md` 包含：引脚定义表（4 路 PWM + 8 路编码器 + UART）+ 协议帧格式 + `make` 编译命令

---

## 参考资料

| 文件 | 内容 |
|------|------|
| `src/air_ground_car_bringup/scripts/mecanum_controller.py` | 仿真版麦轮逆运动学（算法需与固件一致） |
| `src/air_ground_car_bringup/urdf/mecanum_chassis.urdf.xacro` | 麦轮底盘几何参数 |
| `src/air_ground_car_bringup/config/chassis_params.yaml` | 底盘运动学参数（轮径、轮距等） |
| `src/air_ground_bringup/config/mecanum_chassis_control.yaml` | ros_control PID 参数（参考用于 STM32 PID 调参） |
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
