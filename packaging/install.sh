#!/usr/bin/env bash
# @author zhouwr
#
# DSH Desktop — Arch Linux + KDE Plasma 6 一键安装脚本。
#
# 幂等：重跑会升级包、刷新 .desktop、重新注册图标。
#
# 流程：
#   1. 用 pacman 装齐运行时依赖（PyQt 已不需要——本版本是 C++/Qt6 原生）
#   2. 用 npm 装 dsh CLI（如已装则跳过）
#   3. cmake 构建并 ninja install 到 /usr
#   4. 注册黑白鲸鱼 SVG + PNG 到 hicolor 图标主题
#   5. 若 dsh-web.service 已存在则启用并启动

set -euo pipefail

# 解析项目根目录
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ---- 颜色日志 ----
log()   { printf '\033[1;36m[install]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[install]\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m[install]\033[0m %s\n' "$*" >&2; exit 1; }

need_root() {
  if [[ $EUID -ne 0 ]]; then
    die "需要 root，请用 sudo $0 重跑"
  fi
}

# ---- 步骤 ----

ensure_pkgs() {
  log "检查系统依赖"
  if ! command -v pacman >/dev/null 2>&1; then
    die "未检测到 pacman（仅支持 Arch Linux 及其衍生版）"
  fi

  local missing=()
  for pkg in qt6-base qt6-webengine qt6-svg qt6-tools libxcb cmake ninja extra-cmake-modules npm polkit procps-ng; do
    if ! pacman -Q "$pkg" >/dev/null 2>&1; then
      missing+=("$pkg")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    log "将安装缺失的软件包: ${missing[*]}"
    pacman -S --needed --noconfirm "${missing[@]}"
  else
    log "Qt6 / cmake / ninja / ECM 已就绪"
  fi

  # dsh 是 npm 全局包，pacman 仓库没有
  if ! command -v npm >/dev/null 2>&1; then
    die "npm 安装后仍不可用，请检查系统 PATH"
  fi
  if ! command -v dsh >/dev/null 2>&1; then
    log "未检测到 dsh CLI；通过 npm 全局安装"
    npm install -g @deepseek-ai/dsh
  else
    log "dsh 已就绪：$(command -v dsh)"
  fi
}

build_cmake() {
  log "cmake 构建"
  cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build "$ROOT/build" --parallel
}

install_artifacts() {
  log "安装到 /usr"
  DESTDIR= cmake --install "$ROOT/build" --prefix /usr
}

install_icons() {
  log "注册黑白鲸鱼图标"
  local icon_dir="/usr/share/icons/hicolor/scalable/apps"
  mkdir -p "$icon_dir"
  cp -f "$ROOT/packaging/dsh-whale-black.svg" "$icon_dir/dsh-whale-black.svg"
  cp -f "$ROOT/packaging/dsh-whale-white.svg" "$icon_dir/dsh-whale-white.svg"

  for size in 22 32 48 64 128 256; do
    local out_dir="/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$out_dir"
    if command -v rsvg-convert >/dev/null 2>&1; then
      rsvg-convert -w "$size" -h "$size" "$ROOT/packaging/dsh-whale-black.svg" \
        > "$out_dir/dsh-whale-black.png"
      rsvg-convert -w "$size" -h "$size" "$ROOT/packaging/dsh-whale-white.svg" \
        > "$out_dir/dsh-whale-white.png"
    fi
  done

  # KWin/Plasma 使用 .desktop 的 dsh-whale 名称：Breeze 下映射黑鲸鱼，
  # Breeze Dark 下映射白鲸鱼。两套主题使用同一名称才能动态切换。
  for size in 16 22 24 32 48 64; do
    install -Dm644 "$ROOT/assets/dsh-whale-black.svg" \
      "/usr/share/icons/breeze/apps/$size/dsh-whale.svg"
    install -Dm644 "$ROOT/assets/dsh-whale-white.svg" \
      "/usr/share/icons/breeze-dark/apps/$size/dsh-whale.svg"
  done

  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
  fi
}

