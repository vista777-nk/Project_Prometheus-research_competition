#!/usr/bin/env bash
# =============================================================================
# test-serial-loopback.sh — 串口回路测试入口 (树莓派 ↔ STM32/MSPM0)
#
#   ./test-serial-loopback.sh                      # 默认 /dev/ttyAMA0
#   ./test-serial-loopback.sh /dev/ttyUSB0
#   ./test-serial-loopback.sh --self-test          # 不碰硬件, CI 用
#   CHASSIS=mecanum ./test-serial-loopback.sh /dev/ttyAMA0
#
# 逻辑全在同名的 .py 里。这里只做三件事: 找 python、转发参数、把退出码传出去。
#
# 为什么不像 task-15 原文那样把 Python 塞进 heredoc:
#   `bash -n` 只检查 shell 语法, `py_compile` 看不见 heredoc 里的东西,
#   于是那段 Python **不被任何门禁覆盖**, 而它是串口协议的第三份实现。
#   拆成独立文件之后, 它进了 validate.sh §2 的 py_compile 与自测,
#   还能在没有硬件的机器上跑 10 条协议断言。
#   (同样的理由让 validate_consistency.py 早先从 validate.sh 的 heredoc 里搬了出来。)
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_ENTRY="${SCRIPT_DIR}/test-serial-loopback.py"

[[ -f "${PYTHON_ENTRY}" ]] || {
    echo "错误: 找不到 ${PYTHON_ENTRY}" >&2
    exit 2
}

PYTHON=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1; then
        PYTHON="${candidate}"
        break
    fi
done
[[ -n "${PYTHON}" ]] || { echo "错误: 找不到 python3" >&2; exit 2; }

# 中文 + 符号输出在 Windows 控制台 (GBK) 上会抛 UnicodeEncodeError,
# 把测试整个打断。与 validate.sh 同样的处理。
export PYTHONIOENCODING=utf-8:replace

# 第一个参数如果不是以 - 开头, 当作串口设备路径 (保持任务书里的用法)。
if [[ $# -gt 0 && "$1" != -* ]]; then
    PORT="$1"
    shift
    exec "${PYTHON}" "${PYTHON_ENTRY}" --port "${PORT}" "$@"
fi

exec "${PYTHON}" "${PYTHON_ENTRY}" "$@"
