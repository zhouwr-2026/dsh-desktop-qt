#!/bin/bash
# DSH Desktop AppImage 构建脚本
# 用法: bash scripts/build-appimage.sh
#
# 此脚本会在有网络的环境下自动下载工具并构建 AppImage

set -euo pipefail

VERSION="0.1.0"
PACKAGE="dsh-desktop"

log() { echo "[appimage] $*"; }
warn() { echo "[appimage] WARN: $*" >&2; }
die()  { echo "[appimage] ERROR: $*" >&2; exit 1; }

# 检查 appimagetool
ensure_appimagetool() {
    if command -v appimagetool &>/dev/null; then
        log "appimagetool 已就绪: $(which appimagetool)"
        return 0
    fi
    
    # 尝试从缓存加载
    if [[ -f "/tmp/appimagetool.AppImage" ]]; then
        log "使用缓存的 appimagetool"
        chmod +x /tmp/appimagetool.AppImage
        return 0
    fi
    
    log "正在下载 appimagetool..."
    
    # 尝试多个下载源
    local urls=(
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
        "https://ghproxy.com/https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
        "https://mirror.ghproxy.com/https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    )
    
    for url in "${urls[@]}"; do
        log "  尝试: $url"
        if curl -sL "$url" -o /tmp/appimagetool.AppImage --max-time 60 2>/dev/null; then
            if [[ -s /tmp/appimagetool.AppImage ]] && [[ $(stat -c%s /tmp/appimagetool.AppImage) -gt 100000 ]]; then
                chmod +x /tmp/appimagetool.AppImage
                log "  ✓ 下载成功"
                return 0
            fi
        fi
    done
    
    warn "所有下载源均失败"
    warn ""
    warn "请手动下载 appimagetool:"
    warn "  wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    warn "  chmod +x appimagetool-x86_64.AppImage"
    warn "  sudo mv appimagetool-x86_64.AppImage /usr/local/bin/appimagetool"
    warn ""
    warn "然后重新运行此脚本"
    return 1
}

# 准备 AppDir
prepare_appdir() {
    log "准备 AppDir..."
    rm -rf dist/AppDir
    mkdir -p dist/AppDir/usr/bin dist/AppDir/usr/share/applications
    
    # 复制二进制
    for bin in dsh-desktop dsh-desktop-updater dsh-desktop-uninstaller; do
        if [[ -f "build/release/$bin" ]]; then
            cp "build/release/$bin" dist/AppDir/usr/bin/
            log "  ✓ $bin"
        fi
    done
    
    # 复制桌面文件
    if [[ -f "packaging/dsh-desktop.desktop" ]]; then
        cp packaging/dsh-desktop.desktop dist/AppDir/usr/share/applications/
        log "  ✓ dsh-desktop.desktop"
    fi
    
    # 创建 AppRun
    cat > dist/AppDir/AppRun << 'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
exec "$HERE/usr/bin/dsh-desktop" "$@"
APPRUN
    chmod +x dist/AppDir/AppRun
    log "  ✓ AppRun"
}

# 构建 AppImage
build_appimage() {
    log "构建 AppImage..."
    
    local tool="/tmp/appimagetool.AppImage"
    command -v appimagetool &>/dev/null && tool="$(which appimagetool)"
    
    if [[ ! -x "$tool" ]]; then
        die "无法找到 appimagetool"
    fi
    
    "$tool" dist/AppDir "dist/${PACKAGE}-${VERSION}-x86_64.AppImage"
    
    if [[ -f "dist/${PACKAGE}-${VERSION}-x86_64.AppImage" ]]; then
        log "✓ AppImage 已生成: dist/${PACKAGE}-${VERSION}-x86_64.AppImage"
        log "  大小: $(ls -lh "dist/${PACKAGE}-${VERSION}-x86_64.AppImage" | awk '{print $5}')"
    else
        die "AppImage 构建失败"
    fi
}

# 主流程
main() {
    log "开始构建 AppImage..."
    log "版本: $VERSION"
    
    # 确保已构建
    if [[ ! -f "build/release/dsh-desktop" ]]; then
        log "构建项目..."
        cmake --preset release
        cmake --build build/release --parallel
    fi
    
    # 确保工具
    ensure_appimagetool || die "无法获取 appimagetool"
    
    # 准备和构建
    prepare_appdir
    build_appimage
    
    # 生成校验和
    cd dist
    sha256sum "${PACKAGE}-${VERSION}-x86_64.AppImage" > SHA256SUMS
    cd ..
    
    log ""
    log "完成！产物: dist/${PACKAGE}-${VERSION}-x86_64.AppImage"
}

main "$@"
