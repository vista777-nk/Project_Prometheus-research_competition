# 树莓派边缘节点部署

> **Task-12** · Phase 1 基础设施 · 两台树莓派5（车机 + 无人机）共用一套配置
>
> 状态：配置完成并通过静态校验（31 个 Host 用例 + 7 类静态检查）·
> **尚未在真实树莓派上执行过**，已知限制见 §9

---

## 1. 这是什么

两台树莓派5 的部署配置。**同一个 Docker 镜像**，靠环境变量区分角色：

| 角色 | IP | 连接的硬件 | 启动 |
|------|-----|-----------|------|
| 车机 | `192.168.1.10` | STM32/MSPM0 下位机 · RPLIDAR · ICM42688 · OpenMV · 3DR(ground) | `car_edge.launch` |
| 无人机 | `192.168.1.20` | Pixhawk 6C · RealSense D435i · 3DR(air) | `drone_edge.launch` |

```
实验室服务器 192.168.1.100  (ROS Master + GPU)
        ↕ TCP :9090
车机 Pi 192.168.1.10 ←─3DR 无线─→ 无人机 Pi 192.168.1.20
   ↕ /dev/mcu (下位机)              ↕ /dev/pixhawk (飞控)
```

车机 Pi 是 MAVLink 的汇集点，也是局域网 NTP 服务器 —— 两件事同一个理由，
见 [ADR-0006](../../docs/decisions/ADR-0006.md)。

---

## 2. 快速开始

```bash
# 在开发机上：改完配置先跑静态校验
bash src/deployment/validate.sh

# 在树莓派上：装配置（不会自动启用服务）
sudo bash src/deployment/install.sh --role car     # 或 --role drone

# 编辑本机配置，然后启用
sudo -e /opt/air-ground/.env
sudo systemctl enable --now air-ground-car-edge.service
sudo systemctl enable --now air-ground-healthcheck.timer
```

完整流程见 §4。

---

## 3. 目录结构

```
src/deployment/
├── validate.sh                     ★ 静态校验入口，本地与 CI 跑同一份
├── validate_consistency.py           跨文件一致性（compose 结构 / 话题名）
├── install.sh                        装到树莓派上，幂等
│
├── docker/
│   ├── Dockerfile.edge               边缘节点镜像（两个角色共用）
│   ├── Dockerfile.server             Phase 2 占位，构建时直接报错退出
│   ├── entrypoint.sh                 按 ROLE 决定启动哪个 launch
│   ├── docker-compose.edge.yml       公共基础
│   ├── docker-compose.car.yml        车机 override
│   ├── docker-compose.drone.yml      无人机 override
│   ├── docker-compose.car-sensors.yml  可插拔传感器（单独一层，见 §5.3）
│   └── .env.example                  各机配置样例
│
├── systemd/
│   ├── air-ground-car-edge.service
│   ├── air-ground-drone-edge.service
│   ├── air-ground-healthcheck.service
│   └── air-ground-healthcheck.timer
│
├── scripts/
│   ├── require-image.sh              启动前确认镜像在本地
│   └── wait-for-device.sh            等设备枚举，超时大声失败
│
├── network/
│   ├── 99-air-ground-devices.rules   udev：把会漂移的设备名钉死
│   ├── setup-3dr-radio.py            SiK 电台 AT 配置（默认只读）
│   ├── check-usb3.sh                 确认 D435i 在 USB3 口上
│   ├── air-ground-lan.nmconnection.template
│   └── drone-hardware.md             无人机侧接线与上电顺序
│
├── chrony/
│   ├── chrony-car-server.conf        车机 = NTP 服务器
│   └── chrony-client.conf            无人机 + 实验室服务器
│
├── ssh/
│   ├── sshd_hardening.conf
│   ├── setup-fail2ban.sh
│   └── generate-ssh-keys.sh
│
├── healthcheck/
│   ├── agcheck.py                    ★ 纯判定逻辑，无 I/O
│   ├── check_nodes.py                systemd timer 入口
│   ├── check_topics.py               人用的排查工具
│   ├── alert.sh                      journald / LED / 蜂鸣器
│   └── test_healthcheck.py           45 个 Host 用例
│
├── mavlink/                          MAVLink 2 签名（task-14）
│   ├── generate-mavlink-key.sh       默认写 ~/.config，拒绝往工作区写
│   ├── px4-signing.params            签名段刻意留空，见 ADR-0010
│   └── test-mavlink-signing.py       5 条黑盒断言
│
├── calibration/                      标定工具链（task-15，见其 README）
│   ├── record-calib-bag.sh           采集；开录前先查话题在不在线
│   ├── calibrate-camera.py           内参 → ROS camera_info 格式 YAML
│   ├── calibrate-imu.py              Allan 方差 → 噪声密度 / 零偏
│   ├── calibrate-cam-imu-extrinsic.py  Phase 1 只有接口与采集检查
│   ├── convert-bag-to-kalibr.py      本项目 YAML ↔ Kalibr；bag 抽帧
│   ├── validate-calibration.py     ★ 合理性检查，只依赖 PyYAML
│   ├── generate-calib-report.py      YAML → Markdown 报告
│   ├── calibration_db/               标定归档（只增不改，见其 README）
│   └── test/                         合成真值样本 + 16 条流水线用例
│
├── test/                             集成验证（task-15 Part B）
│   ├── test-serial-loopback.sh       入口，转发给 .py
│   ├── test-serial-loopback.py     ★ 帧协议第三份实现 + 13 条自测
│   └── test-observation-pipeline.py  真预处理器 + 真 World Model，10 条
│
└── logging/
    ├── ros-logrotate.conf
    └── setup-journald.sh
```

