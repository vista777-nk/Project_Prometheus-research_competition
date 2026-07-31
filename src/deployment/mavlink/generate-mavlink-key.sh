#!/usr/bin/env bash
# =============================================================================
# generate-mavlink-key.sh — 生成 MAVLink 2 消息签名密钥 (32 字节)
#
# 用法: bash src/deployment/mavlink/generate-mavlink-key.sh [输出路径]
#
# ⚠ **密钥绝不进仓库。**
#   .gitignore 里已经拦了 *_secret.key / *.secret 作为第二道防线,
#   但第一道防线是这个脚本本身: 缺省写到 ~/.config/air-ground/ 下,
#   并且拒绝往 Git 工作区里写 —— 与 generate-ssh-keys.sh 同一个做法。
#
# 与 task-14 §14B.2 原文的差异:
#   原文 `KEYFILE="mavlink_secret.key"` 写在**当前目录**。照抄的话,
#   在 src/deployment/mavlink/ 里跑一次就会往仓库里落一个密钥文件,
#   而这正是 validate.sh §5 会报"疑似私钥进了部署目录"的那种情况。
#   把默认路径挪出仓库, 比事后靠 .gitignore 兜底更靠谱。
# =============================================================================
set -euo pipefail

DEFAULT_DIR="${HOME}/.config/air-ground"
KEYFILE="${1:-${DEFAULT_DIR}/mavlink_secret.key}"

# --- 拒绝往仓库里写 ---------------------------------------------------------
# 用 `git rev-parse --is-inside-work-tree` 判断, **不要**拿 --show-toplevel
# 的输出和 `pwd` 做前缀比较: Git for Windows 那两者返回的路径格式不同
# (`e:/Vista/...` vs `/e/Vista/...`), 比较恒不相等, 守卫恒不生效。
# 第一版就是这么写的, 冒烟测试时它眼睁睁把密钥写进了仓库。
#
# 目标目录还不存在时 (首次生成), 往上找到最近一个存在的祖先目录再问 git,
# 否则 `git -C <不存在的目录>` 直接失败 → 又是一个恒不生效的守卫。
TARGET_DIR="$(dirname "${KEYFILE}")"
PROBE_DIR="${TARGET_DIR}"
while [[ ! -d "${PROBE_DIR}" ]]; do
    PARENT="$(dirname "${PROBE_DIR}")"
    [[ "${PARENT}" == "${PROBE_DIR}" ]] && break
    PROBE_DIR="${PARENT}"
done
if [[ "$(git -C "${PROBE_DIR}" rev-parse --is-inside-work-tree 2>/dev/null)" == "true" ]]; then
    echo "拒绝把密钥写进 Git 工作区: ${TARGET_DIR}" >&2
    echo "密钥不进仓库是本项目的安全红线 (SECURITY.md / ADR-0010 §决策-4)。" >&2
    echo "改用默认路径: ${DEFAULT_DIR}/mavlink_secret.key" >&2
    exit 1
fi

if [[ -f "${KEYFILE}" ]]; then
    echo "密钥已存在, 不重复生成: ${KEYFILE}" >&2
    echo "重新生成会让所有已配置的节点 (Pixhawk / 车机 Pi / 地面站) 同时失联；" >&2
    echo "确实要轮换的话, 先手动删除该文件, 并准备好一次性更新全部节点。" >&2
    exit 1
fi

mkdir -p "$(dirname "${KEYFILE}")"
chmod 700 "$(dirname "${KEYFILE}")"

# --- 生成 32 字节随机密钥 ---------------------------------------------------
# 先生成到变量、校验通过再落盘。反过来做的话, 一个长度不对的密钥会先在磁盘上
# 存在一小会儿 —— 而这段时间里它的权限还没设。
#
# `tr -d ' \r\n'`: MSYS/Git-Bash 的 openssl 输出带 CRLF, 直接落盘会多一个 \r,
# 而那个 \r 会被当成密钥内容的一部分 —— 开发机生成、Linux 上使用时两边不一致,
# 症状是"密钥明明一样却验签失败"。(本脚本的冒烟测试就是这么抓到的。)
if command -v openssl >/dev/null 2>&1; then
    KEY_HEX="$(openssl rand -hex 32 | tr -d ' \r\n')"
elif [[ -r /dev/urandom ]]; then
    KEY_HEX="$(od -An -tx1 -N32 /dev/urandom | tr -d ' \r\n')"
else
    echo "既没有 openssl 也读不到 /dev/urandom, 无法生成密钥" >&2
    exit 1
fi

if [[ ! "${KEY_HEX}" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "生成的密钥不是 64 个十六进制字符 (实际 ${#KEY_HEX} 字符), 已放弃" >&2
    exit 1
fi

# umask 077: 文件从**创建的那一刻**就是 0600, 而不是先 0644 再 chmod。
( umask 077 && printf '%s\n' "${KEY_HEX}" > "${KEYFILE}" )
chmod 600 "${KEYFILE}"

echo "已生成 32 字节 MAVLink 签名密钥: ${KEYFILE}"
echo ""
echo "=== 下一步 ==="
echo "1. 分发到车机 Pi (走 SSH, 不要走聊天软件/邮件):"
echo "     scp ${KEYFILE} car-pi:/tmp/mavlink_secret.key"
echo "     ssh car-pi 'sudo install -o root -g root -m 600 \\"
echo "       /tmp/mavlink_secret.key /etc/air-ground/mavlink_secret.key && \\"
echo "       rm -f /tmp/mavlink_secret.key'"
echo "2. 飞控端通过 QGroundControl 的安全通道设置签名密钥"
echo "   (参数名见 px4-signing.params 顶部的说明 —— 尚未在实机核实)"
echo "3. 备份到离线介质。密钥丢了 = 所有节点要重新配一遍。"
echo "4. 轮换周期建议一季度一次, 且必须同一时间更新全部节点。"
