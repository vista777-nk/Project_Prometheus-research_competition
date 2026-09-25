#!/usr/bin/env bash
# 把实验室服务器裸机 ROS 栈安装为 systemd 常驻服务。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_REPO="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SERVER_USER="${SUDO_USER:-${USER}}"
REPO_ROOT="${DEFAULT_REPO}"
DRY_RUN=0
ENABLE=0
USER_SERVICE=0

usage() {
    cat <<'EOF'
用法:
  sudo bash src/deployment/install_server.sh [--user USER] [--repo PATH] [--enable]
  bash src/deployment/install_server.sh --user-service [--repo PATH] [--enable]
  bash src/deployment/install_server.sh --dry-run [--user USER] [--repo PATH]

--enable 会立即启用服务器服务与五分钟健康巡检；不指定时只安装文件。
--user-service 安装到当前用户的 systemd；要求 loginctl 显示 Linger=yes。
真实配置保存在 /etc/air-ground/server.env，重复安装不会覆盖。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --user) SERVER_USER="${2:?--user 需要用户名}"; shift 2 ;;
        --repo) REPO_ROOT="${2:?--repo 需要路径}"; shift 2 ;;
        --enable) ENABLE=1; shift ;;
        --user-service) USER_SERVICE=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf '未知参数: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

REPO_ROOT="$(cd "${REPO_ROOT}" && pwd)"
[[ -r "${REPO_ROOT}/scripts/setup_runtime.sh" ]] || {
    printf '不是可运行的项目仓库: %s\n' "${REPO_ROOT}" >&2
    exit 1
}
[[ -r "${REPO_ROOT}/devel/setup.bash" ]] || {
    printf '工作空间尚未构建；先以普通用户执行 make build\n' >&2
    exit 1
}
id "${SERVER_USER}" >/dev/null 2>&1 || {
    printf '用户不存在: %s\n' "${SERVER_USER}" >&2
    exit 1
}
SERVER_GROUP="$(id -gn "${SERVER_USER}")"

run() {
    if [[ ${DRY_RUN} -eq 1 ]]; then
        printf '  [dry-run]'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}

if [[ ${USER_SERVICE} -eq 1 ]]; then
    current_user="$(id -un)"
    [[ "${SERVER_USER}" == "${current_user}" ]] || {
        printf -- '--user-service 只能安装给当前用户 %s\n' "${current_user}" >&2
        exit 1
    }
    user_home="${HOME}"
    user_lib="${user_home}/.local/lib/air-ground"
    user_units="${user_home}/.config/systemd/user"
    user_config="${user_home}/.config/air-ground/server.env"

    printf '[1/5] 安装用户服务脚本与健康检查\n'
    run install -d -m 0755 "${user_lib}" "${user_units}"
    run install -m 0755 "${SCRIPT_DIR}/server/launch_lab_server.sh" \
        "${user_lib}/launch_lab_server.sh"
    run install -m 0755 "${SCRIPT_DIR}/server/check_lab_server.py" \
        "${user_lib}/check_lab_server.py"

    printf '[2/5] 安装用户 systemd 单元\n'
    for unit in \
        air-ground-lab-server.service \
        air-ground-lab-server-healthcheck.service \
        air-ground-lab-server-healthcheck.timer; do
        run install -m 0644 "${SCRIPT_DIR}/systemd/user/${unit}" "${user_units}/${unit}"
    done

    printf '[3/5] 准备用户配置与 data2 日志目录\n'
    run install -d -m 0755 "$(dirname "${user_config}")"
    run install -d -m 0750 /data2/air-ground-server /data2/air-ground-server/ros-log
    if [[ ! -e "${user_config}" ]]; then
        run install -m 0600 "${SCRIPT_DIR}/server/server.env.example" "${user_config}"
        run sed -i "s|^AIR_GROUND_REPO=.*|AIR_GROUND_REPO=${REPO_ROOT}|" "${user_config}"
        if [[ ${DRY_RUN} -eq 1 ]]; then
            printf '  将创建 %s（ROS/TCP 默认仅回环）\n' "${user_config}"
        else
            printf '  已创建 %s（ROS/TCP 默认仅回环）\n' "${user_config}"
        fi
    else
        printf '  保留现有 %s，不覆盖\n' "${user_config}"
    fi

    printf '[4/5] 重载并校验用户 systemd\n'
    run systemctl --user daemon-reload
    run systemd-analyze --user verify \
        "${user_units}/air-ground-lab-server.service" \
        "${user_units}/air-ground-lab-server-healthcheck.service" \
        "${user_units}/air-ground-lab-server-healthcheck.timer"

    if [[ ${ENABLE} -eq 1 ]]; then
        printf '[5/5] 启用用户服务与健康巡检\n'
        run systemctl --user enable --now air-ground-lab-server.service
        run systemctl --user enable --now air-ground-lab-server-healthcheck.timer
        run systemctl --user --no-pager --full status air-ground-lab-server.service
    else
        printf '[5/5] 已安装但未启用\n'
        printf '  systemctl --user enable --now air-ground-lab-server.service\n'
        printf '  systemctl --user enable --now air-ground-lab-server-healthcheck.timer\n'
    fi
    exit 0