> `scripts/smoke-test-phase1.sh`（仓库根，不在本目录）是 Phase 1 的总入口，
> 它把上面这些的自测串起来跑一遍并检查 CI 归属。`make smoke-phase1`。

---

## 4. 从烧录系统到开机自启

### 4.1 烧录与首次登录

1. Raspberry Pi Imager 烧 **Raspberry Pi OS Lite (64-bit, Bookworm)**
2. 烧录前在 Imager 的高级选项里设好：主机名（`car-pi` / `drone-pi`）、
   用户名 `airground`、**勾选启用 SSH 并粘贴公钥**
3. 首次开机后：

```bash
sudo apt update && sudo apt full-upgrade -y
sudo raspi-config       # Interface Options: 启用 I2C、Serial Port（关登录终端、开硬件串口）
sudo reboot
```

> 树莓派5 的硬件串口默认被登录终端占用。不关掉的话，下位机的数据会
> 被当成控制台输入 —— 现象是串口能打开但收到的全是乱码。

### 4.2 装 Docker

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker airground
newgrp docker           # 或重新登录
docker run --rm hello-world
```

### 4.3 取得镜像

Phase 1 **离线分发**，没有 registry：

```bash
# 开发机（x86）上构建 ARM64 镜像需要 buildx + qemu
docker buildx build --platform linux/arm64 \
    -f src/deployment/docker/Dockerfile.edge \
    -t air-ground-edge:v1 --load .
docker save air-ground-edge:v1 | gzip > edge-v1.tar.gz
sha256sum edge-v1.tar.gz > edge-v1.tar.gz.sha256    # 供 Pi 上校验传输完整性

