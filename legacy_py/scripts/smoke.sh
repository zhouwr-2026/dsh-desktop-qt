#!/usr/bin/env bash
# End-to-end smoke test for dsh-desktop.
#
# Validates:
#   - All Python modules import.
#   - Backend can reach the dsh web service.
#   - Update checker can talk to the npm registry.
#   - Theme watcher reports a value.
#   - Icons module loads assets.
#   - In offscreen mode the QApplication initializes and exits cleanly.

set -euo pipefail

cd "$(dirname "$0")/.."

log() { printf '\033[1;36m[smoke]\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m[smoke]\033[0m %s\n' "$*" >&2; exit 1; }

log "1/6 模块导入"
QT_QPA_PLATFORM=offscreen python3 -c "
from dsh_desktop import backend, downloads, icons, notifications, theme, updater, app
print('  ok')
" || fail "imports failed"

log "2/6 后端探测"
QT_QPA_PLATFORM=offscreen python3 -c "
from dsh_desktop.backend import DshBackend
b = DshBackend()
s = b.status()
print(f'  backend running={s.running} mode={s.mode.value} url={s.url}')
assert s.running, 'dsh web not reachable'
print('  ok')
" || fail "backend probe failed"

log "3/6 更新检查器"
QT_QPA_PLATFORM=offscreen python3 -c "
from dsh_desktop.updater import check_for_update, read_local_version
v = read_local_version()
print(f'  本地版本: {v}')
status = check_for_update(timeout_s=4)
print(f'  npm latest: {status.latest}  可更新: {status.update_available}')
assert status.latest, '无法从 npm registry 获取最新版本'
print('  ok')
" || fail "updater failed"

log "4/6 主题监听器"
QT_QPA_PLATFORM=offscreen python3 -c "
from PyQt6.QtWidgets import QApplication
qa = QApplication([])
from dsh_desktop.theme import ThemeWatcher
tw = ThemeWatcher()
print(f'  主题: {tw.current}')
assert tw.current in ('light', 'dark'), f'unexpected theme: {tw.current}'
print('  ok')
" || fail "theme watcher failed"

log "5/6 图标资源"
QT_QPA_PLATFORM=offscreen python3 -c "
from PyQt6.QtWidgets import QApplication
qa = QApplication([])
from dsh_desktop.icons import icon_for_scheme, pixmap_for_scheme, _assets_root
print(f'  资源根: {_assets_root()}')
pix = pixmap_for_scheme('light', 128)
assert not pix.isNull(), '亮色图标缺失'
pix = pixmap_for_scheme('dark', 128)
assert not pix.isNull(), '暗色图标缺失'
ic = icon_for_scheme('light')
assert not ic.isNull(), 'light QIcon 缺失'
print('  ok')
" || fail "icons failed"

log "6/6 桌面端应用冒烟（offscreen 启动 + 干净退出）"
QT_QPA_PLATFORM=offscreen DSH_DESKTOP_DEBUG=1 timeout 10 python3 -m dsh_desktop --self-test \
  > /tmp/dsh-self-test.log 2>&1 \
  || fail "self-test 启动失败"
python3 << 'PY' || fail "self-test 报告断言失败"
import json, re
log_text = open('/tmp/dsh-self-test.log').read()
m = re.search(r'DSH_DESKTOP_SELF_TEST_BEGIN\n(.*?)\nDSH_DESKTOP_SELF_TEST_END', log_text, re.S)
assert m, '未找到 self-test 报告'
data = json.loads(m.group(1))
assert data['tray_visible'], '托盘不可见'
assert data['window_visible'], '窗口不可见'
assert '显示桌面' in data['menu_items'], '菜单缺显示桌面'
assert '退出' in data['menu_items'], '菜单缺退出'
assert not data['update_action_visible'], '更新按钮应默认隐藏'
print('  托盘可见:', data['tray_visible'])
print('  窗口可见:', data['window_visible'])
print('  窗口标题:', data['window_title'])
print('  窗口 URL:', data['window_url'])
print('  菜单项:', data['menu_items'])
print('  主题:', data['theme'])
print('  后端:', data['backend'])
print('  ok')
PY

log "全部通过 ✓"