fi

if [[ ${DRY_RUN} -eq 0 && ${EUID} -ne 0 ]]; then
    printf '需要 root 权限写 /etc、/usr/local 和 systemd\n' >&2
    exit 1
fi

printf '[1/5] 安装服务脚本与健康检查\n'
run install -d -m 0755 /usr/local/lib/air-ground
run install -m 0755 "${SCRIPT_DIR}/server/launch_lab_server.sh" \
    /usr/local/lib/air-ground/launch_lab_server.sh
run install -m 0755 "${SCRIPT_DIR}/server/check_lab_server.py" \
    /usr/local/lib/air-ground/check_lab_server.py

printf '[2/5] 安装 systemd 单元\n'
for unit in \
    air-ground-lab-server@.service \
    air-ground-lab-server-healthcheck@.service \
    air-ground-lab-server-healthcheck@.timer; do
    run install -m 0644 "${SCRIPT_DIR}/systemd/${unit}" "/etc/systemd/system/${unit}"
done

printf '[3/5] 准备配置与 data2 日志目录\n'
run install -d -m 0755 /etc/air-ground
run install -d -m 0750 -o "${SERVER_USER}" -g "${SERVER_GROUP}" \
    /data2/air-ground-server /data2/air-ground-server/ros-log
if [[ ! -e /etc/air-ground/server.env ]]; then
    run install -m 0640 -o root -g "${SERVER_GROUP}" \
        "${SCRIPT_DIR}/server/server.env.example" /etc/air-ground/server.env
    run sed -i "s|^AIR_GROUND_REPO=.*|AIR_GROUND_REPO=${REPO_ROOT}|" \
        /etc/air-ground/server.env
    if [[ ${DRY_RUN} -eq 1 ]]; then
        printf '  将创建 /etc/air-ground/server.env（ROS/TCP 默认仅回环）\n'
    else
        printf '  已创建 /etc/air-ground/server.env（ROS/TCP 默认仅回环）\n'
    fi
else
    printf '  保留现有 /etc/air-ground/server.env，不覆盖\n'
fi

printf '[4/5] 重载并校验 systemd\n'
run systemctl daemon-reload
run systemd-analyze verify \
    /etc/systemd/system/air-ground-lab-server@.service \
    /etc/systemd/system/air-ground-lab-server-healthcheck@.service \
    /etc/systemd/system/air-ground-lab-server-healthcheck@.timer

instance="$(systemd-escape "${SERVER_USER}")"
if [[ ${ENABLE} -eq 1 ]]; then
    printf '[5/5] 启用服务与健康巡检\n'
    run systemctl enable --now "air-ground-lab-server@${instance}.service"
    run systemctl enable --now "air-ground-lab-server-healthcheck@${instance}.timer"
    run systemctl --no-pager --full status "air-ground-lab-server@${instance}.service"
else
    printf '[5/5] 已安装但未启用\n'
    printf '  核对 /etc/air-ground/server.env 后执行:\n'
    printf '  sudo systemctl enable --now air-ground-lab-server@%s.service\n' "${instance}"
    printf '  sudo systemctl enable --now air-ground-lab-server-healthcheck@%s.timer\n' \
        "${instance}"
fi
