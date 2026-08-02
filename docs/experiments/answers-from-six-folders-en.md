# Answers from the Electronics Team — Six Hardware Folders

Below is what we found from reading the actual chip datasheets, SDK driver code, SysConfig
generated pin-outs, and factory firmware inside the six hardware documentation folders.

---

## 1. ICM42688 IMU — TDK 6-Axis Gyro/Accel

**Source files**: `dmx_icm42688.h`, `dmx_icm42688_iic.c`, `dmx_icm42688_iic.h`
(from DAIMXA open-source driver library, STC32G12K128 demo board package).

| Item | Value |
|---|---|
| Chip | TDK **ICM42688-P** |
| WHO_AM_I register | `0x75` |
| Expected device ID | `0x47` |
| I2C slave address used by SDK/demo | **`0x69`** (AD0 pin pulled HIGH on this breakout) |
| Note | Chip defaults to `0x68` when AD0=0. We must verify the actual breakout with `i2cdetect -y 1`. |
| I2C pins (STC32 demo board) | SCL=P4.0, SDA=P4.1 |
| SPI pins (STC32 demo board) | SPC=P4.0, SDI=P4.1, SDO=P4.2, CS=P4.3 |
| Default ranges in demo | ±16 g accelerometer, ±2000 dps gyroscope |
| Supported accel ranges | ±2 / ±4 / ±8 / ±16 g |
| Supported gyro ranges | ±15.625 / 31.25 / 62.5 / 125 / 250 / 500 / 1000 / 2000 dps |
| Sensitivity formula | `value_raw × range / 32768` |
| Key registers | PWR_MGMT0=`0x4E`, ACCEL_CONFIG0=`0x50`, GYRO_CONFIG0=`0x4F` |
| Accel data registers | `0x1F`–`0x24` (X1/X0, Y1/Y0, Z1/Z0) |
| Gyro data registers | `0x25`–`0x2A` |
| Interrupt pin | **Not used** in the demo — no `INT_CONFIG` register setup |
| Demo UART | UART1, P3.0/P3.1, 115200 bps |
| Vendor | DaimaX (daimxa.taobao.com), open-source at gitee.com/daimxa |
| Breakout board model | **Unknown** — the demo runs on an STC32G12K128 motherboard, not a Pi I2C header |
| Mounting orientation vs `base_link` | **Not specified** in these files |

---

## 2. MSPM0G3507 — TI Cortex-M0+ MCU & Robot Expansion Board

**Source files**: `ti_msp_dl_config.h`, `ti_msp_dl_config.c` (SysConfig-generated,
*DO NOT EDIT* header — these are the **actual** pin assignments from the TI SDK).

### 2.1 Confirmed pins (from SysConfig for the robot expansion board)

| Function | Pin | Source macro |
|---|---|---|
| UART0 TX | **PA10** | `GPIO_UART_0_TX_PIN = DL_GPIO_PIN_10`, IOMUX_PINCM21 |
| UART0 RX | **PA11** | `GPIO_UART_0_RX_PIN = DL_GPIO_PIN_11`, IOMUX_PINCM22 |
| UART0 baud | **115200** | `UART_0_BAUD_RATE` |
| Debug SWCLK | **PA20** | DEBUGSS |
| Debug SWDIO | **PA19** | DEBUGSS |
| LED D1 | **PB2** | IOMUX_PINCM15 |
| Button K1 | **PA2** | IOMUX_PINCM7 |
| System clock | **32 MHz** (internal oscillator, no PLL) | `CPUCLK_FREQ = 32000000` |
| UART peripheral clock | 4 MHz | `UART_0_INST_FREQUENCY` |
| SysTick timer | TIMG0, load value 3999 | `TIMER_0_INST_LOAD_VALUE` |
| Timer IRQ | `TIMG0_IRQHandler`, interrupt `TIMG0_INT_IRQn` | Generated |
| UART IRQ | `UART0_IRQHandler`, interrupt `UART0_INT_IRQn` | Generated |

### 2.2 K230 vision module companion wiring (from tutorial)

| K230 module pin | MSPM0 board pin |
|---|---|
| 5V | VCC |
| GND | GND |
| TXD (IO9) | **A22** |
| RXD (IO10) | **A21** |

K230 protocol: custom framed ASCII, header/trailer + comma-separated fields
carrying bounding box `(x, y, w, h)`.

### 2.3 Development board identity

- **LP-MSPM0G3507 LaunchPad** (Texas Instruments)
- Chip: MSPM0G3507, Cortex-M0+, 80 MHz capable, 128 KB Flash, 32 KB SRAM
- SDK: TI MSPM0 SDK with DriverLib + SysConfig code generation tool

