#!/usr/bin/env bash
# =============================================================================
# setup-journald.sh — journald 持久化 + 容量上限
#
# 用法: sudo ./setup-journald.sh
#
# 为什么需要:
#   1. 树莓派 OS 默认 journald 存在 /run (tmpfs) 里, **一重启全没了**。
#      而边缘节点最需要看的恰恰是"上次为什么挂的"。
#   2. 持久化之后如果不设上限, journal 会慢慢吃满 SD 卡。
# =============================================================================
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "需要 root 权限: sudo $0" >&2
    exit 1
fi

echo "[1/3] 建立持久化目录 ..."
mkdir -p /var/log/journal
systemd-tmpfiles --create --prefix /var/log/journal

echo "[2/3] 写入 journald 配置 ..."
mkdir -p /etc/systemd/journald.conf.d
install -m 0644 /dev/stdin /etc/systemd/journald.conf.d/air-ground.conf <<'EOF'
# 由 src/deployment/logging/setup-journald.sh 生成

[Journal]
# 落盘而不是只放 tmpfs —— 重启后还能查上次挂掉的原因
Storage=persistent

# SD 卡容量有限, 且写满会引发一连串莫名其妙的系统故障
SystemMaxUse=500M
SystemKeepFree=1G
SystemMaxFileSize=50M
MaxRetentionSec=1month

# 降低 SD 卡写入频率。代价: 断电时最后 5 分钟内的日志可能丢失。
# 对"事后查为什么挂的"这个用途, 5 分钟的窗口通常够用,
# 而 SD 卡写入寿命是实打实的约束。
SyncIntervalSec=5m

# 单个服务刷日志时不要把别人的记录挤掉
RateLimitIntervalSec=30s
RateLimitBurst=10000
EOF

echo "[3/3] 重启 journald ..."
systemctl restart systemd-journald

echo ""
echo "完成。核对:"
echo "  journalctl --disk-usage"
echo "  journalctl -u air-ground-car-edge -b -1   # 上一次启动的日志"
