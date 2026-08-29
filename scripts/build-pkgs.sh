#!/bin/bash
# DSH Desktop 多格式打包脚本
# 用法: sudo bash scripts/build-pkgs.sh

set -euo pipefail

VERSION="0.1.0"
PACKAGE="dsh-desktop"

log() { echo "[build] $*"; }
warn() { echo "[build] WARN: $*" >&2; }
die()  { echo "[build] ERROR: $*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || die "需要 root 权限运行此脚本"

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
    command -v dpkg-deb &>/dev/null || { warn "dpkg-deb 不可用，跳过 DEB"; return 0; }
    cmake --preset release
    cpack --config build/release/CPackConfig.cmake -G DEB -B dist
    ls -lh dist/*.deb 2>/dev/null && log "DEB 包已生成" || warn "DEB 包生成失败"
}

# 构建 RPM 包
build_rpm() {
    log "构建 RPM 包..."
    command -v rpmbuild &>/dev/null || { warn "rpmbuild 不可用，跳过 RPM"; return 0; }
    cmake --preset release
    cpack --config build/release/CPackConfig.cmake -G RPM -B dist
    ls -lh dist/*.rpm 2>/dev/null && log "RPM 包已生成" || warn "RPM 包生成失败"
}

# 构建 AppImage
build_appimage() {
    log "构建 AppImage..."
    command -v appimagetool &>/dev/null || { warn "appimagetool 不可用，跳过 AppImage"; return 0; }
    
    local appdir="dist/AppDir"
    rm -rf "$appdir"
    mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications"
    
    cp "build/release/dsh-desktop" "$appdir/usr/bin/"
    cp "build/release/dsh-desktop-updater" "$appdir/usr/bin/"
    cp "build/release/dsh-desktop-uninstaller" "$appdir/usr/bin/"
    cp "packaging/dsh-desktop.desktop" "$appdir/usr/share/applications/"
    
    cat > "$appdir/AppRun" << 'APPRUN'
#!/bin/sh
exec "$(dirname "$0")/usr/bin/dsh-desktop" "$@"
APPRUN
    chmod +x "$appdir/AppRun"
    
    appimagetool "$appdir" "dist/${PACKAGE}-${VERSION}-x86_64.AppImage"
    ls -lh dist/*.AppImage 2>/dev/null && log "AppImage 已生成" || warn "AppImage 生成失败"
}

# 主流程
main() {
    log "开始构建多格式安装包..."
    mkdir -p dist
    
    cmake --preset release
    cmake --build build/release --parallel
    ctest --test-dir build/release --output-on-failure
    bash scripts/package-linux.sh
    
    build_deb
    build_rpm
    build_appimage
    
    log ""
    log "构建完成！产物位于 dist/:"
    ls -lh dist/*.tar.gz dist/*.deb dist/*.rpm dist/*.AppImage 2>/dev/null || true
}

main "$@"
