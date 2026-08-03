#!/usr/bin/env bash
# =============================================================================
# smoke_test_phase1.sh — Phase 1 的毕业证书
#
#   bash scripts/smoke_test_phase1.sh
#
# 退出码: 0 全部通过 (SKIP 不计失败) / 1 有失败项
#
# -----------------------------------------------------------------------------
# 它测什么、不测什么
# -----------------------------------------------------------------------------
# 三层, 从便宜到贵:
#
#   第 1 层 存在性 —— 交付物在不在它该在的位置
#   第 2 层 接口冒烟 —— 语法过不过、YAML 解析得了吗、自测跑不跑得起来
#   第 3 层 CI 归属 —— 每个交付物有没有一个 job 真的在管它
#
# 它**不测**功能正确性: 那是各自的单元测试与 validate.sh 的事,
# 同一个检查不实现两处 (ADR-0008 §决策-3)。
#
# 第 1 层单独存在的理由: 文件被挪走 / 改名 / 忘了 git add 时, 第 2、3 层的
# 报错信息会指向莫名其妙的地方 (比如"import 不到某模块"), 而第 1 层直接
# 说"这个文件不在"。
#
# 查不了的项**明确报 SKIP 并说明原因**, 不静默略过 —— 与 validate.sh 同规矩。
# 一份全是绿勾但其实什么都没查的报告, 比没有报告更糟。
# =============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${WS}" || { echo "无法进入仓库根 ${WS}" >&2; exit 1; }

# 中文 + 符号在 Windows 控制台 (GBK) 上会抛 UnicodeEncodeError, 把检查打断。
export PYTHONIOENCODING=utf-8:replace

PASS=0
FAIL=0
SKIP=0

pass()    { printf '  \033[32m✓\033[0m %s\n' "$1"; PASS=$((PASS + 1)); }
fail()    { printf '  \033[31m✗\033[0m %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip()    { printf '  \033[33m—\033[0m SKIP %s\n' "$1"; SKIP=$((SKIP + 1)); }
section() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

PYTHON=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1; then
        PYTHON="${candidate}"
        break
    fi
done

# --- 第 1 层: 存在性 --------------------------------------------------------
have_file() {
    local description="$1" path="$2"
    if [[ -f "${path}" ]]; then
        pass "${description}"
    else
        fail "${description} —— 找不到 ${path}"
    fi
}

have_dir() {
    local description="$1" path="$2"
    if [[ -d "${path}" ]]; then
        pass "${description}"
    else
        fail "${description} —— 找不到目录 ${path}"
    fi
}

# --- 第 3 层: CI 归属 -------------------------------------------------------
ci_has() {
    local description="$1" needle="$2"
    if grep -q -- "${needle}" .github/workflows/ci.yml 2>/dev/null; then
        pass "${description}"
    else
        fail "${description} —— ci.yml 里找不到 ${needle}"
    fi
}

# 2026-08-03 曾因分支从 feat/task-XX 改成 task-new，而 push.branches 白名单没有
# 同步更新，导致工作流零运行。CI 是所有工作分支的门禁，不能依赖命名约定碰巧匹配。
ci_push_covers_all_branches() {
    local workflow=".github/workflows/ci.yml"
    if ! grep -q '^  push:' "${workflow}" 2>/dev/null; then
        fail "任意分支 push 触发 CI —— ci.yml 没有 push 事件"
        return
    fi

    if awk '
        /^  push:/ { in_push = 1; next }
        /^  [[:alnum:]_]+:/ { in_push = 0 }
        in_push && /^    branches(-ignore)?:/ { restricted = 1 }
        END { exit restricted ? 0 : 1 }
    ' "${workflow}"; then
        fail "任意分支 push 触发 CI —— push 事件仍有 branches 过滤器"
    else
        pass "任意分支 push 均触发 CI"
    fi
}

# 跑一个自身带自测的脚本。退出码 2 视为 SKIP (依赖缺失), 与 validate.sh 一致。
run_selftest() {
    local description="$1"
    shift
    if [[ -z "${PYTHON}" ]]; then
        skip "${description} (找不到 python3)"
        return
    fi
    local log
    log="$(mktemp)"
    "$@" >"${log}" 2>&1
    case $? in
        0) pass "${description}" ;;
        2) skip "${description} ($(tail -1 "${log}" | cut -c1-70))" ;;
        *)
            fail "${description}"
            tail -12 "${log}" | sed 's/^/      /'
            ;;
    esac
    rm -f "${log}"
}

echo "========================================="
echo " Phase 1 Smoke Test"
echo " 仓库根: ${WS}"
echo "========================================="

# =============================================================================
section "1. 固件 (task-10 / task-11)"
# =============================================================================
have_dir  "STM32 麦轮固件目录"          "src/firmware/stm32_mecanum"
have_file "STM32 README"                "src/firmware/stm32_mecanum/README.md"
have_file "STM32 Makefile"              "src/firmware/stm32_mecanum/Makefile"
have_dir  "MSPM0 差速固件目录"          "src/firmware/mspm0_diff"
have_file "MSPM0 README"                "src/firmware/mspm0_diff/README.md"
have_file "MSPM0 Makefile"              "src/firmware/mspm0_diff/Makefile"
have_file "共用帧结构 (ADR-0003)"       "src/firmware/common/protocol_frame.c"
have_file "共用 CRC-16/CCITT"           "src/firmware/common/crc16.c"

