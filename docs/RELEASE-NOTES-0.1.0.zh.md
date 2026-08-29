# DSH Desktop 0.1.0 发布说明

发布日期：2026-08-28

## 一句话总结

DSH Desktop 是 DeepSeek Harness 在 Linux 桌面端的官方原生包装器，基于 Qt 6 与 WebEngine，使用 C++17 实现系统托盘与安全的 systemd 服务管理。本次为首次正式发布。

## 功能

- 基于 Qt WebEngine 嵌入 DeepSeek Harness Web 界面。
- 常驻系统托盘菜单（显示/隐藏桌面、检查更新、查看日志、清空下载缓存、退出/重启等 13 项）。
- 主题自适应图标（hicolor + KDE Breeze / Breeze Dark）。
- 会话导出下载由原生保存对话框接管，进度条与桌面通知完整。
- 只读 systemd 服务发现 + supervised `dsh web` 兜底 + 一次性更新流程。
- 跨发行版打包：Arch PKGBUILD、Debian control/rules、Fedora/RHEL/openSUSE spec、通用 tar.gz。

## 系统要求

- Qt 6.6 或更高版本
- libxcb 1.17 或更高版本
- systemd、polkit、Node.js/npm
- `@deepseek-ai/dsh >= 0.1.0-rc.7`

## 多发行版安装

详见 `docs/INSTALL-LINUX.zh.md`。

| 系统 | 推荐格式 |
| --- | --- |
| Arch Linux / Manjaro / EndeavourOS | `.pkg.tar.zst` 或 PKGBUILD |
| Debian 13、Ubuntu 24.04+ | `.deb` |
| Fedora 39+ | `.rpm` |
| RHEL 9 / Rocky / AlmaLinux | `.rpm`（启用 EPEL） |
| openSUSE Tumbleweed / Leap 16+ | `.rpm` |
| 其它 systemd Linux | `.tar.gz` |

## 升级

DEB / RPM / Arch 包直接用发行版包管理器升级；源码安装重新运行 `sudo packaging/install.sh`。

## 卸载

- 包管理：`apt remove dsh-desktop` / `dnf remove dsh-desktop` / `zypper remove dsh-desktop` / `pacman -Rns dsh-desktop`。
- 源码安装：`sudo packaging/install.sh --uninstall`。

用户配置、WebEngine 缓存与下载目录默认保留。

## 已知限制

- 不支持 musl 系发行版（如 Alpine），需原生编译 Qt WebEngine。
- 主题导出服务以 root 身份读 `/root/.config/kdeglobals`；非 root KDE 会话需手动执行 `systemctl --user enable --now dsh-theme-export.path`。
- 远程 URL 模式不做 SSRF 防护，需用户自行确认远端可信。

## 安全说明

- 不使用 shell 字符串拼接，所有外部命令均通过 `QProcess::setProgram/setArguments` 显式 argv 派生。
- 提权统一走 `pkexec --disable-internal-agent` 或 root-owned 二进制硬编码路径。
- systemd 主题导出服务启用 `NoNewPrivileges`、`ProtectSystem=strict`、`ProtectHome=yes`、`ReadOnlyPaths`、`ReadWritePaths`。
- npm 安装阶段默认 `ignore-scripts=true`，避免 preinstall 投毒。

完整安全审核记录见 `docs/SECURITY-REVIEW-2026-08-28.md`。
