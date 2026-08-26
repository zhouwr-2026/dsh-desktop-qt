#!/usr/bin/env bash
# @author zhouwr
#
# DSH Desktop 端到端冒烟测试（C++ 实现）

set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build/dev"
SELF_TEST_LOG="$(mktemp "${TMPDIR:-/tmp}/dsh-self-test.XXXXXX.log")"
cleanup() {
  rm -f "$SELF_TEST_LOG"
}
trap cleanup EXIT

log()   { printf '\033[1;36m[smoke]\033[0m %s\n' "$*"; }
fail()  { printf '\033[1;31m[smoke]\033[0m %s\n' "$*" >&2; exit 1; }

log "1/4 dev preset 构建"
cmake --preset dev >/dev/null || fail "dev preset 配置失败"
cmake --build "$BUILD_DIR" --parallel >/dev/null || fail "cmake 构建失败"
test -x "$BUILD_DIR/dsh-desktop" || fail "$BUILD_DIR/dsh-desktop 不存在"
log "  构建产物: $(ls -la "$BUILD_DIR/dsh-desktop" | awk '{print $5,$NF}')"

theme_fixture="$(mktemp "${TMPDIR:-/tmp}/dsh-kdeglobals.XXXXXX")"
theme_marker="$(mktemp "${TMPDIR:-/tmp}/dsh-theme.XXXXXX")"
printf '[KDE]\nLookAndFeelPackage=org.kde.breezedark.desktop\n' > "$theme_fixture"
DSH_KDEGLOBALS_SOURCE="$theme_fixture" DSH_THEME_OUTPUT="$theme_marker" \
DSH_THEME_REQUIRE_ROOT_PLASMA=0 bash ./packaging/dsh-theme-export
[[ "$(<"$theme_marker")" == "dark" ]] || fail "跨用户 KDE 主题导出失败"
rm -f "$theme_fixture" "$theme_marker"

log "2/4 完整单元测试"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "单元测试失败"

log "3/4 后端可达性"
"$BUILD_DIR/dsh-desktop" --smoke || fail "后端不可达"

log "4/4 自检（完整启动 + 干净退出）"
env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen \
  XDG_SESSION_TYPE=offscreen timeout 10 "$BUILD_DIR/dsh-desktop" --self-test \
  > "$SELF_TEST_LOG" 2>&1 || fail "self-test 启动失败"
python3 - "$SELF_TEST_LOG" << 'PY' || fail "self-test 报告断言失败"
import json, re, sys
log_text = open(sys.argv[1]).read()
m = re.search(r'DSH_DESKTOP_SELF_TEST_BEGIN\n(.*?)\nDSH_DESKTOP_SELF_TEST_END', log_text, re.S)
assert m, '未找到 self-test 报告'
data = json.loads(m.group(1))
assert data['tray_visible'], '托盘不可见'
assert data['window_visible'], '窗口不可见'
assert data['tray_theme'] == data['theme'], '托盘主题与全局主题不一致'
assert data['tray_tooltip'] == 'DSH Desktop', '托盘提示只能显示应用名称'
assert data['logo_theme'] == data['theme'], '统一 Logo 主题与全局主题不一致'
assert data['window_logo_theme'] == data['theme'], '窗口 Logo 主题与全局主题不一致'
assert data['clipboard_write_enabled'], 'WebEngine 剪贴板写入未启用'
for expected in ('显示桌面', '隐藏桌面', '检查更新', '重启', '退出'):
    assert expected in data['menu_items'], f'菜单缺少 {expected}'
assert not data['update_action_visible'], '更新按钮应默认隐藏'
print(f'  托盘可见: {data["tray_visible"]}')
print(f'  窗口可见: {data["window_visible"]}')
print(f'  窗口 URL: {data["window_url"]}')
print(f'  主题: {data["theme"]}')
print(f'  菜单项: {data["menu_items"]}')
PY

log "全部通过 ✓"