# 拷到 U 盘 → 插到树莓派 → 在 Pi 上:
sudo /opt/air-ground/scripts/update-image.sh
```

`update-image.sh` 把这条流程里**容易漏做的四步**变成必然执行：

| 步骤 | 漏做的代价 |
|------|-----------|
| 校验 SHA-256 | U 盘中途拔出 → `docker load` 报 `unexpected EOF`，看不出是传输坏了还是镜像坏了 |
| 查架构 | 开发机忘了 `--platform linux/arm64` → load 成功，容器启动才报 `exec format error` |
| 查剩余空间 | load 到一半盘满 → 留下无主 layer，还得手工 `docker system prune` |
| 备份旧镜像 | 新镜像有问题时无法回滚，场地上只能干等 |

```bash
sudo scripts/update-image.sh                     # 自动找 U 盘上的包
sudo scripts/update-image.sh /path/to/edge.tar.gz --restart   # 载入并重启（会中断实验）
sudo scripts/update-image.sh --rollback          # 回到上一个镜像
sudo scripts/update-image.sh --dry-run           # 只说要做什么
```

> **默认不重启**。新镜像下次启动才生效——镜像更新常在实验间隙做，
> 把中断的时机交给人决定。

也可以在树莓派上就地构建（首次约 30–60 分钟）：

```bash
cd ~/research_compitition
docker build -f src/deployment/docker/Dockerfile.edge -t air-ground-edge:v1 .
```

> **构建上下文必须是仓库根目录**（命令末尾的 `.`）。
> Dockerfile 里 `COPY ./src ./src` 取的是仓库的 `src/`。

### 4.4 装配置

```bash
sudo bash src/deployment/install.sh --role car        # 无人机用 --role drone
```

`install.sh` 做了什么：建目录与 `airground` 用户 → 拷配置到 `/opt/air-ground`
→ 装 udev 规则 → 按角色装 chrony → 装 logrotate 与 journald 持久化 → 装 systemd 单元。

**不做**的事：不自动 enable 服务、不装 SSH 加固、不覆盖已存在的 `.env`。
理由都写在脚本里 —— 简言之，这三件事做错的代价是"再也连不上这台机器"。

### 4.5 填本机配置

```bash
getent group dialout i2c | awk -F: '{print $1"="$3}'   # 查真实 GID
sudo -e /opt/air-ground/.env
```

必须改对的三项：

| 项 | 车机 | 无人机 |
|---|---|---|
| `ROLE` | `car` | `drone` |
| `ROS_IP` | `192.168.1.10` | `192.168.1.20` |
| `HOST_GID_DIALOUT` / `HOST_GID_I2C` | 上面查出来的实际值 | 同左 |

> `ROS_IP` 写错的症状很有迷惑性：`rosnode list` 能看到节点，
> `rostopic echo` 却永远没数据 —— 因为节点用一个别人连不上的地址注册到了 Master。

### 4.6 静态 IP

```bash
sed -e "s/@@ADDR@@/192.168.1.10/" -e "s/@@UUID@@/$(uuidgen)/" \
    src/deployment/network/air-ground-lan.nmconnection.template \
    > /tmp/air-ground-lan.nmconnection
sudo install -m 0600 -o root -g root \
    /tmp/air-ground-lan.nmconnection /etc/NetworkManager/system-connections/
sudo nmcli connection reload && sudo nmcli connection up air-ground-lan
```

> 权限必须是 `0600` 且属主 root，否则 NetworkManager 会**拒绝加载**，
> 只在 journal 里留一行 —— 现象是"文件明明在那儿却不生效"。

### 4.7 启用服务

```bash
/opt/air-ground/scripts/require-image.sh      # 先确认镜像在
sudo systemctl enable --now air-ground-car-edge.service
sudo systemctl enable --now air-ground-healthcheck.timer
#                                             ↑ 是 .timer 不是 .service
```

### 4.8 SSH 加固（**最后一步，顺序不能反**）

```bash
ssh airground@192.168.1.10 'echo ok'          # 先确认密钥能登录
sudo install -m 0644 src/deployment/ssh/sshd_hardening.conf \
    /etc/ssh/sshd_config.d/10-air-ground.conf
