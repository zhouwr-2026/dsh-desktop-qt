#!/usr/bin/env bash
# @author zhouwr
#
# packaging/install.sh::is_official_dsh_web 的单元测试。
#
# install.sh 是用户安装入口；该函数判定 systemd unit 的 ExecStart 是否调用
# 官方 ``dsh web``，决定 install.sh 是否信任一个已有 unit。Regex 写错会导致
# 误信任非官方 unit（潜在安全/合规问题）或误拒官方 unit（用户体验问题）。
#
# 通过 sed 从 install.sh 中提取函数体并 source，避免修改 install.sh 结构
# 引入额外维护面（与现有 tests/test_profile_checker.sh 的 mock systemctl
# fixture 模式思路一致：隔离被测函数，不改源码）。
#
# 触发：依赖审查 "不确定项" 提请注意。

set -euo pipefail

# 解析 install.sh 路径
installer="${1:?install.sh path is required}"

if [[ ! -r "${installer}" ]]; then
    printf 'FAIL: cannot read installer at %s\n' "${installer}" >&2
    exit 1
fi

# 用 sed 提取函数体并 source（同一会话内复用，无中间文件）
# shellcheck disable=SC1090
source <(sed -n '/^is_official_dsh_web()/,/^}/p' "${installer}")

pass=0
fail=0

# 用例：(input, expected_yes_or_no)
# expected "yes" → 函数应返回 0（是官方 dsh web）
# expected "no"  → 函数应返回非 0（不是）
assert_match() {
    local input="$1"
    local expected="$2"
    local actual
    if is_official_dsh_web "${input}"; then
        actual="yes"
    else
        actual="no"
    fi
    if [[ "${actual}" == "${expected}" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: input=%-50s expected=%s got=%s\n' \
            "\"${input}\"" "${expected}" "${actual}" >&2
    fi
}

# ----- 正向用例：官方 dsh web 启动方式 -----
assert_match "/usr/bin/dsh web" yes
assert_match "/usr/bin/dsh web --port 9090" yes
assert_match "dsh web" yes
assert_match "/usr/bin/dsh web --host 127.0.0.1" yes
assert_match "/usr/bin/dsh web --no-open" yes
assert_match "{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; }" yes
assert_match "{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --port 9090 ; }" yes
assert_match "/opt/local/bin/dsh web" yes
assert_match "/usr/bin/dsh    web" yes  # 多个空格

# ----- 负向用例：非官方入口或危险命令 -----
assert_match "" no                       # 空字符串
assert_match "/usr/bin/dsh tui" no       # tui 子命令
assert_match "/usr/bin/dsh --profile web" no          # 仅 --profile web
assert_match "/usr/bin/dsh --profile=web web" no       # profile 形式当前 regex 不支持
assert_match "/usr/bin/dsh --profile web plugin install" no
assert_match "/usr/bin/node server.js" no              # 完全不同 binary
assert_match "/usr/bin/python3 -m http.server" no
assert_match "/usr/bin/dsh-web" no                     # basename 是 dsh-web
assert_match "dsh-web --port 9090" no                   # 同上
assert_match "/usr/bin/dsh webfoo" no                  # web 后不是分隔
assert_match "/usr/bin/dsh-tui web" no                  # basename 是 dsh-tui

# ----- 报告 -----
if (( fail == 0 )); then
    printf 'is_official_dsh_web: %d/%d tests passed\n' "${pass}" "$((pass + fail))"
    exit 0
fi
printf 'is_official_dsh_web: %d/%d tests passed (%d failed)\n' \
    "${pass}" "$((pass + fail))" "${fail}" >&2
exit 1
