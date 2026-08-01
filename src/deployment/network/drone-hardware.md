# 无人机树莓派5 硬件连接

> task-12 §12.5 的落地版。车机侧的连接见 [task-12 §12.5](../../../project-prometheus-tasks/task-12-drone-firmware-and-rpi-deployment.md) 的硬件清单与 [`../README.md`](../README.md) §1 角色表。

无人机 Pi 的连接比车机敏感：Pixhawk USB 断开等于飞控失联，D435i 带宽不足会
让深度图悄悄降质而**不报任何错误**。

---

## 1. 连接表

| 外设 | Pi 接口 | 稳定设备名 | 协议 | 备注 |
|------|---------|-----------|------|------|
| Pixhawk 6C | USB-C | `/dev/pixhawk` | MAVLink 2 @921600 | udev 按 VID 固定，见 [`99-air-ground-devices.rules`](99-air-ground-devices.rules) |
| RealSense D435i | **USB3（蓝色口）** | `/dev/video*` + `/dev/bus/usb` | UVC + libusb | 插 USB2 不报错、只降质，用 [`check-usb3.sh`](check-usb3.sh) 确认 |
| 3DR SiK 数传 | UART GPIO 14/15 | `/dev/telem` | 透传 57600 | 角色 = `air`，与车机那只配对 |
| 供电 | GPIO 5V (Pin 2/4) | — | — | 由机上 BEC 5V/3A 供电，**不要**用 USB 供电 |

> **为什么不用 USB 供电**：树莓派5 满载瞬时电流可以到 5A。USB 口供电在电机
> 启动的电流尖峰下会掉压重启 —— 现象是"飞起来就重启"，地面测试永远复现不了。

---

## 2. MAVLink 链路走向

```
无人机 Pi (192.168.1.20)
   MAVROS ──/dev/pixhawk──> Pixhawk 6C
      │
      └── gcs_url ──> /dev/telem (3DR air)
                          ╎ 无线
                      (3DR ground) ──> 车机 Pi (192.168.1.10)
                                          └── UDP :14550 ──> 服务器
```

车机 Pi 是 MAVLink 的汇集点，因此 MAVROS 的 `gcs_url` 指向车机而不是服务器。
时钟同步的主从关系也是同一个理由（车机当 NTP server，见
[`../chrony/chrony-car-server.conf`](../chrony/chrony-car-server.conf)）。

---

## 3. MAVROS launch 片段

task-12 §12.5 给的 launch 片段依赖 `/dev/pixhawk` 这个符号链接，
它由本目录的 udev 规则提供。实际的 launch 文件由 **task-14** 交付
（实机传感器驱动骨架），这里只记录接口约定：

```xml
<arg name="fcu_url" default="/dev/pixhawk:921600"/>
<arg name="gcs_url" default="udp://:14550@192.168.1.10:14550"/>
```

> `fcu_url` 里的波特率要与 Pixhawk 侧 `SER_TEL1_BAUD` 一致。
> 921600 是 USB CDC 上的名义值，USB 连接实际不受此限制，但参数写错时
> MAVROS 会一直报 "Device error"。

---

## 4. 上电顺序

1. **先**接好 3DR 数传与飞控，**再**给 Pi 上电
   —— udev 规则在开机时统一触发，热插拔虽然也能识别，但
   `air-ground-drone-edge.service` 的设备等待窗口只有 30 秒
2. 等状态灯稳定后再上动力电
3. 确认 `systemctl status air-ground-drone-edge` 是 `active (running)`

设备没识别出来时：

```bash
lsusb                                    # 总线上有没有
ls -l /dev/ | grep -E 'pixhawk|telem'    # 符号链接有没有
sudo udevadm control --reload-rules && sudo udevadm trigger
udevadm info -a -n /dev/ttyACM0 | grep -m2 -E 'idVendor|idProduct'
```

最后一条查出来的 VID/PID 与 udev 规则里的对不上，就改规则 ——
**不要**去改上层的设备路径，那会让符号链接这层抽象白做。
