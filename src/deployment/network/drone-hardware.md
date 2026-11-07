# 无人机 Raspberry Pi 5 / Pixhawk 6C 硬件连接

> ADR-0018 的部署落地版。生产连接与台架 USB 连接必须明确区分。

## 1. 连接表

| 外设 | 连接 | 容器内接口 | 协议/备注 |
|---|---|---|---|
| Pixhawk 6C TELEM2 | Pi 5 GPIO14 TX ↔ RX、GPIO15 RX ↔ TX、GND 共地 | `/dev/pixhawk` | MAVLink 2 @921600；宿主需 `dtoverlay=uart0-pi5`，实际为 `/dev/ttyAMA0` |
| Pixhawk 6C TELEM1 | 915 MHz/500 mW 空中电台 | 不进入 Pi 容器 | 地面端接 QGroundControl 主机；不传图像 |
| D435i CB | Pi USB3 | `/dev/bus/usb` | UVC + libusb；用 `check-usb3.sh` 核对链路速率 |
| 双 Pi Camera | 两个 CSI 口 | 待定 | 具体型号/端口/libcamera profile 未确认，`DRONE_VISION=pi_dual` 失败关闭 |
| IA6B | Pixhawk RC 输入 | 不进入 Pi | 输出协议、通道和 failsafe 待上机确认 |
| M9N GPS | Pixhawk GPS 口 | 由 MAVROS 间接提供 | 不直连 Pi |
| Pi 供电 | 独立 9–24 V→5 V/5 A 模块 | — | 与飞控/动力共地；先测压降、纹波和瞬态 |

TELEM 口与 Pi UART 均为 3.3 V 逻辑。不要把 Pixhawk TELEM2 的 5 V 引脚接到 Pi
5 V 电源；Pi 使用独立 5 V/5 A 模块供电。Pixhawk 由 PM07 按官方接线供电。

## 2. 链路拓扑

```text
无人机 Pi
  MAVROS ── /dev/pixhawk ── GPIO14/15 ── Pixhawk TELEM2
  D435i ── USB3

Pixhawk TELEM1 ── 915 MHz 空中端 ╎ 无线 ╎ 地面端 ── QGroundControl
Pixhawk RC     ── IA6B
Pixhawk GPS    ── M9N
```

Pi 的 MAVROS `gcs_url` 为空；遥测地面链路由 Pixhawk 直接处理。图像/Observation
走受控 IP 链路和项目 TCP 桥，不能转进低带宽电台。

## 3. 主机和 PX4 配置

Pi 5 的 `/dev/ttyAMA10` 是独立 3 针调试 UART，不是 40 针 GPIO14/15。生产接线：

```ini
# /boot/firmware/config.txt
enable_uart=1
dtoverlay=uart0-pi5
```

同时用 `raspi-config` 关闭串口 login shell，重启后确认 `/dev/ttyAMA0`。`.env`：

```dotenv
DRONE_FCU_DEVICE=/dev/ttyAMA0
DRONE_VISION=d435i
```

Pixhawk 侧 TELEM2 必须配置成 MAVLink 2，波特率与 `drone-edge-real.launch` 的
921600 一致；TELEM1 保留给遥测电台。参数名和取值以烧录的 PX4 版本/QGroundControl
为准，修改前导出完整参数备份。

台架允许用 Pixhawk USB：先用 udev 得到 `/dev/pixhawk`，再把
`DRONE_FCU_DEVICE=/dev/pixhawk`。USB 是调试备选，不改变容器内路径。

## 4. 上电/验收顺序

1. 拆下螺旋桨，断开动力电；检查 PM07、5 V/5 A 模块极性和共地；
2. 只给 Pixhawk/Pi 低压上电，验证 TELEM2 MAVROS heartbeat；
3. 接地面电台，验证 TELEM1 到 QGroundControl，断开电台不应终止 Pi 容器；
4. D435i 接 USB3，验证 RGB、对齐深度、点云和 10 分钟重连；
5. 验证 IA6B 通道、失控值、解锁开关，导出参数备份；
6. 完成无桨电机序号/方向测试后，才进入保护区系留实飞。

```bash
python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role drone
ls -l /dev/ttyAMA0
docker compose -f docker-compose.edge.yml -f docker-compose.drone.yml config
```
