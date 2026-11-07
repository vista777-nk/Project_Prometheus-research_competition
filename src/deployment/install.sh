#!/usr/bin/env bash
# =============================================================================
# install.sh — 把部署配置装到树莓派的正确位置
#
# 用法 (在树莓派上, 仓库已 clone 到本机):
#   sudo bash src/deployment/install.sh --role car
#   sudo bash src/deployment/install.sh --role drone
#   bash src/deployment/install.sh --role car --dry-run    # 只打印要做什么
#
# 幂等: 重复执行是安全的, 已存在且内容相同的文件不会被动。
# **不会**覆盖 /opt/air-ground/.env —— 那里面是各机独有的配置。
#
# 刻意不做的事:
#   · 不自动 enable 服务。装完要人工核对一遍 .env 再启用,
#     否则一个写错的 ROS_IP 会让服务在开机时反复重启。
#   · 不装 docker。装 docker 有官方脚本, 不该在这里重造。
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPLOY_DIR="${REPO_ROOT}/src/deployment"
TARGET="/opt/air-ground"
LOG_DIR="/var/log/air-ground"
HOST_UID=1000
HOST_USER=""
HOST_GID=""

ROLE=""
DRY_RUN=0

usage() {
    sed -n '2,20p' "$0"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --role)     ROLE="${2:?--role 需要 car 或 drone}"; shift 2 ;;
        --dry-run)  DRY_RUN=1; shift ;;
        -h|--help)  usage 0 ;;
        *)          echo "未知参数: $1" >&2; usage 2 ;;
    esac
done

if [[ "${ROLE}" != "car" && "${ROLE}" != "drone" ]]; then
    echo "必须指定 --role car 或 --role drone" >&2
    exit 2
fi

if [[ ${DRY_RUN} -eq 0 && ${EUID} -ne 0 ]]; then
    echo "需要 root 权限 (要写 /etc 和 /opt): sudo $0 --role ${ROLE}" >&2
    exit 1
fi

run() {
    if [[ ${DRY_RUN} -eq 1 ]]; then
        printf '  [dry-run] %s\n' "$*"
    else
        "$@"
    fi
}

note() { printf '\033[1m%s\033[0m\n' "$*"; }

# =============================================================================
note "[1/8] 建立目录与用户"
# =============================================================================
passwd_entry="$(getent passwd "${HOST_UID}" || true)"
if [[ -z "${passwd_entry}" ]]; then
    if [[ ${DRY_RUN} -eq 1 ]]; then
        HOST_USER="airground"
        HOST_GID=1000
        run useradd --system --create-home --home-dir /home/airground \
            --shell /bin/bash --uid "${HOST_UID}" airground
    else
        useradd --system --create-home --home-dir /home/airground \
            --shell /bin/bash --uid "${HOST_UID}" airground
        passwd_entry="$(getent passwd "${HOST_UID}")"
    fi
fi

if [[ -n "${passwd_entry}" ]]; then
    IFS=: read -r HOST_USER _ resolved_uid HOST_GID _ <<< "${passwd_entry}"
    if [[ "${resolved_uid}" != "${HOST_UID}" || -z "${HOST_USER}" \
          || -z "${HOST_GID}" ]]; then
        echo "无法解析 UID ${HOST_UID} 的宿主机账户" >&2
        exit 1
    fi
fi

# 数字 uid 1000 要与镜像内的 airground 一致，宿主机用户名可以不同。否则挂进容器
# 的日志目录属主对不上，容器里写不进去（症状: ROS 日志目录是空的，但节点没报错）。
note "    宿主机运行账户: ${HOST_USER} (uid=${HOST_UID}, gid=${HOST_GID})"
run usermod -aG docker,dialout,i2c,plugdev,video "${HOST_USER}"

# 所有落盘文件使用数字 uid/gid；不能把镜像内用户名误当成宿主机账户名。
run install -d -m 0755 -o "${HOST_UID}" -g "${HOST_GID}" "${TARGET}"
run install -d -m 0755 -o "${HOST_UID}" -g "${HOST_GID}" "${LOG_DIR}"

# =============================================================================
note "[2/8] 部署配置文件到 ${TARGET}"
# =============================================================================
for sub in docker systemd scripts healthcheck network chrony ssh logging; do
    run install -d -m 0755 -o "${HOST_UID}" -g "${HOST_GID}" "${TARGET}/${sub}"
done

