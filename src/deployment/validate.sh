#!/usr/bin/env bash
# =============================================================================
# validate.sh — 部署配置的静态校验入口 (本地与 CI 跑同一份)
#
#   bash src/deployment/validate.sh
#
# 设计意图: 部署配置的失败方式是"到了现场才发现"。能在提交前静态查出来的
# 东西必须全查掉, 而且本地和 CI 必须是同一份脚本 —— 两份必然漂移。
#
# 查不了的项**明确报 SKIP 并说明原因**, 不静默略过。
# 一份全是绿勾但其实什么都没查的报告, 比没有报告更糟。
#
#   检查项                     Git-Bash/Windows   Linux CI
#   ------------------------   ----------------   --------
#   shell 语法 (bash -n)              ✓              ✓
#   Python 语法 + 单元测试            ✓              ✓
#   串口协议自测 (task-15)            ✓              ✓
#   标定流水线 (task-15)          需 numpy/cv2       ✓
#   compose 结构 + 话题一致性         ✓              ✓
#   行尾必须是 LF                     ✓              ✓
#   密钥泄漏扫描                      ✓              ✓
#   systemd-analyze verify           SKIP            ✓
#   docker build                     SKIP        另一个 job
#
# 退出码: 0 全部通过 (SKIP 不计失败) / 1 有失败项
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# cd 失败必须立刻退出: 后面全部的 find / git / 相对路径都建立在"当前目录是仓库根"
# 这个前提上。cd 没成功而继续跑, 会在错误的目录里查出一份"全过"的报告 ——
# 又是一个假绿灯, 而且比不查更有欺骗性。
cd "${REPO_ROOT}" || { echo "无法进入仓库根 ${REPO_ROOT}" >&2; exit 1; }

# 内嵌/外部 Python 都会打印带符号的中文。Windows 控制台默认 GBK,
# 直接写会抛 UnicodeEncodeError 把检查整个打断 —— 校验脚本自己不能有这种脆弱点。
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

# =============================================================================
section "1. Shell 脚本语法"
# =============================================================================
while IFS= read -r script; do
    if bash -n "${script}" 2>/dev/null; then
        pass "${script#src/deployment/}"
    else
        fail "${script#src/deployment/}"
        bash -n "${script}" 2>&1 | sed 's/^/      /'
    fi
done < <(find src/deployment -name '*.sh' -type f | sort)

# =============================================================================
section "2. Python 语法与单元测试"
# =============================================================================
if [[ -z "${PYTHON}" ]]; then
    skip "找不到 python3 —— Python 相关检查全部跳过"