sudo sshd -t                                   # 语法检查，这一步不能跳
sudo systemctl reload ssh
sudo bash src/deployment/ssh/setup-fail2ban.sh
```

> **改 sshd 之前另开一个 SSH 会话不要关。** 配置写错时 reload 会让 sshd
> 拒绝新连接，已有会话是唯一的补救通道；否则只能拔卡插读卡器改文件。

### 4.9 验收

```bash
systemctl status air-ground-car-edge
journalctl -u air-ground-car-edge -f
/opt/air-ground/healthcheck/check_nodes.py     # 退出码 0 = 健康
chronyc tracking | grep 'System time'
ls -l /dev/ | grep -E 'mcu|pixhawk|telem'      # udev 符号链接
```

---

## 5. 关键设计取舍

### 5.1 systemd 的加固指令保护的是什么

`air-ground-*-edge.service` 里有一串 `ProtectSystem` / `NoNewPrivileges` /
`PrivateTmp`。**它们保护的不是边缘节点。**

这个单元唯一的工作是调用 docker CLI；真正的负载跑在 dockerd 创建的容器里，
**不在本单元的 cgroup 和 namespace 内**。这些指令能约束的只是那个几十毫秒的
CLI 包装进程。

真正的隔离边界在容器侧：非 root 用户、无 `privileged`、设备白名单。
把这一点写清楚，是因为一串看起来很严肃的加固指令很容易让人以为
安全问题已经解决了。详见 [ADR-0005](../../docs/decisions/ADR-0005.md) §决策-3。

网络这一侧的边界另有取舍，见 §5.5。

### 5.2 谁来重启

**systemd 独占。** compose 里是 `restart: "no"`。

两个重启控制器叠在一起（compose `unless-stopped` + systemd `Restart=always`）
会产生一种很难查的现象：容器崩了 docker 就地重启，systemd 完全看不到，
于是 `systemctl status` 一切正常、`RestartCount` 永远是 0，
而节点其实已经在后台反复重启。

### 5.3 为什么可插拔传感器要单独一层 compose

compose 的 `devices:` 里列的设备**不存在时容器直接启动失败**。

这对下位机和飞控是想要的行为（没有它们边缘节点本来也没意义，失败要响）；
对 USB 传感器不是 —— "雷达没插导致整台车的边缘节点起不来"是让故障范围
被配置放大。因此 RPLIDAR / OpenMV 放在 `docker-compose.car-sensors.yml`，
接上了才叠加。

### 5.4 健康检查管什么、不管什么

| 层次 | 谁负责 |
|------|--------|
| 容器进程活着 | docker + systemd `Restart=always` |
| **ROS Master 可达** | `check_nodes.py`（本任务） |
| **必需话题有发布者** | `check_nodes.py`（本任务） |
| 话题上真的有消息在流 | ✗ 需要 rospy，只能在容器内跑 `rostopic hz` |
| **能力集是否完整（降级运行）** | `check_nodes.py` 读容器写的状态文件 |

第 1 层已经有人管了，重复造轮子没有意义。第 2、3 层挂掉时进程往往还活得
好好的，重启策略完全看不见 —— 那才是健康检查的价值所在。

第 4 层做不到：判断消息速率必须订阅话题，需要 ROS 环境。
`check_topics.py` 里写明了这个边界，并给出容器内的替代命令。
**不把发布者存在性叫作"心跳"**。

**第 5 层是"跑着，但不是满配"。** `EDGE_MODE=real` 而
`car_edge_real.launch` 的 **real 后端尚未实现**时（见下方"现状"），车机拿不到真实
传感器数据。此时话题全都在发、Master 也正常，前四层**一片绿**，
但实机传感器驱动并没有真的在工作。

> **现状（2026-08-01）**：`car_edge_real.launch` 已被 task-14 交付（文件存在），
> 但默认 `backend:=mock`（4 个驱动节点起来发**假数据**），`backend:=real` 会抛异常退出。
> 而 `entrypoint.sh`（L116-128）的降级逻辑前提是"该文件不存在才降级"——由于文件现已存在，
> `EDGE_MODE=real` 时会**直接加载它跑 mock 后端、不触发降级、健康检查显示绿**。
> 这正是 ADR-0008 警惕的"假绿灯"。`entrypoint.sh` 的注释与降级语义已过时，
> 需重新设计 mock/real 判定（**代码待办，本文档仅更正事实，未改动代码**）。

只打一条 echo 是不够的——它留在容器日志里，上位机看不见。所以 entrypoint
在 roslaunch **之前**把结论写进 `/var/log/air-ground/edge-state.env`
（bind mount，容器外可读），`check_nodes.py` 读它并以退出码 **4** 报出：

```
[2026-07-31T12:00:00] DEGRADED: 降级运行 (role=car, 话题齐全但能力集不完整)
  car_edge_real.launch 未提供 (task-14 未交付), 已降级为 car_edge.launch —— 实机传感器驱动未启动
