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
#   4. 注册黑白鲸鱼 SVG 到 hicolor / Breeze 图标主题（仅 SVG，无量位图 PNG；
#      canonical 单一来源为 assets/dsh-whale-{black,white}.svg，与 CMake 一致）
#   5. 只读探测 dsh-web.service（system scope，user 可用时一并检查）：
#      仅当 LoadState=loaded 且 ExecStart 为官方 `dsh web` 时接受该单元；
#      active 视为复用，inactive/failed 视为存在但未启动（由桌面端在运行时征询同意），
#      外部/非官单元视为不受管理且不改动，缺失视为未配置（桌面端走已验证的备用/自建路径）。
#      本脚本绝不启停/启用/禁用任何已存在的 DSH 服务。

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

run_build_command() {
  # 防御性检查：参数必须由本脚本内部提供，避免未来重构时被外部 CLI
  # 参数填满，导致在 SUDO_USER 下执行非预期命令。
  # (变更理由: 安全审查 L-4)
  if [[ "$#" -eq 0 ]]; then
    die "run_build_command 未传参数"
  fi
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
    sudo -u "${SUDO_USER}" -- "$@"
  else
    "$@"
  fi
}

# ---- 步骤 ----

ensure_pkgs() {
  log "检查系统依赖"
  if ! command -v pacman >/dev/null 2>&1; then
    die "未检测到 pacman（仅支持 Arch Linux 及其衍生版）"
  fi

  local missing=()
  for pkg in qt6-base qt6-webengine qt6-svg qt6-tools libxcb cmake ninja pkgconf npm polkit procps-ng knotifications; do
    if ! pacman -Q "$pkg" >/dev/null 2>&1; then
      missing+=("$pkg")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    log "将安装缺失的软件包: ${missing[*]}"
    pacman -S --needed --noconfirm "${missing[@]}"
  else
    log "Qt6 / cmake / ninja 已就绪"
  fi

  # dsh 是 npm 全局包，pacman 仓库没有
  if ! command -v npm >/dev/null 2>&1; then
    die "npm 安装后仍不可用，请检查系统 PATH"
  fi
  if ! command -v dsh >/dev/null 2>&1; then
    log "未检测到 dsh CLI；通过 npm 全局安装"
    # 关闭 npm install 脚本以最小化供应链投毒面：
    # @deepseek-ai/dsh 是深名空间官方包，但 .npmrc / preinstall 仍是常规
    # 攻击面，安装期我们只关心二进制落盘，不执行任意 JS。
    # (变更理由: 依赖审查建议 #6)
    npm config set ignore-scripts true
    npm install -g @deepseek-ai/dsh
  else
    log "dsh 已就绪：$(command -v dsh)"
  fi
}

build_cmake() {
  log "release preset 构建"
  (cd "$ROOT" && run_build_command cmake --preset release)
  run_build_command cmake --build "$ROOT/build/release" --parallel
}

install_artifacts() {
  log "安装到 /usr"
  DESTDIR= cmake --install "$ROOT/build/release" --prefix /usr
}

install_icons() {
  log "注册黑白鲸鱼 SVG 图标（含 KDE symbolic 自配色）"
  # 仅装 hicolor 4 个文件（普通色版 + symbolic + 两份归档）；breeze / breeze-dark
  # 不装——KDE 主题继承链 Inherits=hicolor 自动传播 fallback，避免每个主题每个
  # size 都铺一份（之前 27 个文件是过度设计）。Kickoff 命中 hicolor 普通色版，
  # Tasks plasmoid 命中 hicolor symbolic 自动按 colorscheme 配色。
  local icon_dir="/usr/share/icons/hicolor/scalable/apps"
  mkdir -p "$icon_dir"
  install -Dm644 "$ROOT/assets/dsh-whale-black.svg" "$icon_dir/dsh-whale.svg"
  install -Dm644 "$ROOT/assets/dsh-whale-black.svg" "$icon_dir/dsh-whale-black.svg"
  install -Dm644 "$ROOT/assets/dsh-whale-white.svg" "$icon_dir/dsh-whale-white.svg"

  # 派生 KDE symbolic SVG（fill=#000000 → currentColor），源资产保持 black +
  # white 两份不重复；symbolic 是 build / install 阶段派生品。
  local symbolic_svg
  symbolic_svg="$(mktemp --suffix=.svg)"
  sed 's/fill="#000000"/fill="currentColor"/g' \
    "$ROOT/assets/dsh-whale-black.svg" > "$symbolic_svg"
  install -Dm644 "$symbolic_svg" "$icon_dir/dsh-whale-symbolic.svg"
  rm -f "$symbolic_svg"

  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
  fi
}