else
    while IFS= read -r pyfile; do
        if "${PYTHON}" -m py_compile "${pyfile}" 2>/dev/null; then
            pass "${pyfile#src/deployment/}"
        else
            fail "${pyfile#src/deployment/}"
            "${PYTHON}" -m py_compile "${pyfile}" 2>&1 | sed 's/^/      /'
        fi
    done < <(find src/deployment -name '*.py' -type f | sort)

    if "${PYTHON}" src/deployment/healthcheck/test_healthcheck.py >/tmp/agtest.log 2>&1; then
        pass "healthcheck 单元测试 — $(grep -oE 'Ran [0-9]+ tests' /tmp/agtest.log || echo '?')"
    else
        fail "healthcheck 单元测试"
        tail -25 /tmp/agtest.log | sed 's/^/      /'
    fi

    if "${PYTHON}" src/deployment/test/test_entrypoint_modes.py \
            >/tmp/agentrypoint.log 2>&1; then
        pass "entrypoint 模式测试 — $(grep -oE 'Ran [0-9]+ tests' /tmp/agentrypoint.log || echo '?')"
    else
        fail "entrypoint 模式测试"
        tail -40 /tmp/agentrypoint.log | sed 's/^/      /'
    fi

    if "${PYTHON}" src/deployment/test/test_server_deployment.py \
            >/tmp/agserverdeployment.log 2>&1; then
        pass "服务器常驻部署测试 — $(grep -oE 'Ran [0-9]+ tests' /tmp/agserverdeployment.log || echo '?')"
    else
        fail "服务器常驻部署测试"
        tail -40 /tmp/agserverdeployment.log | sed 's/^/      /'
    fi

    if "${PYTHON}" src/deployment/test/test_pi_preflight.py \
            >/tmp/agpipreflight.log 2>&1; then
        pass "树莓派主机预检测试 — $(grep -oE 'Ran [0-9]+ tests' /tmp/agpipreflight.log || echo '?')"
    else
        fail "树莓派主机预检测试"
        tail -40 /tmp/agpipreflight.log | sed 's/^/      /'
    fi

    # MAVLink 2 签名自测 (task-14)。退出码 2 = 没装 pymavlink, 报 SKIP 不算失败 ——
    # 与上面 validate_consistency.py 的降级约定一致。CI 上会真跑
    # (validate-deployment job 装了 pymavlink), 所以这里的 SKIP 不会掩盖问题。
    "${PYTHON}" src/deployment/mavlink/test-mavlink-signing.py >/tmp/agmavlink.log 2>&1
    case $? in
        0) pass "MAVLink 2 签名自测 (签名开销/验签/错误密钥/重放/未签名)" ;;
        2) skip "MAVLink 签名自测 (未安装 pymavlink)" ;;
        *)
            fail "MAVLink 2 签名自测"
            tail -25 /tmp/agmavlink.log | sed 's/^/      /'
            ;;
    esac

    # 串口协议自测 (task-15)。**没有降级路径** —— 它只用标准库,
    # 任何能跑 python3 的机器上都必须过。黄金帧与固件的
    # test_protocol.c::test_pong_golden_frame 同源: 协议有三份独立实现
    # (STM32 / MSPM0 / 本文件), 任何一端改了字节序或 CRC 参数, 两边总有一处红。
    if "${PYTHON}" src/deployment/test/test-serial-loopback.py --self-test \
            >/tmp/agserial.log 2>&1; then
        pass "串口协议自测 (黄金帧/CRC/拆帧/重同步 10 条)"
    else
        fail "串口协议自测"
        tail -25 /tmp/agserial.log | sed 's/^/      /'
    fi

    # 标定流水线 (task-15)。退出码 2 = 没有 numpy/OpenCV, 报 SKIP。
    # CI 的 validate-deployment job 装了这两个 (版本约束 -c requirements.txt),
    # 所以那边一定真跑。
    "${PYTHON}" src/deployment/calibration/test/test_calib_pipeline.py \
        >/tmp/agcalib.log 2>&1
    case $? in
        0) pass "标定流水线 (合成真值 → 内参/IMU/转换/报告 16 条)" ;;
        2) skip "标定流水线 (未安装 numpy 或 OpenCV)" ;;
        *)
            fail "标定流水线"
            tail -25 /tmp/agcalib.log | sed 's/^/      /'
            ;;
    esac
fi

# =============================================================================
section "3. compose 结构与话题名一致性"
# =============================================================================
# 逻辑在 validate_consistency.py 里 —— 那部分判断最多, 塞进 heredoc 就既
# 无法单独调试也不会被上面的语法检查覆盖。
if [[ -z "${PYTHON}" ]]; then
    skip "需要 python"
else
    consistency_out="$("${PYTHON}" src/deployment/validate_consistency.py 2>&1)"
    consistency_status=$?
    printf '%s\n' "${consistency_out}"
    case ${consistency_status} in
        0) pass "compose 结构 + 话题一致性" ;;
        2) skip "compose 结构 (无 pyyaml, 已降级为结构检查并通过)" ;;
        *) fail "compose 结构 / 话题一致性" ;;
    esac
fi

