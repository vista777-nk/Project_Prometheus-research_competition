#!/usr/bin/env bash
# =============================================================================
# generate-ssh-keys.sh — 生成部署用 SSH 密钥对
#
# 用法: ./generate-ssh-keys.sh [车机地址] [无人机地址]
#
# ⚠ **私钥只留在本机 ~/.ssh 下, 绝不进仓库。**
#   .gitignore 里已经拦了 *_ed25519 / id_* / *.pem 作为第二道防线, 但
#   第一道防线是这个脚本本身从不往仓库目录写东西。
#
# 与 task-12 §12.6 原文的差异: 改用 Ed25519 而不是 RSA-4096。
#   · 密钥短得多 (68 字节 vs 3243 字节), 握手更快
#   · 无需操心密钥长度是否够用, 也不受 RSA 的 SHA-1 签名弃用影响
#   · OpenSSH 6.5(2014) 起全面支持, 树莓派 OS / Ubuntu 20.04 都没问题
#   RSA 现在唯一的理由是要连很老的设备, 本项目没有这种情况。
# =============================================================================
set -euo pipefail

KEYFILE="${HOME}/.ssh/air_ground_ed25519"
CAR_HOST="${1:-192.168.1.10}"
DRONE_HOST="${2:-192.168.1.20}"

mkdir -p "${HOME}/.ssh"
chmod 700 "${HOME}/.ssh"

if [[ -f "${KEYFILE}" ]]; then
    echo "密钥已存在, 不重复生成: ${KEYFILE}"
else
    # -N "": 无口令。理由是这把钥匙要给开机自启的自动化流程用,
    # 有口令就得引入 ssh-agent, 反而多一层可能在无人值守时卡住的东西。
    # 代价: 开发机被拿到 = 两台 Pi 被拿到。因此这把钥匙**只**用于这两台设备,
    # 不要复用到任何其他地方。
    ssh-keygen -t ed25519 -f "${KEYFILE}" -N "" -C "air-ground-edge-$(date -u +%Y%m%d)"
    echo "已生成: ${KEYFILE}"
fi

chmod 600 "${KEYFILE}"
chmod 644 "${KEYFILE}.pub"

echo ""
echo "=== 公钥 ==="
cat "${KEYFILE}.pub"
echo ""
echo "=== 下一步: 分发到两台树莓派 ==="
echo "  ssh-copy-id -i ${KEYFILE}.pub airground@${CAR_HOST}"
echo "  ssh-copy-id -i ${KEYFILE}.pub airground@${DRONE_HOST}"
echo ""
echo "=== 然后写进 ~/.ssh/config, 省得每次敲 -i ==="
cat <<EOF
  Host car-pi
      HostName ${CAR_HOST}
      User airground
      IdentityFile ${KEYFILE}
      IdentitiesOnly yes

  Host drone-pi
      HostName ${DRONE_HOST}
      User airground
      IdentityFile ${KEYFILE}
      IdentitiesOnly yes
EOF
echo ""
echo "⚠ 确认能用密钥登录之后, 再去 Pi 上启用 ssh/sshd_hardening.conf"
echo "  (那份配置会关掉口令登录 —— 顺序反了就进不去了)。"
