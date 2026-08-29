#!/bin/bash
# DSH Desktop AppImage 构建脚本
# 用法: bash scripts/build-appimage.sh

set -euo pipefail

VERSION="0.1.0"
PACKAGE="dsh-desktop"

log() { echo "[appimage] $*"; }

# 检查 appimagetool
if ! command -v appimagetool &>/dev/null; then
    log "正在下载 appimagetool..."
    
    # 尝试从多个源下载
    for url in \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        "https://github-releases.githubusercontent.com/63334885/2a3f5c80-05d6-11eb-887a-f179b2c283f3"
    do
        if curl -sL "$url" -o /tmp/appimagetool.AppImage --max-time 30 2>/dev/null; then
            if [[ -s /tmp/appimagetool.AppImage ]]; then
                log "下载成功"
                break
            fi
        fi
    done
    
    chmod +x /tmp/appimagetool.AppImage || die "无法获取 appimagetool"
else
    log "appimagetool 已就绪"
fi

# 准备 AppDir
log "准备 AppDir..."
rm -rf dist/AppDir
mkdir -p dist/AppDir/usr/bin dist/AppDir/usr/share/applications

# 复制文件
cp build/release/dsh-desktop dist/AppDir/usr/bin/
cp build/release/dsh-desktop-updater dist/AppDir/usr/bin/
cp build/release/dsh-desktop-uninstaller dist/AppDir/usr/bin/
cp packaging/dsh-desktop.desktop dist/AppDir/usr/share/applications/

# 创建 AppRun
cat > dist/AppDir/AppRun << 'APPRUN'
#!/bin/sh
exec "$(dirname "$0")/usr/bin/dsh-desktop" "$@"
APPRUN
chmod +x dist/AppDir/AppRun

# 构建 AppImage
log "构建 AppImage..."
/tmp/appimagetool.AppImage dist/AppDir "dist/${PACKAGE}-${VERSION}-x86_64.AppImage"

log "完成！产物: dist/${PACKAGE}-${VERSION}-x86_64.AppImage"