install_license() {
  log "安装许可证"
  install -Dm644 "$ROOT/LICENSE" \
    "/usr/share/licenses/dsh-desktop/LICENSE"
}

install_system_files() {
  log "安装 systemd 单元与桌面自启动条目"
  bash "$ROOT/packaging/post-install.sh" --prefix /usr
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

is_official_dsh_web() {
  # 判定 systemd 单元的 ExecStart 是否调用官方 `dsh web`（dsh CLI 的 web 子命令）。
  # 入参：systemctl show --property=ExecStart --value 的输出。
  local execstart="$1"
  [[ -n "$execstart" ]] || return 1
  # 匹配形如 "<可选项>/dsh web <…>" 或 "dsh web <…>" 的命令行；
  # 命令名 basename 必须恰好是 dsh，且后紧跟子命令 web（含 dsh 的其他名字不匹配）。
  if [[ "$execstart" =~ (^|[[:space:]])([^[:space:]]+/)?dsh[[:space:]]+web([[:space:]]|$) ]]; then
    return 0
  fi
  return 1
}

configure_dsh_service() {
  log "检查 dsh-web.service（systemd，只读探测，不改动任何已存在的 DSH 服务）"

  # 需要探测的 systemd scope：system 始终检查；user 会话可用时一并检查。
  local scopes=("--system")
  if systemctl --user show >/dev/null 2>&1; then
    scopes+=("--user")
  fi

  local scope found_any=0
  for scope in "${scopes[@]}"; do
    local loadstate activestate execstart
    loadstate="$(systemctl "$scope" show --property=LoadState --value dsh-web.service 2>/dev/null || true)"
    # 该 scope 下未配置（LoadState=not-found 或查询失败）则跳过。
    if [[ -z "$loadstate" || "$loadstate" == "not-found" ]]; then
      continue
    fi
    found_any=1

    activestate="$(systemctl "$scope" show --property=ActiveState --value dsh-web.service 2>/dev/null || true)"
    execstart="$(systemctl "$scope" show --property=ExecStart --value dsh-web.service 2>/dev/null || true)"

    # 仅接受 LoadState=loaded 且 ExecStart 为官方 `dsh web` 的单元。
    if [[ "$loadstate" == "loaded" ]] && is_official_dsh_web "$execstart"; then
      case "$activestate" in
        active|activating|reloading)
          log "检测到官方 dsh-web.service（$scope）正在运行：直接复用，不再重复启动"
          ;;
        *)
          warn "检测到官方 dsh-web.service（$scope）存在但当前未运行（$activestate）；本脚本不擅自启动，DSH Desktop 将在运行时询问你的同意"
          ;;
      esac
    else
      warn "检测到 dsh-web.service（$scope）存在，但其配置并非官方 'dsh web'（LoadState=$loadstate）；视为外部/不受管单元，本脚本不会改动它"
    fi
  done

  if (( found_any == 0 )); then
    warn "系统未配置 dsh-web.service（systemd）；桌面端将使用其已验证的备用/自建路径"
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

  # 委托给原生卸载器：它只按 UninstallPlan 决策，用 QFile/QDir 显式删除桌面端
  # 产物；后台 unit/数据仅在计划允许时移除，官方服务与用户数据默认保留。
  # 交给原生卸载器显示复选框；默认不勾选后台服务，勾选后还需二次确认。
  local uninstaller="/usr/bin/dsh-desktop-uninstaller"
  if [[ ! -x "$uninstaller" ]]; then
    die "找不到卸载器 $uninstaller，请先重新安装或检查安装路径"
  fi
  "$uninstaller" --prefix /usr

  log "卸载完成（用户配置 ~/.config/anywhere-labs/、下载缓存 ~/.local/share/dsh-desktop/ 等已保留；后台 DSH 服务默认保留）"
}

# ---- 主流程 ----

main() {
  need_root

  ensure_pkgs
  build_cmake
  install_artifacts
  install_license
  install_icons
  install_system_files
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