# =============================================================================
section "2. 部署 (task-12)"
# =============================================================================
have_file "Dockerfile.edge"             "src/deployment/docker/Dockerfile.edge"
have_file "docker-compose.edge.yml"     "src/deployment/docker/docker-compose.edge.yml"
have_file "systemd 车机单元"            "src/deployment/systemd/air-ground-car-edge.service"
have_file "systemd 无人机单元"          "src/deployment/systemd/air-ground-drone-edge.service"
have_file "SSH 加固配置"                "src/deployment/ssh/sshd_hardening.conf"
have_file "部署静态校验入口"            "src/deployment/validate.sh"

# =============================================================================
section "3. 实机传感器驱动 (task-14 Part A)"
# =============================================================================
have_file "RPLIDAR 驱动"                "src/air_ground_car_bringup/scripts/rplidar_driver.py"
have_file "ICM42688 驱动"               "src/air_ground_car_bringup/scripts/icm42688_driver.py"
have_file "底盘 MCU 协议"              "src/air_ground_car_bringup/scripts/chassis_protocol.py"
have_file "底盘/HC-SR04 生产桥"        "src/air_ground_car_bringup/scripts/chassis_bridge.py"
have_file "OpenMV 桥"                   "src/air_ground_car_bringup/scripts/openmv_bridge.py"
have_file "硬件接口抽象基类"            "src/air_ground_car_bringup/scripts/hardware_interface.py"
# 注意路径: 任务书写的是 test/mock_hardware.py, 实际在 scripts/ ——
# mock 后端是**运行时**要加载的 (backend: mock), 不是测试专用件, 所以它
# 和其它驱动放在一起。按任务书的路径查会得到一个假的红灯。
have_file "mock 硬件后端 (在 scripts/, 非 test/)" \
                                        "src/air_ground_car_bringup/scripts/mock_hardware.py"
have_file "实机传感器参数"              "src/air_ground_car_bringup/config/real_sensors.yaml"
have_dir  "驱动宿主机测试"              "src/air_ground_car_bringup/test/host"

# =============================================================================
section "4. MAVLink 2 签名 (task-14 Part B)"
# =============================================================================
have_file "密钥生成脚本"                "src/deployment/mavlink/generate-mavlink-key.sh"
have_file "签名自测"                    "src/deployment/mavlink/test-mavlink-signing.py"
have_file "PX4 参数模板"                "src/deployment/mavlink/px4-signing.params"
have_file "MAVLink README"              "src/deployment/mavlink/README.md"

# =============================================================================
section "5. 标定工具链 (task-15 Part A)"
# =============================================================================
have_file "采集脚本"                    "src/deployment/calibration/record-calib-bag.sh"
have_file "相机内参标定"                "src/deployment/calibration/calibrate-camera.py"
have_file "IMU 标定"                    "src/deployment/calibration/calibrate-imu.py"
have_file "相机-IMU 外参 (接口)"        "src/deployment/calibration/calibrate-cam-imu-extrinsic.py"
have_file "Kalibr 格式转换"             "src/deployment/calibration/convert-bag-to-kalibr.py"
have_file "标定结果校验"                "src/deployment/calibration/validate-calibration.py"
have_file "标定报告生成"                "src/deployment/calibration/generate-calib-report.py"
have_file "合成样本生成器"              "src/deployment/calibration/test/make_sample_calib_data.py"
have_file "标定流水线测试"              "src/deployment/calibration/test/test_calib_pipeline.py"
have_file "标定 README"                 "src/deployment/calibration/README.md"
have_file "标定归档规范"                "src/deployment/calibration/calibration_db/README.md"

# =============================================================================
section "6. 集成验证 (task-15 Part B)"
# =============================================================================
have_file "串口回路测试 (入口)"         "src/deployment/test/test-serial-loopback.sh"
have_file "串口回路测试 (逻辑)"         "src/deployment/test/test-serial-loopback.py"
have_file "Observation 数据流验证"      "src/deployment/test/test-observation-pipeline.py"

# =============================================================================
section "7. 文档与决策"
# =============================================================================
have_file "接口控制文档"                "project-prometheus-tasks/ICD.md"
have_file "平台架构"                    "project-prometheus-tasks/PLATFORM.md"
have_file "研究哲学"                    "project-prometheus-tasks/RESEARCH_PHILOSOPHY.md"
have_file "ADR-0003 串口协议"           "docs/decisions/ADR-0003.md"
have_file "ADR-0011 标定输出格式"       "docs/decisions/ADR-0011.md"
have_file "ADR-0012 cppcheck 门禁"      "docs/decisions/ADR-0012.md"
have_file "ADR-0013 超声波 MCU 时序"    "docs/decisions/ADR-0013.md"
have_file "ADR-0018 确认 BOM/模块边界"  "docs/decisions/ADR-0018.md"