# compose / entrypoint / .env.example
for f in "${DEPLOY_DIR}"/docker/*.yml "${DEPLOY_DIR}"/docker/.env.example; do
    run install -m 0644 -o "${HOST_UID}" -g "${HOST_GID}" \
        "${f}" "${TARGET}/docker/"
done

# 可执行脚本
for f in "${DEPLOY_DIR}"/scripts/*.sh; do
    run install -m 0755 -o "${HOST_UID}" -g "${HOST_GID}" \
        "${f}" "${TARGET}/scripts/"
done
for f in "${DEPLOY_DIR}"/healthcheck/*.py "${DEPLOY_DIR}"/healthcheck/*.sh; do
    run install -m 0755 -o "${HOST_UID}" -g "${HOST_GID}" \
        "${f}" "${TARGET}/healthcheck/"
done

run install -m 0644 "${DEPLOY_DIR}/README.md" "${TARGET}/README.md"

# .env 只在不存在时创建, 绝不覆盖 —— 里面是各机独有的地址与 GID
if [[ ! -f "${TARGET}/.env" ]]; then
    run install -m 0640 -o "${HOST_UID}" -g "${HOST_GID}" \
        "${DEPLOY_DIR}/docker/.env.example" "${TARGET}/.env"
    note "    已从样例创建 ${TARGET}/.env —— **装完必须编辑它**"
else
    note "    ${TARGET}/.env 已存在, 保持不动"
fi

# =============================================================================
note "[3/8] udev 设备规则"
# =============================================================================
run install -m 0644 "${DEPLOY_DIR}/network/99-air-ground-devices.rules" \
    /etc/udev/rules.d/99-air-ground-devices.rules
run udevadm control --reload-rules
run udevadm trigger

# =============================================================================
note "[4/8] chrony 时间同步"
# =============================================================================
# 车机当 NTP server, 无人机与服务器向它同步 (见 chrony 目录下的说明)
run install -d -m 0755 /etc/chrony/conf.d
if [[ "${ROLE}" == "car" ]]; then
    run install -m 0644 "${DEPLOY_DIR}/chrony/chrony-car-server.conf" \
        /etc/chrony/conf.d/air-ground.conf
else
    run install -m 0644 "${DEPLOY_DIR}/chrony/chrony-client.conf" \
        /etc/chrony/conf.d/air-ground.conf
fi
run systemctl restart chrony || true

# =============================================================================
note "[5/8] 日志轮转与 journald 持久化"
# =============================================================================
run install -m 0644 "${DEPLOY_DIR}/logging/ros-logrotate.conf" \
    /etc/logrotate.d/air-ground
run bash "${DEPLOY_DIR}/logging/setup-journald.sh"

# =============================================================================
note "[6/8] systemd 单元"
# =============================================================================
run install -m 0644 "${DEPLOY_DIR}/systemd/air-ground-${ROLE}-edge.service" \
    /etc/systemd/system/
run install -m 0644 "${DEPLOY_DIR}/systemd/air-ground-healthcheck.service" \
    /etc/systemd/system/
run install -m 0644 "${DEPLOY_DIR}/systemd/air-ground-healthcheck.timer" \
    /etc/systemd/system/
run systemctl daemon-reload

# =============================================================================
note "[7/8] SSH 加固 (需要确认)"
# =============================================================================
# 刻意不自动装: 这份配置会关掉口令登录。若密钥还没分发好就启用,
# 下一次断开连接后就再也进不来了 —— 而这台 Pi 可能已经装在无人机上。
note "    ⚠ SSH 加固不自动启用。确认密钥能登录之后, 手动执行:"
note "        sudo install -m 0644 ${DEPLOY_DIR}/ssh/sshd_hardening.conf \\"
note "            /etc/ssh/sshd_config.d/10-air-ground.conf"
note "        sudo sshd -t && sudo systemctl reload ssh"
note "        sudo bash ${DEPLOY_DIR}/ssh/setup-fail2ban.sh"

# =============================================================================
note "[8/8] 完成"
# =============================================================================
cat <<EOF

装好了, 但**还没启用**。按顺序做完下面三步:

  1) 编辑本机配置 (ROS_IP / CHASSIS / 宿主机 GID):
       sudo -e ${TARGET}/.env
       # GID 用这条查: getent group dialout i2c | awk -F: '{print \$1"="\$3}'

  2) 确认镜像在本地:
       ${TARGET}/scripts/require-image.sh

  3) 启用服务:
       sudo systemctl enable --now air-ground-${ROLE}-edge.service
       sudo systemctl enable --now air-ground-healthcheck.timer
       #                                            ↑ enable 的是 .timer 不是 .service

核对:
  systemctl status air-ground-${ROLE}-edge
  journalctl -u air-ground-${ROLE}-edge -f
  ${TARGET}/healthcheck/check_nodes.py
EOF
