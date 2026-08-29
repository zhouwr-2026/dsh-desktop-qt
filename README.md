# DSH Desktop — DeepSeek Harness 原生 Linux 桌面端

DSH Desktop 是使用 C++17 与 Qt 6 开发的 DeepSeek Harness 原生桌面包装器。它通过 Qt WebEngine 嵌入官方 Web 界面，提供系统托盘、原生对话框、主题图标、更新与 systemd 服务管理，不依赖 Electron、Tauri 或 Python 运行时。

> **Reference**: This project is based on the desktop wrapper concept from [anywhere-labs/deepseek-harness-desktop](https://github.com/anywhere-labs/deepseek-harness-desktop), reimplemented in native C++/Qt6 for Linux KDE Plasma 6 with zero Electron dependency.

## Credits & Acknowledgments

- 常驻系统托盘与原生 Qt 菜单。
- 持久化 WebEngine 登录会话、缓存与 Cookie。
- 会话导出下载、原生保存对话框、进度显示与桌面通知。
- 外部 HTTP/HTTPS 链接交给系统浏览器。
- KDE Plasma 深色/浅色主题图标联动，并为 GNOME/XFCE 提供 hicolor 回退图标。
- 统一检查 `dsh` 后端与桌面端更新。
- 只读发现并校验现有 `dsh-web.service`；没有有效服务时使用 supervised `dsh web` 子进程。
- 后端健康检查、自动恢复提示和安全的安装/卸载助手。

## 系统要求

- Qt 6.6 或更高版本：Core、Gui、Widgets、Network、DBus、Svg、WebEngine。
- libxcb 1.17 或更高版本。
- CMake 3.19+、Ninja、PkgConfig。
- systemd、polkit、Node.js/npm。
- `@deepseek-ai/dsh >= 0.1.0-rc.7`。

支持 Arch Linux、Debian/Ubuntu、Fedora/RHEL/openSUSE 和其它使用 systemd 的 Linux。详细兼容范围与包名见 [Linux 多发行版安装指南](docs/INSTALL-LINUX.zh.md)。

## 快速安装

### Arch Linux

```bash
sudo packaging/install.sh
```

### DEB / RPM / 通用压缩包

正式发布页会提供：

- `dsh-desktop_0.1.0_amd64.deb`
- `dsh-desktop-0.1.0-1.x86_64.rpm`
- `dsh-desktop-0.1.0-Linux.tar.gz`
- Arch `PKGBUILD` / `.pkg.tar.zst`

完整命令见 [docs/INSTALL-LINUX.zh.md](docs/INSTALL-LINUX.zh.md)。

## 后端管理

对于 loopback 地址，DSH Desktop 会只读检查 system 与 user 两个 scope 的 `dsh-web.service`。只有 `LoadState=loaded` 且 `ExecStart` 调用官方 `dsh web` 的服务才会被复用。已有服务处于 inactive 或 failed 时，桌面端必须先获得用户确认才会启动。

显式远程 URL 使用 External 模式，桌面端不会启动、停止或重启远程服务。远程 URL 应由用户自行确认可信性。

## 更新与网络访问

启动后约 60 秒，桌面端会访问：

- npm 官方注册表：检查 `@deepseek-ai/dsh` 版本；
- 项目 Gitee Release API：检查桌面端版本。

托盘菜单也可以手动触发检查。桌面端更新包会验证 SHA-256，再交给独立 `dsh-desktop-updater` 原子替换运行中的程序。

## 构建与测试

```bash
cmake --preset dev
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure

cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

完整端到端验证：

```bash
bash scripts/smoke.sh
```

生成跨发行版包：

```bash
bash scripts/package-linux.sh
```

## 诊断

```bash
dsh-desktop --help
dsh-desktop --version
dsh-desktop --probe
dsh-desktop --smoke
dsh-desktop --self-test
```

日志默认写入 Qt `AppDataLocation`，也可以使用 `--log-file <路径>` 覆盖。

## 卸载

源码安装：

```bash
sudo packaging/install.sh --uninstall
```

DEB/RPM/Arch 包请使用对应包管理器卸载。用户配置、WebEngine 数据、下载文件和 DSH_HOME 默认保留。

## 文档

- [Linux 多发行版安装指南](docs/INSTALL-LINUX.zh.md)
- [0.1.0 发布说明](docs/RELEASE-NOTES-0.1.0.zh.md)
- [依赖清单](docs/DEPENDENCIES.md)
- [服务架构方案](docs/DSH-DESKTOP-SERVICE-PLAN.zh.md)
- [安全审核报告](docs/SECURITY-REVIEW-2026-08-28.md)
- [安全策略（披露漏洞）](SECURITY.md)
- [变更日志](CHANGELOG.md)
- [贡献指南](CONTRIBUTING.md)
- [支持与反馈](SUPPORT.md)
- [发布流程（维护者）](docs/RELEASING.md)
- [CI 工作流](.github/workflows/release.yml)


## Credits & Acknowledgments

### Upstream Projects
- **[deepseek-ai/deepseek-harness](https://github.com/deepseek-ai/deepseek-harness)** — The official DeepSeek Harness backend and CLI tool that provides the web service we wrap. Licensed under Apache-2.0.
- **[anywhere-labs/deepseek-harness-desktop](https://github.com/anywhere-labs/deepseek-harness-desktop)** — The reference Electron-based desktop wrapper for Windows/macOS. Our project reimplements the desktop experience natively for Linux using Qt 6.

### Dependencies
- **[Qt 6](https://www.qt.io/)** — Cross-platform application framework. Licensed under GPL-3.0 / Commercial.
- **[systemd](https://systemd.io/)** — System and service manager. Licensed under LGPL-2.1+.
- **[polkit](https://www.freedesktop.org/wiki/Software/polkit/)** — Policy framework for managing system-wide privileges. Licensed under LGPL-2.1+.
- **[libxcb](https://xcb.freedesktop.org/)** — C binding to the X11 protocol. Licensed under MIT.

### Open Source Licenses
This project is licensed under MIT License. See [LICENSE](LICENSE) for details.

The following third-party components are used:
- Qt 6 components: LGPL-3.0 or Commercial
- systemd: LGPL-2.1+
- polkit: LGPL-2.1+
- libxcb: MIT

See [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) for complete dependency tree.

---

## 许可证

MIT
