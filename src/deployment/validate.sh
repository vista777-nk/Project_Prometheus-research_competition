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
cd "${REPO_ROOT}"

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
       -o -name '*.template' -o -name '.dockerignore' \) -type f | sort)

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
if command -v systemd-analyze >/dev/null 2>&1; then
    for unit in src/deployment/systemd/*.service src/deployment/systemd/*.timer; do
        out="$(systemd-analyze verify "${unit}" 2>&1 || true)"
        # CI 上 /opt/air-ground 并不存在, "路径找不到"是预期告警, 只看真正的语法错误
        real="$(echo "${out}" | grep -vE 'Failed to (open|resolve)|does not exist|not found|No such file' || true)"
        if [[ -z "${real// }" ]]; then
            pass "$(basename "${unit}")"
        else
            fail "$(basename "${unit}")"
            echo "${real}" | sed 's/^/      /'
        fi
    done
else
    skip "systemd-analyze 不可用 (非 Linux) —— 单元语法由 CI 校验"
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
