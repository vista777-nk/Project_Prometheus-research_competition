#!/usr/bin/env bash
# =============================================================================
# setup-fail2ban.sh — 安装并配置 fail2ban 保护 SSH
#
# 用法: sudo ./setup-fail2ban.sh
#
# 与 task-12 §12.6 原文的差异:
#   · 原文的 logpath 写死 /var/log/auth.log。树莓派 OS Bookworm 默认
#     **不装 rsyslog**, 认证日志只进 journald, 那个文件根本不存在 ——
#     fail2ban 会起不来并报 "Failed to access log file"。
#     这里改用 backend=systemd, 两种情况都能工作。
#   · 加了本网段白名单: 免得自己在实验室里连错几次密码把自己封了。
# =============================================================================
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "需要 root 权限: sudo $0" >&2
    exit 1
fi

echo "[1/3] 安装 fail2ban ..."
apt-get update
apt-get install -y --no-install-recommends fail2ban

echo "[2/3] 写入 jail 配置 ..."
# jail.local 而不是改 jail.conf: 后者会被包升级覆盖
install -m 0644 /dev/stdin /etc/fail2ban/jail.local <<'EOF'
# 由 src/deployment/ssh/setup-fail2ban.sh 生成 —— 手改会在下次运行脚本时被覆盖

[DEFAULT]
# 本网段不封禁。实验室里连错几次密码就把自己关在门外, 而这台机器
# 可能正装在无人机上, 代价远大于收益。
ignoreip = 127.0.0.1/8 ::1 192.168.1.0/24

bantime  = 3600
findtime = 600
maxretry = 3

[sshd]
enabled = true
port    = ssh
filter  = sshd
# 树莓派 OS Bookworm 默认不装 rsyslog, /var/log/auth.log 不存在,
# 认证记录只在 journald 里。用 systemd backend 兼容两种情况。
backend = systemd
EOF

echo "[3/3] 启用服务 ..."
systemctl enable --now fail2ban
systemctl restart fail2ban

echo ""
echo "完成。核对状态:"
echo "  sudo fail2ban-client status sshd"
echo ""
echo "误封了自己的话:"
echo "  sudo fail2ban-client set sshd unbanip <你的IP>"
