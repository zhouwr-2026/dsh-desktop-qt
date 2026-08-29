#!/bin/bash
# DSH Desktop 多格式打包脚本
# 用法: sudo bash scripts/build-pkgs.sh
#
# 本脚本用于在具备相应工具的发行版上生成 .deb/.rpm/AppImage 包

set -euo pipefail

VERSION="0.1.0"
PACKAGE="dsh-desktop"
ARCH="$(uname -m | sed 's/x86_64/amd64/' | sed 's/aarch64/arm64/')"

log() { echo "[build] $*"; }
warn() { echo "[build] WARN: $*" >&2; }
die()  { echo "[build] ERROR: $*" >&2; exit 1; }

# 检查 root 权限
[[ $EUID -eq 0 ]] || die "需要 root 权限运行此脚本"

# 检查源归档
SOURCE_ARCHIVE="/tmp/${PACKAGE}-${VERSION}-source.tar.gz"
[[ -f "$SOURCE_ARCHIVE" ]] || die "找不到源码归档: $SOURCE_ARCHIVE"
[[ -f "dist/${PACKAGE}-${VERSION}-Linux.tar.gz" ]] || die "找不到 Linux 安装包: dist/${PACKAGE}-${VERSION}-Linux.tar.gz"

# 安装构建依赖
install_build_deps() {
    if command -v pacman &>/dev/null; then
        log "安装 Arch 构建依赖"
        sudo pacman -S --needed --noconfirm base-devel cmake ninja pkgconf \
            qt6-base qt6-svg qt6-webengine qt6-tools libxcb \
            systemd polkit procps-ng npm
    elif command -v apt-get &>/dev/null; then
        log "安装 Debian/Ubuntu 构建依赖"
        sudo apt-get update
        sudo apt-get install -y build-essential cmake ninja-build pkg-config \
            debhelper dpkg-dev rpm fakeroot \
            libxcb1-dev qt6-base-dev qt6-svg-dev qt6-webengine-dev \
            qt6-tools-dev qt6-tools-dev-tools systemd policykit-1 procps npm
    elif command -v dnf &>/dev/null; then
        log "安装 Fedora/RHEL 构建依赖"
        sudo dnf install -y cmake ninja-build pkg-config \
            cmake-helpers qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebengine-devel \
            qt6-qttools-devel systemd-rpm-macros libxcb-devel \
            polkit nodejs npm
    fi
}

# 构建 DEB 包
build_deb() {
    log "构建 DEB 包..."
    
    # 检查工具
    command -v dpkg-deb &>/dev/null || { warn "dpkg-deb 不可用，跳过 DEB"; return 0; }
    
    # 使用 cpack 生成 DEB
    cmake --preset release
    cpack --config build/release/CPackConfig.cmake -G DEB -B dist
    
    if [[ -f "dist/${PACKAGE}_${VERSION}_amd64.deb" ]]; then
        log "DEB 包已生成: dist/${PACKAGE}_${VERSION}_amd64.deb"
    else
        warn "DEB 包生成失败"
    fi
}

# 构建 RPM 包
build_rpm() {
    log "构建 RPM 包..."
    
    # 检查工具
    command -v rpmbuild &>/dev/null || { warn "rpmbuild 不可用，跳过 RPM"; return 0; }
    
    # 使用 cpack 生成 RPM
    cmake --preset release
    cpack --config build/release/CPackConfig.cmake -G RPM -B dist
    
    if [[ -f "dist/${PACKAGE}-${VERSION}-1.x86_64.rpm" ]]; then
        log "RPM 包已生成: dist/${PACKAGE}-${VERSION}-1.x86_64.rpm"
    else
        warn "RPM 包生成失败"
    fi
}

# 构建 AppImage
build_appimage() {
    log "构建 AppImage..."
    
    # 检查工具
    command -v appimagetool &>/dev/null || { warn "appimagetool 不可用，跳过 AppImage"; return 0; }
    
    # 准备 AppDir
    local appdir="dist/AppDir"
    rm -rf "$appdir"
    mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/scalable/apps"
    
    # 复制文件
    cp "build/release/dsh-desktop" "$appdir/usr/bin/"
    cp "build/release/dsh-desktop-updater" "$appdir/usr/bin/"
    cp "build/release/dsh-desktop-uninstaller" "$appdir/usr/bin/"
    cp "packaging/dsh-desktop.desktop" "$appdir/usr/share/applications/"
    cp "assets/dsh-whale.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/"
    
    # 创建 AppRun
    cat > "$appdir/AppRun" << 'APPRUN'
#!/bin/sh
exec "$(dirname "$0")/usr/bin/dsh-desktop" "$@"
APPRUN
    chmod +x "$appdir/AppRun"
    
    # 生成 AppImage
    appimagetool "$appdir" "dist/${PACKAGE}-${VERSION}-${ARCH}.AppImage"
    
    if [[ -f "dist/${PACKAGE}-${VERSION}-${ARCH}.AppImage" ]]; then
        log "AppImage 已生成: dist/${PACKAGE}-${VERSION}-${ARCH}.AppImage"
    else
        warn "AppImage 生成失败"
    fi
}

# 生成 yay 安装命令
generate_yay_command() {
    cat << EOF

## Arch Linux (yay) 安装命令

\`\`\`bash
# 方法 1: 从 AUR 安装（推荐）
yay -Syu dsh-desktop

# 方法 2: 手动构建
git clone https://github.com/zhouwr-2026/dsh-desktop-qt.git
cd dsh-desktop-qt/packaging
makepkg -si

# 方法 3: 从 Release 下载
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-Linux.tar.gz
tar xzf dsh-desktop-0.1.0-Linux.tar.gz
cd dsh-desktop-0.1.0
sudo ./packaging/install.sh
\`\`\`
