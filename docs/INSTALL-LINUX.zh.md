# DSH Desktop 多发行版安装与卸载指南

本文档适用于 DSH Desktop `0.1.0` 正式版。项目要求 **Qt 6.6 或更高版本**，并依赖 Qt WebEngine；老发行版若仓库版本不足，需要升级发行版、启用官方 backports，或使用通用压缩包。

## 1. 支持范围

| 系统 | 支持状态 | 推荐安装格式 |
| --- | --- | --- |
| Arch Linux / Manjaro / EndeavourOS | 完整支持 | `.pkg.tar.zst` / PKGBUILD |
| Debian 13、Ubuntu 24.04 及更新版本 | 支持，需确认 Qt 6.6+ | `.deb` |
| Debian 12 | 默认 Qt/libxcb 版本可能不足 | 通用压缩包或 backports |
| Fedora 39+ | 支持 | `.rpm` |
| RHEL 9 / Rocky / AlmaLinux 9 | 需要 EPEL/CRB 提供 Qt WebEngine | `.rpm` |
| openSUSE Tumbleweed / Leap 16+ | 支持 | `.rpm` |
| 其它使用 systemd 的 Linux | 基础支持 | `.tar.gz` |
| GNOME / XFCE / Cinnamon | 可运行，KDE 主题联动能力降低 | 对应发行版包 |

## 2. 通用运行要求

- Qt 6.6+：Core、Gui、Widgets、Network、DBus、Svg、WebEngine
- libxcb 1.17+
- systemd（`systemctl`、`journalctl`）
- polkit（`pkexec`）
- Node.js / npm
- `@deepseek-ai/dsh >= 0.1.0-rc.7`

首次安装若没有 `dsh` 命令，可执行：

```bash
sudo npm install -g @deepseek-ai/dsh
```

## 3. Arch Linux / Manjaro / EndeavourOS

### 3.1 安装构建依赖

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf \
  qt6-base qt6-svg qt6-webengine qt6-tools libxcb \
  systemd polkit procps-ng npm
```

KDE 用户建议安装：

```bash
sudo pacman -S --needed polkit-kde-agent knotifications
```

### 3.2 直接源码安装

```bash
git clone https://github.com/anywhere-labs/deepseek-harness-desktop.git
cd deepseek-harness-desktop
sudo packaging/install.sh
```

### 3.3 构建 Arch 包

正式 Release 发布页提供源代码归档后：

```bash
cd packaging
makepkg -si
```

`packaging/PKGBUILD` 中的 `source` 与 `sha256sums` 必须和对应 Release 归档一致；不要发布带 `SKIP` 的 AUR 包。

## 4. Debian / Ubuntu

### 4.1 安装构建依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libxcb1-dev qt6-base-dev qt6-svg-dev qt6-webengine-dev \
  qt6-tools-dev qt6-tools-dev-tools systemd policykit-1 procps npm
```

KDE 用户可选：

```bash
sudo apt install -y polkit-kde-agent-1
```

### 4.2 安装 `.deb`

```bash
sudo apt install ./dsh-desktop_0.1.0_amd64.deb
```

如果 apt 报 Qt 版本不足，请不要强行用 `dpkg --force-depends`；应升级发行版或使用已包含依赖的目标仓库。

### 4.3 从源码构建 DEB

```bash
sudo apt install -y debhelper devscripts
cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
cpack --config build/release/CPackConfig.cmake -G DEB -B dist
```

也可以使用 Debian 模板：

```bash
cp -a packaging/debian debian
dpkg-buildpackage -us -uc -b
```

## 5. Fedora / RHEL / Rocky / AlmaLinux

### 5.1 Fedora 安装依赖

```bash
sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config \
  libxcb-devel qt6-qtbase-devel qt6-qtsvg-devel \
  qt6-qtwebengine-devel qt6-qttools-devel \
  systemd-devel polkit procps-ng nodejs-npm rpm-build
```

### 5.2 RHEL 系启用仓库

```bash
sudo dnf install -y epel-release
sudo crb enable || true
```

然后按 Fedora 依赖列表安装。若仓库没有 Qt 6.6 WebEngine，建议使用 Fedora、更新的 RHEL 系版本或通用压缩包。

### 5.3 安装 RPM

```bash
sudo dnf install ./dsh-desktop-0.1.0-1.x86_64.rpm
```