install_desktop_file() {
  log "安装全用户应用菜单与自启动"
  local apps_dir="/usr/share/applications"
  local autostart_dir="/etc/xdg/autostart"
  mkdir -p "$apps_dir"
  mkdir -p "$autostart_dir"
  install -m 0644 "$ROOT/packaging/dsh-desktop.desktop" \
    "$apps_dir/dsh-desktop.desktop"
  install -m 0644 "$ROOT/packaging/dsh-desktop.desktop" \
    "$autostart_dir/dsh-desktop.desktop"
  # 清理旧版本留下的高优先级副本，避免它遮蔽 /usr/share 中的新文件。
  rm -f /usr/local/share/applications/dsh-desktop.desktop
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps_dir" 2>/dev/null || true
  fi
}

configure_dsh_service() {
  log "检查 dsh-web.service（systemd）"
  if systemctl list-unit-files dsh-web.service >/dev/null 2>&1; then
    if ! systemctl is-enabled --quiet dsh-web.service 2>/dev/null; then
      log "启用并启动 dsh-web.service"
      systemctl enable --now dsh-web.service
    else
      log "dsh-web.service 已启用"
    fi
  else
    warn "未发现 dsh-web.service；桌面端会改用 'dsh web' 子进程模式"
  fi
}

configure_theme_export() {
  log "配置跨用户 KDE 主题同步"
  systemctl daemon-reload
  systemctl enable --now dsh-theme-export.path
  systemctl start dsh-theme-export.service
}

uninstall() {
  log "卸载 DSH Desktop"
  # 系统级文件
  rm -f /usr/bin/dsh-desktop
  rm -f /usr/share/applications/dsh-desktop.desktop
  rm -f /usr/local/share/applications/dsh-desktop.desktop
  rm -f /etc/xdg/autostart/dsh-desktop.desktop
  rm -f /usr/share/polkit-1/actions/org.dsh.desktop.policy
  systemctl disable --now dsh-theme-export.path 2>/dev/null || true
  rm -f /usr/lib/systemd/system/dsh-theme-export.path
  rm -f /usr/lib/systemd/system/dsh-theme-export.service
  rm -f /usr/lib/dsh-desktop/dsh-theme-export
  rm -f /run/dsh-desktop/theme
  systemctl daemon-reload
  rm -f /usr/share/icons/hicolor/scalable/apps/dsh-whale-black.svg
  rm -f /usr/share/icons/hicolor/scalable/apps/dsh-whale-white.svg
  for size in 22 32 48 64 128 256; do
    rm -f "/usr/share/icons/hicolor/${size}x${size}/apps/dsh-whale-black.png"
    rm -f "/usr/share/icons/hicolor/${size}x${size}/apps/dsh-whale-white.png"
  done
  for size in 16 22 24 32 48 64; do
    rm -f "/usr/share/icons/breeze/apps/$size/dsh-whale.svg"
    rm -f "/usr/share/icons/breeze-dark/apps/$size/dsh-whale.svg"
  done
  # 单实例 socket 残留
  rm -f /run/user/*/dsh-desktop.sock 2>/dev/null
  log "卸载完成（用户配置 ~/.config/anywhere-labs/ 与下载缓存 ~/.local/share/dsh-desktop/ 已保留）"
}

# ---- 主流程 ----

main() {
  need_root

  ensure_pkgs
  build_cmake
  install_artifacts
  install_icons
  install_desktop_file
  configure_dsh_service
  configure_theme_export

  log "完成。应用菜单与自启动已对本机所有桌面用户启用。"
}

# CLI 解析：第一个参数是 --uninstall 时走卸载
if [[ "${1:-}" == "--uninstall" ]]; then
  shift
  need_root
  uninstall
  exit 0
fi

main "$@"
