# 支持与反馈

## 报告 Bug

请使用 GitHub Issues 提交 bug 报告，并附带：

1. 复现步骤
2. 期望行为 vs 实际行为
3. 环境信息：
   - 发行版与版本（`/etc/os-release`）
   - Qt 版本（`qmake6 -query QT_VERSION` 或 `pacman -Qi qt6-base`）
   - `dsh-desktop --version` 输出
   - `dsh-desktop --probe` 输出（无需图形界面）
4. 相关日志（默认 `~/.local/share/dsh-desktop/dsh-desktop.log`，可用 `--log-file` 覆盖）

## 功能请求

请同样通过 GitHub Issues，标题以 `[Feature]` 前缀。先搜索是否已有类似请求。

## 安全问题

**不要**通过 GitHub Issues 报告安全漏洞。请私下联系维护团队：

- 邮箱：`security@dsh-desktop.example.invalid`（待维护者替换为真实邮箱）
- 加密：使用项目根 `SECURITY.md` 中维护者 PGP 公钥加密邮件

完整安全策略与已知威胁模型见 [`docs/SECURITY-REVIEW-2026-08-28.md`](docs/SECURITY-REVIEW-2026-08-28.md)。

## 发行版打包支持

发行版维护者：

- Arch AUR：包名 `dsh-desktop`，spec 在 `packaging/PKGBUILD`
- Debian / Ubuntu：spec 在 `debian/`
- Fedora / RHEL / openSUSE：spec 在 `packaging/rpm/dsh-desktop.spec`
- 通用 Linux：TGZ 在 `dist/dsh-desktop-<version>-Linux.tar.gz`

详细安装 / 升级 / 卸载见 [`docs/INSTALL-LINUX.zh.md`](docs/INSTALL-LINUX.zh.md)。

## 社区

- 文档：本仓库 `docs/` 目录
- 设计方案：`docs/DSH-DESKTOP-SERVICE-PLAN.zh.md`
- 发布说明：`docs/RELEASE-NOTES-<version>.zh.md`
- 变更历史：[`CHANGELOG.md`](CHANGELOG.md)

## 商业支持

本项目由社区维护，无商业支持 SLA。如需商业部署定制，请联系维护团队。