---

## 3. OpenMV Visual Module

**Source files**: factory firmware `main.py` (color-following gimbal),
extension-board factory programs, `说明.txt`.

### 3.1 Camera identity

| Item | Value |
|---|---|
| Camera model | **OpenMV Cam H7** or **H7 Plus** (the factory programs target both) |
| Expansion board | **MV4** series: variant 1 (2-ch PWM servo) or variant 2 (with TFT LCD) |
| Firmware | MicroPython |
| Image sensor init | RGB565, QQVGA (160×120) |
| Serial port used | **UART3, 115200, 8N1** |
| Serial protocol | ASCII `#255P1400T1000!` (P=pan pulse, T=tilt pulse) |
| Onboard buttons | **P7** (btn1), **P9** (btn2) |

### 3.2 2-axis gimbal servos (PWM15S ×2, driven directly by OpenMV)

| Parameter | Value | Source code |
|---|---|---|
| Servo model | **PWM15S** ×2 | Confirmed by electronics team |
| Servo 1 (pan / bottom) | `Servo(1)` | `pan_servo = Servo(1)` |
| Servo 2 (tilt / top) | `Servo(2)` | `tilt_servo = Servo(2)` |
| PWM pulse range | **500–2500 µs** | `pan_servo.calibration(500,2500,1500)` |
| Center pulse | **1500 µs** | same line |
| Pan angle limit | **±90°** | code: `if pan_servo.angle() > 90 / < -90` |
| Tilt angle limit | **+45° to -60°** | code: `if tilt_servo.angle() > 45 / < -60` |
| Color threshold (red) | `(18, 74, 20, 127, -25, 127)` in LAB space | `target_threshold` |
| PID gains (online) | Pan: P=0.10, D=0.003; Tilt: P=0.10, D=0.006 | source |
| PID gains (offline) | Pan: P=0.07; Tilt: P=0.05 | commented-out fallback |

---

## 4. Two-Axis Gimbal (standalone)

**Source files**: Mixly firmware (`Jibot1-V2-AR出厂例程_Mixly2-231107.mix`),
installation manual PDF (unreadable in text).

| Item | Value |
|---|---|
| Gimbal model | **Two-axis gimbal Model B** (Jibot1-V2) |
| Supported MCU platforms | Arduino, STM32, STC51, ESP32 (four factory firmware sets) |
| Servo PWM values in firmware | **600 / 1500 / 2400 µs** |
| PS2 wireless controller | Supported in factory firmware (button-mapped servo actions) |
| Installation manual | `二维云台B款安装教程-V1.0-250630.pdf` (PDF, non-extractable text) |

**Controller ownership note**: The OpenMV factory program already drives the two servos
directly via `Servo(1)`/`Servo(2)` with its own PID loop. Whether the gimbal is
ultimately controlled by OpenMV, by the Pi via a separate MCU, or by OpenMV passing
through UART commands — this is not yet decided from the available files.

---

## 5. R5 Chassis Panel

- Only one file: `R5系列智能小车底板孔位介绍.pdf` (hole-position reference).
- The PDF content is **not extractable as text** from our tooling.
- Chassis type: **R5 standard acrylic/PCB universal robot car baseplate**.
- Wheel diameters and track dimensions come from the project BOM and
  `chassis_params.yaml`, **not** from physical measurement recorded in this folder.

---

## 6. STM32F407VET6

- One datasheet PDF and one schematic PDF (both non-extractable).
- A large set of STM32F4xx Standard Peripheral Library examples covering:
  LED, key, USART, RTC, ADC, DAC, PWM, DMA, SPI, RS485, CAN, TFT LCD, touch,
  NRF24L01, Flash/EEPROM, SD/FATFS, USB, DSP, UCOSII.
- **No chassis-specific pin-mapping is in this folder.** The actual motor/encoder/UART
  pin assignments live in the firmware project at `src/firmware/stm32_mecanum/`.
- Chip specs from firmware linker script: Cortex-M4F, 168 MHz, 512 KB Flash,
  192 KB SRAM (128 KB + 64 KB CCM).

---

## 7. Critical correction versus our own project code

| Parameter | Project code assumes | Actual from vendor files |
|---|---|---|
| ICM42688 I2C address | `real_sensors.yaml`: **`0x68`** | SDK driver: **`0x69`** (AD0=1) |
| MSPM0 CPU clock | Comment says 80 MHz | SysConfig actual: **32 MHz** (internal OSC, no PLL) |

The `0x68` in `real_sensors.yaml` must be re-verified with `i2cdetect -y 1` on the
actual Pi — if the breakout indeed has AD0 high, the driver will silently fail to
find the device.