### 5.4 从源码构建 RPM

```bash
cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
cpack --config build/release/CPackConfig.cmake -G RPM -B dist
```

或者：

```bash
rpmbuild -ba packaging/rpm/dsh-desktop.spec
```

## 6. openSUSE

### 6.1 安装依赖

```bash
sudo zypper install -y gcc-c++ cmake ninja pkg-config \
  libxcb-devel qt6-base-devel qt6-svg-devel qt6-webengine-devel \
  qt6-tools-devel systemd-devel polkit procps-ng npm rpm-build
```

### 6.2 安装 RPM

```bash
sudo zypper install ./dsh-desktop-0.1.0-1.x86_64.rpm
```

## 7. 通用 Linux 压缩包

### 7.1 生成包

```bash
bash scripts/package-linux.sh
```

产物位于 `dist/`。通用压缩包包含 `/usr` 相对布局，解压到临时目录后可以：

```bash
sudo cp -a usr/. /usr/
sudo cp -a etc/. /etc/
sudo systemctl daemon-reload
```

不建议在 musl 系发行版（如 Alpine）直接使用 glibc 构建的包；Alpine 需要原生编译 Qt WebEngine，成本较高，当前不作为正式支持目标。

## 8. 验证安装

```bash
dsh-desktop --version
dsh-desktop --probe
dsh-desktop --smoke
```

图形环境中启动：

```bash
dsh-desktop
```

## 9. 升级

- DEB：`sudo apt install ./新版本.deb`
- RPM：`sudo dnf upgrade ./新版本.rpm`
- Arch：`sudo pacman -U ./新版本.pkg.tar.zst`
- 源码安装：重新执行 `sudo packaging/install.sh`

桌面端也可通过托盘菜单检查 `dsh` 后端和桌面程序更新。

## 10. 卸载

### Debian / Ubuntu

```bash
sudo apt remove dsh-desktop
```

### Fedora / RHEL

```bash
sudo dnf remove dsh-desktop
```

### openSUSE

```bash
sudo zypper remove dsh-desktop
```

### Arch Linux

```bash
sudo pacman -Rns dsh-desktop
```

### 源码安装

```bash
sudo packaging/install.sh --uninstall
```

用户配置、WebEngine 缓存、下载文件和 DSH_HOME 默认保留，需用户手动确认后删除。

## 11. 发布者打包命令

通用入口（在仓库根执行，自动跑配置、编译、24 个单元测试、smoke 端到端、CPack TGZ、生成 SHA256SUMS）：

```bash
bash scripts/package-linux.sh
```

发布前必须满足：

```bash
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
bash scripts/smoke.sh
systemd-analyze verify packaging/dsh-theme-export.service
systemd-analyze verify packaging/dsh-theme-export.path
```

### 按发行版生成包

`scripts/package-linux.sh` 检测本机工具，自动选择生成器。已知工具与回退命令：

| 工具 | 产物 | 缺失时回退 |
| --- | --- | --- |
| cpack TGZ（内置 CMake） | `.tar.gz`（所有发行版） | 不可用——必须在任何打包前装 cmake |
| `dpkg-deb` | `.deb` | 安装 `dpkg-dev`，执行 `dpkg-buildpackage -us -uc -b`（使用本仓库 `debian/`） |
| `rpmbuild` | `.rpm` | 安装 `rpm-build`，执行 `rpmbuild -ba packaging/rpm/dsh-desktop.spec` |
| `makepkg` | `.pkg.tar.zst` | Arch 系统执行 `cd packaging && makepkg -si`（使用本仓库 `PKGBUILD`） |

脚本会在最后自动写 `dist/SHA256SUMS`，与所有发布归档一起签名。

### 上游归档清单

正式发布 `v0.1.0` 必须包含：

- `dsh-desktop-0.1.0.tar.gz`（源码归档）
- `dsh-desktop_0.1.0_amd64.deb` 或 `dsh-desktop_0.1.0_arm64.deb`
- `dsh-desktop-0.1.0-1.<dist>.x86_64.rpm`
- `dsh-desktop-0.1.0-1-x86_64.pkg.tar.zst`
- `SHA256SUMS`

发布者必须在上游 `v0.1.0` tag 中给 `packaging/PKGBUILD` 的 `source` 与 `sha256sums` 填入真实值（默认 `SKIP` 不能发布 AUR）。
