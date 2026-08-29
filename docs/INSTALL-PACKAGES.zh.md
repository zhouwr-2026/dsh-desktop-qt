# DSH Desktop 多格式安装包安装指南

本文档介绍如何安装 DSH Desktop v0.1.0 的各种格式安装包。

---

## 📦 可用安装包格式

| 格式 | 文件名 | 适用发行版 |
| --- | --- | --- |
| **通用 tar.gz** | `dsh-desktop-0.1.0-Linux.tar.gz` | 所有 systemd + Qt 6.6 Linux |
| **DEB** | `dsh-desktop_0.1.0_amd64.deb` | Debian 13+, Ubuntu 24.04+ |
| **RPM** | `dsh-desktop-0.1.0-1.x86_64.rpm` | Fedora 39+, RHEL 9+, openSUSE Tumbleweed |
| **AppImage** | `dsh-desktop-0.1.0-x86_64.AppImage` | 任意 Linux 桌面 |
| **Arch 包** | `dsh-desktop-0.1.0-1-x86_64.pkg.tar.zst` | Arch Linux / Manjaro / EndeavourOS |

---

## 🐧 Arch Linux / Manjaro / EndeavourOS

### 方法 1: yay 安装（推荐）

```bash
yay -Syu dsh-desktop
```

### 方法 2: makepkg 构建

```bash
git clone https://github.com/zhouwr-2026/dsh-desktop-qt.git
cd dsh-desktop-qt/packaging
makepkg -si
```

### 方法 3: 从 Release 下载

```bash
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-Linux.tar.gz
tar xzf dsh-desktop-0.1.0-Linux.tar.gz
cd dsh-desktop-0.1.0
sudo ./packaging/install.sh
```

---

## 📦 Debian / Ubuntu

### 方法 1: DEB 包安装

```bash
# 下载 DEB 包
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop_0.1.0_amd64.deb

# 安装
sudo apt install ./dsh-desktop_0.1.0_amd64.deb
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

### 方法 1: RPM 包安装

```bash
# 下载 RPM 包
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-1.x86_64.rpm

# 安装
sudo dnf install ./dsh-desktop-0.1.0-1.x86_64.rpm
# 或 RHEL/CentOS:
# sudo rpm -i ./dsh-desktop-0.1.0-1.x86_64.rpm
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

## 🎞️ AppImage（任意 Linux）

### 使用方法

```bash
# 下载 AppImage
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop-0.1.0-x86_64.AppImage

# 添加执行权限
chmod +x dsh-desktop-0.1.0-x86_64.AppImage

# 运行
./dsh-desktop-0.1.0-x86_64.AppImage

# 可选：安装到系统
mkdir -p ~/.local/bin
mv dsh-desktop-0.1.0-x86_64.AppImage ~/.local/bin/dsh-desktop
```

---

## ✅ SHA256 校验

下载完成后，建议校验文件完整性：

```bash
# 下载 SHA256SUMS 文件
wget https://github.com/zhouwr-2026/dsh-desktop-qt/releases/download/v0.1.0/SHA256SUMS

# 校验
sha256sum -c SHA256SUMS
```

---

## 🔧 手动构建所有格式

在项目目录运行：

```bash
sudo bash scripts/build-pkgs.sh
```

此脚本会自动检测当前发行版并安装所需依赖，然后生成所有可用的包格式。

---

## 📚 相关文档

- [多发行版安装指南](INSTALL-LINUX.zh.md)
- [依赖清单](DEPENDENCIES.md)
- [安全审核报告](SECURITY-REVIEW-2026-08-28.md)
