# DSH Desktop 多格式安装包安装指南

本文档介绍如何安装 DSH Desktop v0.1.0 的各种格式安装包。

---

## 📦 可用安装包格式

| 格式 | 文件名 | 适用发行版 |
| --- | --- | --- |
| **通用 tar.gz** | `dsh-desktop-0.1.0-Linux.tar.gz` | 所有 systemd + Qt 6.6 Linux |
| **DEB** | `dsh-desktop-0.1.0-Linux.deb` | Debian 13+, Ubuntu 24.04+ |
| **RPM** | `dsh-desktop-0.1.0-Linux.rpm` | Fedora 39+, RHEL 9+, openSUSE Tumbleweed |
| **源码归档** | `dsh-desktop-0.1.0-source.tar.gz` | 源码编译 / AUR |
| **AppImage** | `dsh-desktop-0.1.0-x86_64.AppImage` | ⚠️ 暂不可用，需本地构建 |

---

## 🐧 Arch Linux / Manjaro / EndeavourOS

### 方法 1: yay 安装（推荐）

```bash
yay -Syu dsh-desktop
```

### 方法 2: 从 Release 下载源码包

```bash
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-source.tar.gz
tar xzf dsh-desktop-0.1.0-source.tar.gz
cd dsh-desktop-0.1.0
cd packaging
makepkg -si
```

---

## 📦 Debian / Ubuntu

### 方法 1: DEB 包安装（推荐）

```bash
# 下载 DEB 包
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-Linux.deb

# 安装
sudo apt install ./dsh-desktop-0.1.0-Linux.deb
```

### 方法 2: 源码编译

```bash
# 安装构建依赖
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libxcb1-dev qt6-base-dev qt6-svg-dev qt6-webengine-dev \
  qt6-tools-dev qt6-tools-dev-tools systemd policykit-1 procps npm

# 编译安装
cmake --preset release
cmake --build build/release --parallel
sudo cmake --install build/release
```

---

## 📦 Fedora / RHEL / openSUSE

### 方法 1: RPM 包安装（推荐）

```bash
# 下载 RPM 包
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-Linux.rpm

# 安装（Fedora/openSUSE）
sudo dnf install ./dsh-desktop-0.1.0-Linux.rpm

# 或 RHEL/CentOS
# sudo rpm -i ./dsh-desktop-0.1.0-Linux.rpm
```

### 方法 2: 源码编译

```bash
# 安装构建依赖
sudo dnf install cmake ninja-build pkg-config \
  qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebengine-devel \
  qt6-qttools-devel systemd-rpm-macros libxcb-devel \
  polkit nodejs npm

# 编译安装
cmake --preset release
cmake --build build/release --parallel
sudo cmake --install build/release
```

---

## 🎞️ AppImage（暂未发布）

AppImage 包需要 `appimagetool` 工具构建，当前暂不可用。

### 手动构建方法

```bash
# 1. 克隆仓库
git clone https://github.com/zhouwr-2026/dsh-desktop-qt.git
cd dsh-desktop-qt

# 2. 准备 AppDir
rm -rf dist/AppDir
mkdir -p dist/AppDir/usr/bin dist/AppDir/usr/share/applications
cp build/release/dsh-desktop dist/AppDir/usr/bin/
cp packaging/dsh-desktop.desktop dist/AppDir/usr/share/applications/

# 3. 创建 AppRun
cat > dist/AppDir/AppRun << 'EOF'
#!/bin/sh
exec "$(dirname "$0")/usr/bin/dsh-desktop" "$@"