# =============================================================================
section "4. 行尾必须是 LF"
# =============================================================================
# 这不是洁癖。CRLF 会让下面几类文件在 Linux 上**直接失效**:
#   systemd  "Type=simple\r" → 单元加载失败
#   sshd     配置行尾带 \r  → sshd 拒绝启动 → 把自己锁在门外
#   udev     规则静默不匹配, 符号链接不出现
# .gitattributes 已声明 eol=lf, 这里验证它真的生效了 ——
# 2026-07-25 的编码事故就是"规则写了但没在内容之前提交"。
mapfile -t CRLF_TARGETS < <(find src/deployment \
    \( -name '*.sh' -o -name '*.service' -o -name '*.timer' \
       -o -name '*.rules' -o -name '*.conf' -o -name 'Dockerfile*' \
       -o -name '*.template' -o -name '.dockerignore' -o -name '*.params' \) -type f | sort)

# 判断方式说明 (这一条是写完之后做负向测试才发现的):
# **不要**用 `grep $'\r'`。Git for Windows 附带的 MSYS grep 在读入时会静默
# 剥掉 CR, 于是在 Windows 上这个检查永远返回"没有 CRLF" —— 塞一个 CRLF 文件
# 进去照样通过。而 Linux CI 上 grep 行为正常, 结果就是本地一直绿、
# 真出问题时本地反而发现不了, 属于最坏的一类假绿灯。
# 改用"剥掉 CR 前后字节数是否变化"判断, 与 grep 的文本模式无关。
crlf_bad=0
for f in "${CRLF_TARGETS[@]}"; do
    # 检查 git index 里的内容 —— 那才是 Linux 侧会拿到的。
    # 未入库的文件退回检查工作区。
    if git ls-files --error-unmatch "${f}" >/dev/null 2>&1; then
        raw_bytes=$(git show ":${f}" 2>/dev/null | wc -c)
        stripped_bytes=$(git show ":${f}" 2>/dev/null | tr -d '\r' | wc -c)
    else
        raw_bytes=$(wc -c < "${f}")
        stripped_bytes=$(tr -d '\r' < "${f}" | wc -c)
    fi
    if [[ "${raw_bytes}" -ne "${stripped_bytes}" ]]; then
        fail "${f#src/deployment/} 含 CRLF ($(( raw_bytes - stripped_bytes )) 个 CR)"
        crlf_bad=1
    fi
done
if [[ ${crlf_bad} -eq 0 ]]; then
    pass "全部 ${#CRLF_TARGETS[@]} 个文件均为 LF"
fi

# =============================================================================
section "5. 密钥泄漏扫描"
# =============================================================================
leaked=0
while IFS= read -r f; do
    [[ -n "${f}" ]] || continue
    fail "疑似私钥进了部署目录: ${f}"
    leaked=1
done < <(find src/deployment -type f \
    \( -name '*.pem' -o -name '*_rsa' -o -name '*_ed25519' \
       -o -name 'id_*' -o -name 'authorized_keys' -o -name '*.key' \) \
    ! -name '*.pub' 2>/dev/null)

if grep -rlE 'BEGIN (RSA |OPENSSH |EC |DSA )?PRIVATE KEY' src/deployment 2>/dev/null | grep -q .; then
    fail "有文件包含 PRIVATE KEY 块"
    leaked=1
fi

# 明文口令。排除 .example / 文档 —— 那些地方写的是占位符。
if grep -rnE '^[[:space:]]*(password|passwd|psk|secret)[[:space:]]*=[[:space:]]*[^$#[:space:]@]' \
        src/deployment 2>/dev/null | grep -vE '\.example|\.md:|README' | grep -q .; then
    fail "有配置文件写了明文口令 (应改用 .env 或模板占位符)"
    leaked=1
fi

if [[ ${leaked} -eq 0 ]]; then
    pass "未发现私钥或明文口令"
fi