```

三处配合，缺一个就静默失效，因此 `validate.sh` §7 会交叉校验：

| 位置 | 作用 |
|------|------|
| `entrypoint.sh` | 每次启动重写状态文件（**不是只在降级时写**，否则修好之后旧文件会一直挂着） |
| `agcheck.py` | `EXIT_DEGRADED = 4`，优先级**低于** Master 掉线和话题缺失——降级不该盖住真故障 |
| `air-ground-healthcheck.service` | `SuccessExitStatus=4`。降级是已知预期状态，每 30 秒标一次 failed 会让 `systemctl --failed` 长期挂红，真出事时反而没人看 |
| `alert.sh` | 4 → warning，不是 critical。节点在跑、数据在发，亮红灯会让现场以为要停飞 |

> 这只是过渡方案。"这台车现在有哪些能力"的正确归宿是 ICD 里的
> `Capability` 消息（task-14 交付）。届时状态文件应退化为 entrypoint
> 的启动自检记录，判定权交给 `Capability`。

---

### 5.5 `network_mode: host` 的安全边界与部署纪律

边缘容器与宿主机**共享网络命名空间**。这不是配置疏忽——ROS 1 的节点端口
由内核随机分配、没有任何配置项能约束成一个范围，`bridge` 模式声明不出
`ports:`。完整推导与被否决的替代方案见
[ADR-0007](../../docs/decisions/ADR-0007.md)。

真正新增的风险只有一条：**容器内进程能连到宿主机 `127.0.0.1` 上的服务**。
（改网络配置、抓包这些仍然做不到——它们由 capability 控制，未授予。）

因此有三条部署纪律，装机时必须遵守：

1. **宿主机上不跑绑定 `0.0.0.0` 的管理服务**——Web 控制台、Jupyter、
   远程调试端口一律不装，或只绑 `127.0.0.1` 并知道它对容器仍然可达。
   SSH 是唯一例外，已由 §4.8 的加固覆盖。
2. **不做端口转发到边缘节点网段**。静态 IP 在内网段（[ADR-0006](../../docs/decisions/ADR-0006.md)），
   与外网之间隔着实验室路由器。
3. **同一台 Pi 上不跑互不信任的第三方容器**——host 模式下它们之间
   没有任何网络隔离。

以下任一情况出现时，**动手之前**先回到 ADR-0007 §决策-3 重估：
接外网 · 迁 ROS 2 · 同机跑第二个互不信任的负载。

## 6. 与任务文档（task-12）的偏差

全部是"照抄会失败"的问题，逐条列出理由：

| # | 文档原文 | 实际实现 | 为什么 |
|:---:|---|---|---|
| 1 | `FROM ros:noetic-ros-core-focal` | `ros-base` | ros-core 里 rosdep **没初始化过**，`rosdep install` 会直接报错 |
| 2 | 直接调 `catkin build` | 显式装 `python3-catkin-tools` | catkin_tools 不在任何 `ros:*` 基础镜像里，原文会 command not found |
| 3 | `docker build -f docker/Dockerfile.edge .` | 上下文改为仓库根 | 原文的 `.` 下没有 `./src`，`COPY` 会失败 |
| 4 | `privileged: true` **且** `devices:` | 删掉 privileged | 两者矛盾：privileged 已给了全部设备，那份白名单一行都不起作用，只是看起来像做了权限控制（评审建议 1） |
| 5 | compose `restart: unless-stopped` | `restart: "no"` | 与 systemd `Restart=always` 冲突，见 §5.2 |
| 6 | `ExecStartPre=docker compose pull` | 改为 `require-image.sh` | Phase 1 离线分发没有 registry，开机 pull 必然失败 → 整个单元起不来 |
| 7 | `roslaunch ... chassis:=${CHASSIS}` | `default_chassis:=` | `car_edge.launch` 声明的是 `default_chassis`；roslaunch 对未声明参数是**硬错误**（`RLException: unused args`） |
| 8 | `Requires=dev-ttyACM0.device` | `Wants=dev-pixhawk.device` + 显式等待 | ① ttyACM 编号会漂移 ② `Requires` 硬阻塞导致飞控没插时相机数传也起不来。udev 规则补了 `TAG+="systemd"` 与 `SYSTEMD_ALIAS`，否则 `dev-pixhawk.device` 这个单元根本不存在 |
| 9 | `ChallengeResponseAuthentication no` | `KbdInteractiveAuthentication no` | 前者在 OpenSSH 9.x 已**移除**；Bookworm 带的是 9.2，照抄会让 `sshd -t` 报错、sshd 起不来 —— 把自己锁在门外 |
| 10 | fail2ban `logpath=/var/log/auth.log` | `backend=systemd` | Bookworm 默认不装 rsyslog，那个文件不存在，fail2ban 会起不来 |
| 11 | `setup-3dr-radio.sh` 里的 Python 片段 | 改写为真实 AT 命令实现（`setup-3dr-radio.py`） | 原文只建了个连接就打印"configured"，**一个参数都没设**。默认改为只读 |
| 12 | `check-d435i-usb.sh` 扫描任意 5000M 端口 | 定位相机自身所在端口 | 树莓派5 本身就有 USB3 口，原检查**恒为真**，相机插 USB2 也报通过 |
| 13 | `check_nodes.py` 硬编码 Master 地址 | 环境变量 + CLI 参数 | 硬编码没法在开发机上对着别的 Master 跑 |
| 14 | `Dockerfile.server` 占位 | 保留占位但**构建时主动报错退出** | 与 ADR-0004 §决策-3 同一原则：不确定的部分标清边界，不填满它 |

---

## 7. 校验

```bash
bash src/deployment/validate.sh
```

| 检查项 | 本地(Git-Bash) | Linux CI |
|--------|:---:|:---:|
| shell 语法 `bash -n` | ✓ | ✓ |
| Python 语法 + 45 个单元测试 | ✓ | ✓ |
| 串口协议自测 13 条（黄金帧 / CRC / 拆帧） | ✓ | ✓ |
| 标定流水线 16 条（合成真值） | 需 numpy+OpenCV | ✓ |
| compose 结构 + 话题名一致性（含标定采集话题） | ✓ | ✓ |
| 行尾必须是 LF | ✓ | ✓ |
| 私钥 / 明文口令扫描 | ✓ | ✓ |
| systemd `[Unit]`/`[Service]` 段归属自查 | ✓ | ✓ |
| `systemd-analyze verify` | SKIP | ✓ |
| 降级状态契约两端一致 | ✓ | ✓ |
| 镜像 Python 依赖版本 == `requirements.txt` | ✓ | ✓ |
| `docker build` | SKIP | 另一个 job |

跳过的项**会明确报 SKIP**，不静默略过。

> 校验脚本自己也做过负向测试：塞一个语法错误的脚本、一个 CRLF 的 unit、
> 一份假私钥进去，确认三项都会红。第二项一开始**没红** ——
> Git for Windows 的 grep 会静默剥掉 CR，`grep $'\r'` 在 Windows 上
> 永远匹配不到。已改用字节数比对，理由写在 `validate.sh` §4 的注释里。
>
> 第二次是 CI 抓的：`StartLimitIntervalSec` 写在了 `[Service]` 段，
> systemd v230 起它属于 `[Unit]`——旧名 `StartLimitInterval` 留了兼容别名、
> 新名没有，于是 systemd 打一句 "Unknown key name … ignoring" 就**静默忽略**，
> 单元照常工作而限流从未生效。已补 §6a 段归属自查，本机也能查（不依赖 systemd）。
> 同一次还发现 `systemd-analyze verify` 会连带加载依赖单元、把它们的错误算到
> 被检单元头上——四个单元全红而真实错误只有两处。改为一次性校验全部单元。
>
> 第三次是 ARM64 镜像构建首次运行时抓的：`pip3 install pymavlink` 直接失败。
> 根因是 Focal 的 pip 是 20.0.2，认不出 PEP 600 的 `manylinux_2_28_aarch64`
> 轮子标签（要 pip 20.3+），于是退回去编译 lxml 源码，而镜像里没有编译器。
> 详见 `Dockerfile.edge` §Python 依赖的注释。顺带发现 Dockerfile 的注释写着
> "与 requirements.txt 对齐"而上界 `<3.0.0` 从没同步过来——已补交叉校验。

---

## 8. 排查手册

| 症状 | 先查这里 |
|------|---------|
| 服务起不来 | `journalctl -u air-ground-*-edge -n 50 --no-pager` |
| 容器起不来，报 device not found | `ls -l /dev/ \| grep -E 'mcu\|pixhawk\|telem'`；没有符号链接就是 udev 没生效 |
| 容器内 `Permission denied: /dev/mcu` | `.env` 里的 `HOST_GID_*` 与宿主机不符，用 `getent group dialout i2c` 核对 |
| 节点可见但话题没数据 | `ROS_IP` 没设或设错 |
| 健康检查一直报 Master 不可达 | `ping 192.168.1.100`；再 `cat /opt/air-ground/.env \| grep MASTER` |
| 深度图/IMU 出不来，彩色图正常 | D435i 插在 USB2 上，跑 `network/check-usb3.sh` |
| 时间戳对不齐，观测被判过期 | `chronyc sources -v`，车机那行应有 `^*` |
| SD 卡写满 | `journalctl --disk-usage`；`du -sh /var/log/air-ground` |

---

## 9. 已知限制

- **整套配置尚未在真实树莓派上执行过。** Phase 1 手上没有硬件，
  本任务的目标是"静态可校验 + CI 可构建"。首次上机必须按 §4 逐步走，
  不要跳步。
- **`docker build` 未在本地执行过**（开发机是 Windows，无 Docker）。
  由 CI 的 `build-edge-image` job 首次验证。ARM64 构建更是只能在 CI 上做
  （需要 buildx + qemu）。首次运行即失败于 `pip3 install pymavlink`，
  修复见 `Dockerfile.edge` §Python 依赖的注释——**该修复同样未能在本地验证**，
  是照着"pip 20.0.2 不认 PEP 600 轮子标签"这条推断做的，
  并同时堵住了另外两条可能的失败路径（apt 预装 lxml/future、pip 升级）。
- **`systemd-analyze verify` 未在本地执行过**（非 Linux），由 CI 校验。
- **udev 规则里的 VID/PID 与序列号需上机核对。** 3DR 数传和 RPLIDAR
  都用 CP2102 芯片（`10c4:ea60`），必须靠序列号区分。规则里留的是
  `REPLACE_WITH_*_SERIAL` 占位符 —— 不填的话那两条规则不匹配任何设备，
  这是刻意的：宁可符号链接不出现（`wait-for-device.sh` 会报出来），
  也不要两个设备随机抢同一个名字。
- **`setup-3dr-radio.py` 未在真实电台上验证。** AT 命令集依据 SiK 固件
  公开文档。默认只读，首次上机先确认能进命令模式再考虑 `--apply`。
- **`car_edge_real.launch` 的 real 后端尚未实现**（文件已被 task-14 交付，默认
  `backend:=mock` 发假数据，`backend:=real` 抛异常退出）。当前 `entrypoint.sh`
  的降级前提是"文件不存在"，而文件已存在，故 `EDGE_MODE=real` 时会加载它跑
  mock 后端、**不触发降级**（见 §5.4"现状"）。**上机验收时车机跑的是 mock 假数据、
  健康检查显示绿**——这是需要警惕的状态，不是"传感器已通"。该降级语义待重新设计。
- **降级状态目前只覆盖 `car_edge_real.launch` 这一种情况。** 传感器掉线、
  相机没枚举到之类的部分能力缺失还没有对应的判定——那需要 task-14 的
  `Capability` 消息，不是一个启动期状态文件能表达的。
- **网络降级（WiFi→4G）与本地缓存模式未实现**，是 Phase 2 规划
  （task-12 评审建议 4）。

---

## 10. 相关文档

| 文档 | 内容 |
|------|------|
| [task-12](../../project-prometheus-tasks/task-12-drone-firmware-and-rpi-deployment.md) | 任务定义与验收标准 |
| [ADR-0005](../../docs/decisions/ADR-0005.md) | 为什么用 Docker 而不是裸机部署 |
| [ADR-0006](../../docs/decisions/ADR-0006.md) | 静态 IP 与时钟主从的分配方案 |
| [ADR-0007](../../docs/decisions/ADR-0007.md) | `network_mode: host` 的暴露面与重估触发条件 |
| [network/drone-hardware.md](network/drone-hardware.md) | 无人机侧接线与上电顺序 |
| [PLATFORM.md §三](../../project-prometheus-tasks/PLATFORM.md) | 物理部署映射 |
| [SECURITY.md](../../SECURITY.md) | 项目安全策略 |
