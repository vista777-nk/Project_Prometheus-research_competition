# Phase 1.5 Pi 基线预检（2026-08-03）

## 范围

本记录对应当前接入的单台 Raspberry Pi，目标是确认主机基线并区分代码侧通过与实机
部署就绪。负责人后续澄清该机目前未连接任一底盘或项目外设；本次只按拟用车机宿主的
`car` 角色准备入口，不执行任何底盘带电测试。

## 环境事实

| 项目 | 实测值 |
|---|---|
| Git 分支 / 提交 | `task-new` / `f4c25f32e305b72c8da709ca2d535d3557af82bb` |
| 主板 | Raspberry Pi 5 Model B Rev 1.1 |
| 架构 | `aarch64` |
| 系统 | Debian GNU/Linux 13.6 (trixie) |
| 项目角色 | 拟用车机 `car`（当前为独立台架主机） |
| Python | 3.13.5 |
| 内存 | 7.9 GiB |
| 根文件系统 | 56.8 GiB（约 20% 已用） |
| 网络 | 当前仅 Wi-Fi；未记录临时地址 |
| 外接稳定设备 | 未发现 `/dev/mcu`、`/dev/rplidar`、`/dev/openmv` 或 `/dev/pixhawk` |

## 首次接手命令与结果

```text
python3 src/deployment/healthcheck/check_pi_host.py --stage base --json
=> ok=true；5/5 基线检查通过

python3 src/deployment/healthcheck/check_pi_host.py --stage deploy --role car --json
=> ok=false；Docker Engine、Docker Compose、/dev/i2c-1、/dev/ttyAMA0 均未就绪

bash scripts/smoke_test_phase1.sh
=> 通过 62，失败 0，跳过 3
   跳过：NumPy/OpenCV/PyYAML 标定与 Observation 流水线、pymavlink 签名自测

bash src/deployment/validate.sh
=> 通过 53，失败 0，跳过 2
   跳过：pymavlink 签名自测、标定流水线
```

## Boot / 服务状态

- `/boot/firmware/config.txt` 仍为默认状态：`dtparam=i2c_arm=on` 被注释，未启用
  `dtoverlay=uart0-pi5`。
- 当前存在的是 `/dev/ttyAMA10`（Pi 5 三针调试 UART），不能作为 40 针生产串口。
- 当前 `systemd-timesyncd` 为 `enabled/active`；没有安装 chrony，不在未测量前替换
  正常工作的时间同步服务。
- Docker 与 Compose 未安装；当前没有项目服务或实验室服务器端口在本机监听。

## 基线后推进（同日）

为关闭与角色无关的主机前置项，安装了 Debian arm64 原生包：

```text
docker.io 26.1.5+dfsg1-9+b13
docker-cli 26.1.5+dfsg1-9+b13
docker-compose 2.26.1-4
```

- `docker.service` 与 `containerd.service` 均为 `enabled/active`；`docker info` 报告
  `Architecture=aarch64`、`Cgroup=2`。
- `docker compose version` 成功；车机（含可插拔传感器层）和无人机 Compose 配置均通过
  原生 `docker compose config`。
- `docker run hello-world` 因当前网络访问 Docker Hub 超时失败；这不是镜像证据，仍按
  项目离线 U 盘/镜像包流程处理。
- `require-image.sh` 在没有 `air-ground-edge:v1` 时以退出码 1 失败，未尝试联网拉取，
  与离线部署约定一致。
- 重新运行 deploy 预检后，Docker/Compose 两项已通过；I²C/UART 设备项仍按预期失败。

安装过程中没有修改 `/boot/firmware/config.txt`、没有重启，也没有启用项目 systemd
服务。APT 刷新还报告 VS Code 第三方源缺少签名密钥；该源未被本次处理。

## 车机接口配置（同日）

此前按负责人提供的“车机角色、已重启一次”信息推进。核查表明该次启动发生在接口配置
落盘前：重启后仍只有 `/dev/ttyAMA10`，没有 `/dev/i2c-1` 或 `/dev/ttyAMA0`，且
`serial-getty@ttyAMA10.service` 因 `console=serial0,115200` 仍在运行；负责人随后澄清
当前 Pi 尚未连接底盘，因此下面只记录宿主入口，不推导车辆安装状态。

已将原始 boot 文件备份到 `/boot/firmware/prometheus-backup/`，随后写入并逐字节复核：

- `config.txt`：启用 `dtparam=i2c_arm=on`；在 `[pi5]` 增加
  `dtoverlay=uart0-pi5`。
- `cmdline.txt`：只移除 `console=serial0,115200`，保留 `console=tty1` 和其余启动参数。
- 本地 overlay 文档确认 `uart0-pi5` 对应 GPIO14/15；没有启用 SPI 或其他未使用接口。

改动落盘后已完成第二次重启，结果见下节。在实际设备节点可打开之前，接口项仍不能
记为完全通过。

本次实机核查还暴露了预检缺口：旧脚本只检查 `/dev/ttyAMA0` 是否存在，不检查
`console=` 串口登录参数。现已增加串口控制台检查及三个负向/解析用例，Pi 主机预检
为 13/13。重启前的真实运行正确报告三项失败：`/dev/i2c-1`、`/dev/ttyAMA0` 和
`console=ttyAMA10,115200`；因此重启后的绿灯将同时包含设备节点与控制台释放证据。

## 配置后重启验证（同日）