# =============================================================================
section "8. 接口冒烟 —— 语法与解析"
# =============================================================================
# 存在性只说明"文件在", 说明不了"能用"。下面这一层的成本仍然是秒级,
# 但能抓住"提交了一个语法错误的脚本"这类事故。
if [[ -z "${PYTHON}" ]]; then
    skip "Python 语法检查 (找不到 python3)"
    skip "YAML 解析检查 (找不到 python3)"
else
    py_bad=0
    # 顶层六个硬件资料包是供应商输入，含旧版 MicroPython/生成脚本；不把它们
    # 当成本项目 Python/Bash 源码。项目维护边界与 CI lint job 保持一致。
    mapfile -t project_python < <(find src scripts -name '*.py' -type f | sort)
    while IFS= read -r pyfile; do
        "${PYTHON}" -m py_compile "${pyfile}" 2>/dev/null || {
            fail "语法错误: ${pyfile}"
            py_bad=1
        }
    done < <(printf '%s\n' "${project_python[@]}")
    [[ ${py_bad} -eq 0 ]] && pass "全部 ${#project_python[@]} 个项目 Python 文件语法正确"

    # YAML 用 PyYAML 真解析一遍。yamllint 查的是风格, 查不出"这份 YAML
    # 根本 load 不出来" —— 而 roslaunch / compose 读的是后者。
    if "${PYTHON}" -c "import yaml" 2>/dev/null; then
        yaml_bad=0
        while IFS= read -r yamlfile; do
            "${PYTHON}" -c "import sys, yaml; yaml.safe_load(open(sys.argv[1], encoding='utf-8'))" \
                "${yamlfile}" 2>/dev/null || {
                fail "YAML 解析失败: ${yamlfile}"
                yaml_bad=1
            }
        done < <(git ls-files '*.yaml' '*.yml')
        [[ ${yaml_bad} -eq 0 ]] && \
            pass "全部 $(git ls-files '*.yaml' '*.yml' | wc -l) 个 YAML 可解析"
    else
        skip "YAML 解析检查 (没装 PyYAML)"
    fi

    # Shell 语法
    sh_bad=0
    mapfile -t project_shell < <(find src scripts -name '*.sh' -type f | sort)
    [[ -f setup_all.sh ]] && project_shell+=(setup_all.sh)
    while IFS= read -r shfile; do
        bash -n "${shfile}" 2>/dev/null || { fail "语法错误: ${shfile}"; sh_bad=1; }
    done < <(printf '%s\n' "${project_shell[@]}")
    [[ ${sh_bad} -eq 0 ]] && pass "全部 ${#project_shell[@]} 个项目 shell 脚本语法正确"
fi

# =============================================================================
section "9. 接口冒烟 —— 自测真跑一遍"
# =============================================================================
run_selftest "串口协议自测 (黄金帧 / CRC / 拆帧, 10 条)" \
    "${PYTHON}" src/deployment/test/test-serial-loopback.py --self-test
run_selftest "标定流水线 (合成真值, 16 条)" \
    "${PYTHON}" src/deployment/calibration/test/test_calib_pipeline.py
run_selftest "Observation 数据流 (真预处理器 + 真 World Model, 10 条)" \
    "${PYTHON}" src/deployment/test/test-observation-pipeline.py
run_selftest "MAVLink 2 签名自测 (5 条)" \
    "${PYTHON}" src/deployment/mavlink/test-mavlink-signing.py

# =============================================================================
section "10. CI 归属 —— 每个交付物都有 job 在管"
# =============================================================================
# 这一节防的是"有交付物但没有门禁"。文件在、语法对、CI 里却没人跑它,
# 等于它只在提交那天是对的。
have_file "CI workflow"                 ".github/workflows/ci.yml"
ci_has "ROS 构建与测试 job"             "build-and-test:"
ci_has "STM32 固件 job"                 "build-stm32-firmware:"
ci_has "MSPM0 固件 job"                 "build-mspm0-firmware:"
# 任务书写的 job 名是 build-docker-edge, 仓库里叫 build-edge-image。
# 按任务书的名字查会永远红 —— 而那种红灯看久了就没人看了。
ci_has "边缘镜像构建 job (build-edge-image)" "build-edge-image:"
ci_has "部署静态校验 job"               "validate-deployment:"
ci_has "全仓 lint job"                  "lint-scripts:"
ci_has "Phase 1 冒烟 job"               "smoke_test_phase1:"
ci_push_covers_all_branches

# =============================================================================
section "汇总"
# =============================================================================
printf '  通过 %d · 失败 %d · 跳过 %d\n' "${PASS}" "${FAIL}" "${SKIP}"
if [[ ${SKIP} -gt 0 ]]; then
    printf '  \033[33m注意\033[0m: 有 %d 项在本机跳过, 完整校验以 CI 为准。\n' "${SKIP}"
fi

if [[ ${FAIL} -gt 0 ]]; then
    printf '\n\033[31mPhase 1 冒烟测试失败\033[0m\n'
    exit 1
fi
printf '\n\033[32mPhase 1 冒烟测试全部通过\033[0m\n'
exit 0
