# DSH Desktop AppImage 构建指南

由于当前构建环境网络限制，AppImage 需要在具备网络访问的目标系统上构建。

---

## 前置条件

- 稳定的互联网连接
- `appimagetool` 或 `linuxdeploy` 工具
- 至少 2GB 可用磁盘空间（用于下载依赖）

---

## 方法 1: 一键构建（推荐）

在项目根目录运行：

```bash
sudo bash scripts/build-pkgs.sh
```

此脚本会自动检测工具并构建所有可用格式，包括 AppImage。

---

## 方法 2: 手动使用 appimagetool

### 1. 安装 appimagetool

```bash
# 方式 A: 使用包管理器（Arch Linux）
yay -S appimage

# 方式 B: 手动下载
wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage
sudo mv appimagetool-x86_64.AppImage /usr/local/bin/appimagetool
```

### 2. 准备 AppDir

```bash
# 清理并创建目录
rm -rf dist/AppDir
mkdir -p dist/AppDir/usr/bin dist/AppDir/usr/share/applications

# 复制二进制
cp build/release/dsh-desktop dist/AppDir/usr/bin/
cp build/release/dsh-desktop-updater dist/AppDir/usr/bin/
cp build/release/dsh-desktop-uninstaller dist/AppDir/usr/bin/

# 复制桌面文件
cp packaging/dsh-desktop.desktop dist/AppDir/usr/share/applications/
```

### 3. 创建 AppRun

```bash
cat > dist/AppDir/AppRun << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
exec "$HERE/usr/bin/dsh-desktop" "$@"