# =============================================================================
section "6. systemd 单元"
# =============================================================================
# --- 6a. 段归属自查 (不依赖 systemd, 本机也跑) -------------------------------
# systemd 对"写错段的键"的处理是**打一行日志然后静默忽略** —— 单元照常启动,
# 配置却没生效。这类错误只有 systemd-analyze verify 会说, 而它只在 Linux 上有,
# 于是本机永远看不到。下面这几条把已经踩过的坑固定成本机也能跑的检查。
#
# StartLimitIntervalSec/StartLimitBurst: v230 起从 [Service] 移到了 [Unit],
# 旧名 StartLimitInterval 留了兼容别名、新名没有。(CI 抓出来过一次。)
misplaced=0
while IFS= read -r unit; do
    bad="$(awk '
        /^\[/       { sect = $0 }
        /^StartLimit(IntervalSec|Burst)=/ { if (sect != "[Unit]") print FILENAME ":" FNR ": " $0 " —— 应在 [Unit] 段" }
    ' "${unit}")"
    if [[ -n "${bad}" ]]; then
        misplaced=1
        echo "${bad}" | sed 's|^.*/systemd/|      |'
    fi
done < <(find src/deployment/systemd -name '*.service' -type f | sort)
if [[ ${misplaced} -eq 0 ]]; then
    pass "StartLimit* 均在 [Unit] 段"
else
    fail "有 StartLimit* 写在了 [Service] 段 (systemd 会静默忽略)"
fi

# --- 6b. systemd-analyze verify ----------------------------------------------
if command -v systemd-analyze >/dev/null 2>&1; then
    # 一次性校验全部单元, **不按单元归类**。
    #
    # 为什么不逐个报: systemd-analyze verify 会连带加载依赖单元, 并把它们的问题
    # 一起打印。healthcheck.service 有 After=air-ground-*-edge.service, 于是校验
    # 它时会把两个 edge 单元的错误也带上 —— 同一个错误在四份报告里各出现一次,
    # 行号还指向别的文件。(CI 首次运行的输出正是如此: 四个单元全红, 真实错误两处。)
    #
    # 试过按文件名过滤来归类, 但那样"不带文件名的错误"会被静默丢掉 —— 又一个假绿灯。
    # 宁可一次报全: systemd 的报错本来就自带 路径:行号。
    mapfile -t units < <(find src/deployment/systemd \
        \( -name '*.service' -o -name '*.timer' \) -type f | sort)
    # 只加载项目单元与发行版系统单元，不扫描 /etc/systemd/system 里的宿主机服务。
    # 实验室服务器上曾因此把 snapd 的版本告警算成项目失败。
    unit_path="${REPO_ROOT}/src/deployment/systemd:/lib/systemd/system:/usr/lib/systemd/system"
    out="$(SYSTEMD_UNIT_PATH="${unit_path}" systemd-analyze verify "${units[@]}" 2>&1 || true)"
    # CI 上 /opt/air-ground 并不存在, "路径找不到"是预期告警, 只看真正的语法错误
    # 受限容器里 systemd 还会尝试建立 Varlink/socket 或 SO_PASSCRED；这是分析器环境
    # 的限制，不是 unit 语法。SO_PASSCRED 被拒时分析器可能在读 unit 前就退出，必须
    # 报 SKIP，不能过滤后写成 PASS。过滤条件保留具体前缀，不能吞掉通用 parse 错误。
    analyzer_blocked=0
    if echo "${out}" | grep -q '^SO_PASSCRED failed: Operation not permitted$'; then
        analyzer_blocked=1
    fi
    real="$(echo "${out}" \
        | grep -vE 'Failed to (open|resolve)|does not exist|not found|No such file' \
        | grep -vE 'Failed to (bind to varlink socket|set up Varlink server)|connect\(\) failed' \
        | grep -v '^SO_PASSCRED failed: Operation not permitted$' \
        | grep -vE 'Command (/opt/air-ground|/usr/local/lib/air-ground|/home/[^/]+/\.local/lib/air-ground)/.* is not executable' \
        | grep -v '^[[:space:]]*$' || true)"
    if [[ -n "${real}" ]]; then
        fail "systemd 单元有语法问题"
        echo "${real}" | sed 's|^.*/src/deployment/systemd/|      |' | sort -u
    elif [[ ${analyzer_blocked} -eq 1 ]]; then
        skip "systemd-analyze 被当前沙箱拒绝 SO_PASSCRED；完整单元语法由 CI 校验"
    else
        pass "${#units[@]} 个 systemd 单元语法正确"
    fi
else
    skip "systemd-analyze 不可用 (非 Linux) —— 完整单元语法由 CI 校验"
fi

# =============================================================================
section "7. 交叉引用"
# =============================================================================
# systemd unit 里写死的路径必须真的对应到仓库里的文件。
# 改名/挪目录时最容易漏的就是这一处, 而症状要到开机才暴露。
xref_bad=0
while IFS= read -r ref; do
    [[ -n "${ref}" ]] || continue
    target="${ref#/opt/air-ground/}"
    if [[ ! -f "src/deployment/${target}" ]]; then
        fail "systemd 引用了仓库里不存在的文件: ${ref}"
        xref_bad=1
    fi
done < <(grep -rhoE '/opt/air-ground/(scripts|healthcheck|docker)/[A-Za-z0-9._-]+' \
    src/deployment/systemd/ 2>/dev/null | sort -u)

# 实验室服务器单元安装到 /usr/local/lib，路径必须与 server/ 源文件一一对应。
while IFS= read -r ref; do
    [[ -n "${ref}" ]] || continue
    filename="${ref##*/}"
    if [[ ! -f "src/deployment/server/${filename}" ]]; then
        fail "服务器 systemd 引用了仓库里不存在的文件: ${ref}"
        xref_bad=1
    fi
done < <(grep -rhoE '/usr/local/lib/air-ground/[A-Za-z0-9._-]+' \
    src/deployment/systemd/ 2>/dev/null | sort -u)

while IFS= read -r ref; do
    [[ -n "${ref}" ]] || continue
    filename="${ref##*/}"
    if [[ ! -f "src/deployment/server/${filename}" ]]; then
        fail "用户服务器 systemd 引用了仓库里不存在的文件: ${ref}"
        xref_bad=1
    fi
done < <(grep -rhoE '%h/\.local/lib/air-ground/[A-Za-z0-9._-]+' \
    src/deployment/systemd/user/ 2>/dev/null | sort -u)

# entrypoint 传的 roslaunch 参数名必须与 launch 文件声明的一致。
# roslaunch 对未声明参数是**硬错误** (RLException: unused args),
# task-12 §12.2 原文写的 chassis:= 与 car_edge.launch 的 default_chassis 对不上。
if grep -q 'default_chassis:=' src/deployment/docker/entrypoint.sh; then
    if grep -q 'name="default_chassis"' src/air_ground_car_bringup/launch/car_edge.launch; then
        pass "entrypoint 的 roslaunch 参数名与 car_edge.launch 一致"
    else
        fail "car_edge.launch 无 default_chassis 参数 → entrypoint 会报 unused args"
        xref_bad=1
    fi
else
    fail "entrypoint 未传 default_chassis"
    xref_bad=1
fi

# 实机模式必须显式把 real 后端传给 launch；mock 只能由显式模式选择，且必须
# 写入降级状态。少任一条都会复活「EDGE_MODE=real 跑假数据但显示健康」的事故。
if grep -q 'SENSOR_BACKEND="real"' src/deployment/docker/entrypoint.sh \
   && grep -q 'backend:=${SENSOR_BACKEND}' src/deployment/docker/entrypoint.sh \
   && grep -q 'EDGE_MODE=mock' src/deployment/docker/entrypoint.sh; then
    pass "entrypoint 的 real/mock/sim 模式语义显式且失败关闭"
else
    fail "entrypoint 未锁住 real→real / mock→DEGRADED 契约"
    xref_bad=1
fi

# 降级状态文件是一份**跨进程契约**: 容器内的 entrypoint.sh 写, 容器外的
# agcheck.py 读。两边分属不同语言、不同镜像、不同生命周期, 改一边不会有
# 任何编译期报错 —— 表现是健康检查永远报"未降级", 而实际正在降级运行。
# 与固件那边用黄金帧锁串口协议是同一个用意。
state_bad=0
for key in AIR_GROUND_DEGRADED AIR_GROUND_DEGRADED_REASON; do
    in_writer=$(grep -c "${key}" src/deployment/docker/entrypoint.sh || true)
    in_reader=$(grep -c "${key}" src/deployment/healthcheck/agcheck.py || true)
    if [[ "${in_writer}" -eq 0 || "${in_reader}" -eq 0 ]]; then
        fail "降级状态键 ${key} 只在一端出现 (entrypoint=${in_writer}, agcheck=${in_reader})"
        state_bad=1
    fi
done
# 文件名也得一致, 否则一端写 A 一端读 B, 同样静默失效
state_name=$(grep -oE 'STATE_FILE_NAME = "[^"]+"' src/deployment/healthcheck/agcheck.py | cut -d'"' -f2)
if [[ -z "${state_name}" ]]; then
    fail "agcheck.py 里找不到 STATE_FILE_NAME"
    state_bad=1
elif ! grep -q "${state_name}" src/deployment/docker/entrypoint.sh; then
    fail "agcheck.py 的 STATE_FILE_NAME (${state_name}) 与 entrypoint.sh 写的不一致"
    state_bad=1
fi
# 退出码 4 必须被 systemd 单元和告警脚本都认识
if ! grep -q 'SuccessExitStatus=4' src/deployment/systemd/air-ground-healthcheck.service; then
    fail "healthcheck.service 缺 SuccessExitStatus=4 → 降级会被长期标成 failed"
    state_bad=1
fi
if ! grep -qE '^\s*4\)' src/deployment/healthcheck/alert.sh; then
    fail "alert.sh 没有处理退出码 4 (降级)"
    state_bad=1
fi
if [[ ${state_bad} -eq 0 ]]; then
    pass "降级状态契约两端一致 (entrypoint ↔ agcheck ↔ systemd ↔ alert)"
fi

# 镜像里的 Python 依赖版本必须与仓库根 requirements.txt 一致。
# Dockerfile 的注释一直写着"与 requirements.txt 对齐", 但上界 <3.0.0 只写在
# requirements.txt 里、没同步过来 —— 一句声称对齐的注释, 和实际不对齐。
# 这类漂移不会有任何报错, 只会让"仿真跑的版本"和"实机跑的版本"悄悄分家。
#
# 只比对**两边都有**的包: 镜像刻意不装 opencv/Pillow (容器里用 apt 的
# ros-noetic-cv-bridge 与 python3-numpy, 在 arm64 上 pip 编译 opencv 代价太大)。
# CI 的 ROS job 已改为直接 pip install -r requirements.txt, 不在这条检查范围内。
pin_bad=0
# numpy/opencv 现在走 apt；这里只比对 Dockerfile 与根 requirements 都由 pip
# 安装的协议/串口依赖。
for pkg in pymavlink pyserial; do
    req_pin=$(grep -oE "^${pkg}[><=,.0-9]+" requirements.txt | head -1)
    img_pin=$(grep -oE "${pkg}[><=,.0-9]+" src/deployment/docker/Dockerfile.edge | head -1)
    if [[ -z "${req_pin}" || -z "${img_pin}" ]]; then
        fail "${pkg}: requirements.txt=[${req_pin}] Dockerfile.edge=[${img_pin}] 至少一处找不到"
        pin_bad=1
    elif [[ "${req_pin}" != "${img_pin}" ]]; then
        fail "${pkg} 版本约束不一致: requirements.txt=${req_pin} vs Dockerfile.edge=${img_pin}"
        pin_bad=1
    fi
done
if [[ ${pin_bad} -eq 0 ]]; then
    pass "镜像 Python 依赖版本与 requirements.txt 一致"
fi

if [[ ${xref_bad} -eq 0 ]]; then
    pass "systemd 引用的文件均存在"
fi

# =============================================================================
section "汇总"
# =============================================================================
printf '  通过 %d · 失败 %d · 跳过 %d\n' "${PASS}" "${FAIL}" "${SKIP}"
if [[ ${SKIP} -gt 0 ]]; then
    printf '  \033[33m注意\033[0m: 有 %d 项在本机跳过, 完整校验以 CI 为准。\n' "${SKIP}"
fi

if [[ ${FAIL} -gt 0 ]]; then
    printf '\n\033[31m校验失败\033[0m\n'
    exit 1
fi
printf '\n\033[32m全部通过\033[0m\n'
exit 0