系统在 `2026-08-03 20:12:35 CST` 重新启动。重启后的只读证据：

- 活动内核命令行只保留 `console=tty1`，没有 serial/ttyAMA/ttyS 控制台参数；进程表
  和 systemd generator 中也未发现 `agetty`/serial-getty。
- sysfs 与内核日志确认 `i2c-1` 已注册；udev 元数据给出的目标节点为
  `/dev/i2c-1`、设备号 `89:1`。
- sysfs 与内核日志确认 PL011 `ttyAMA0` 已注册；udev 元数据给出的目标节点为
  `/dev/ttyAMA0`、设备号 `204:64`。生产配置显式使用该路径，不依赖 `serial0` 别名。
- 当前 AI 执行环境的受限 mount namespace 不暴露宿主机 `/dev`，system D-Bus 也不可达；
  因此 `i2cdetect -y 1` 与 `stty -F /dev/ttyAMA0` 在此环境中均以“节点不存在”失败。
  这不能作为宿主机节点不存在的证据，也不能伪写为通过。
- USB sysfs 只看到键盘和鼠标接收器，没有 A2M12、OpenMV 或底盘 USB-UART；未记录
  设备序列号。

针对本次沙箱现象，deploy 预检新增 sysfs 注册状态：仍坚持只有实际 `/dev` 节点存在
才通过，但现在会把“overlay 未生效”和“内核已注册、节点被 udev/namespace 隐藏”
分开报告。连同 JSON 集合序列化回归，Pi 主机预检现为 15/15。

首次重跑 `validate.sh` 得到通过 52、失败 1、跳过 2；失败内容只有
`SO_PASSCRED failed: Operation not permitted`。这来自当前沙箱阻止
`systemd-analyze verify` 建立凭据 socket，且分析器可能在读取 unit 前退出，不能过滤后
伪装成通过。校验器现将这一条精确状态报告为 SKIP；最终为通过 52、失败 0、跳过 3，
真实 unit 语法仍由 CI 阻塞验证。

## UID 1000 运行账户兼容（同日）

本机 UID 1000 的实际账户名是 `vista2`，不存在 `airground`。旧安装器会先尝试创建
另一个 UID 1000 的 `airground`（必然失败且被 `|| true` 隐藏），随后在第一条
`install -o airground` 处中止；三个 systemd unit 也会因 `User=airground` 无法启动。

现已把权威契约收敛为数字 UID 1000：

- 安装器通过 `getent passwd 1000` 解析现有宿主账户；只有 UID 1000 不存在时才创建
  推荐名 `airground`，且不再忽略用户/设备组配置失败。
- 文件和目录使用数字 uid/gid 落盘；车机、无人机和健康检查 unit 使用 `User=1000`，
  不要求宿主用户名与容器内用户名相同。
- 本机 car dry-run 正确选择 `vista2 (uid=1000, gid=1000)`，没有用户创建动作；相关
  UID、unit 和原有主机预检测试共 17/17 通过。
- 使用隔离的 `getent` 模拟“UID 1000 尚不存在”的新主机时，dry-run 会创建推荐账户
  `airground` 并继续使用数字 uid/gid 安装，另一分支也已覆盖。

## 宿主权限恢复后的直接复核（同日）

网络服务与宿主设备访问恢复后，直接在当前 Pi 上完成最终只读复核：

- `/dev/i2c-1`（`89:1`）和 `/dev/ttyAMA0`（`204:64`）均为真实字符设备且可打开；
  `/dev/serial0` 仍指向三针调试口 `ttyAMA10`，生产串口必须继续显式使用 `ttyAMA0`。
- `i2cdetect -y 1` 成功扫描总线且地址表全空，与尚未连接 IMU 或其他 I²C 外设一致；
  这只证明总线入口可用，不是传感器验收。
- `stty -F /dev/ttyAMA0 -a` 成功打开串口；当前默认显示 9600 baud，但尚无 MCU 可做
  目标波特率、收发、CRC、重连或看门狗验证。
- `check_pi_host.py --stage deploy --role car --json` 为 `ok=true`，10/10 主机部署前置项
  通过；这不包含 `/dev/mcu`、真实传感器或底盘运动能力。
- `lsusb` 仍只见键盘和鼠标接收器，没有项目 USB 外设。
- `systemd-analyze verify` 已实际读取三个修改后的 unit，只报告尚未安装到 `/opt` 的命令
  路径不存在；完整 `validate.sh` 为通过 53、失败 0、跳过 2，证明 unit 语法已在 Pi 上
  执行验证。

## 已验证

1. 当前 SD 卡、架构、Pi 5 型号、Debian 13 和内存满足项目 base 阶段门槛。
2. 仓库在本机可运行的 Phase 1 结构/语法/Host 检查无失败。
3. 实机 `car` deploy 主机前置项 10/10；该结论仅覆盖 Docker、I²C/UART 与控制台释放，
   未把调试 UART 或缺失项目外设误判为整车部署就绪。

## 未验证 / 下一步前置

- Docker/Compose 已安装；仍需取得带 SHA-256 的 ARM64 离线边缘镜像。
- 尚未执行 `/opt` 安装或启用角色服务；当前 Pi 未连接底盘、MCU 或任何项目外设。
- 尚无 MCU 串口通信、ICM42688、RPLIDAR、OpenMV、Pixhawk、相机或稳定网络配置证据。
