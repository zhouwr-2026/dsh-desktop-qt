# 发布流程（维护者内部文档）

本流程适用于 `master` 分支的版本化发布。本文档是 `docs/RELEASE-NOTES-<version>.zh.md` 的执行手册。

## 1. 准备

1. 确认所有改动已合入 `master`，CI 全绿。
2. 确认 `CMakeLists.txt:20`、`packaging/PKGBUILD:5`、BuildVersion 头三处版本号一致。
3. 准备 CHANGELOG：把 `## [Unreleased]` 内容归并到 `## [<新版本>]` 块；保留最近一个 Round 段落作为发布说明核心。

## 2. 验证

```bash
cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
bash scripts/smoke.sh
systemd-analyze verify packaging/dsh-theme-export.service
systemd-analyze verify packaging/dsh-theme-export.path
```

要求：

- `ctest` 全过（24+ 个用例）
- `smoke.sh` 4/4 全过
- 两个 systemd unit 语法正确

## 3. 生成产物

```bash
bash scripts/package-linux.sh
```

输出到 `dist/`：

- `dsh-desktop-<version>-Linux.tar.gz`：通用压缩包（cpack TGZ）
- 可选 `.deb` / `.rpm`：脚本按本机工具自动选择
- `SHA256SUMS`：自动生成

## 4. 平台包回退

脚本会跳过本机不可用的格式，给出回退命令：

```bash
# Debian/Ubuntu（需 dpkg-dev）
dpkg-buildpackage -us -uc -b

# Fedora/RHEL/openSUSE（需 rpm-build）
rpmbuild -ba packaging/rpm/dsh-desktop.spec

# Arch（需 pacman / fakeroot / makepkg）
cd packaging && makepkg -si
```

## 5. 打 tag

```bash
git tag -a v<version> -m "DSH Desktop <version>"
git push origin v<version>
```

## 6. 更新 PKGBUILD 占位

修改 `packaging/PKGBUILD`：

```bash
sed -i 's|source=("dsh-desktop-0.1.0.tar.gz")|source=("$pkgname-$pkgver.tar.gz::https://github.com/<org>/<repo>/releases/download/v$pkgver/$pkgname-$pkgver.tar.gz")|' packaging/PKGBUILD
```

填入真实 SHA256：

```bash
updpkgsums packaging/PKGBUILD   # Arch 工具
# 或手动从 dist/SHA256SUMS 复制
```

提交到 AUR / Arch 镜像。

## 7. 发布归档与签名

将以下文件上传到 GitHub Release `v<version>`：

- `dsh-desktop-<version>-Linux.tar.gz`
- `dsh-desktop_<version>_amd64.deb`
- `dsh-desktop-<version>-1.<dist>.x86_64.rpm`
- `dsh-desktop-<version>-1-x86_64.pkg.tar.zst`
- `SHA256SUMS`
- `SHA256SUMS.sig`（GPG 签名）

签名命令：

```bash
gpg --detach-sign --armor SHA256SUMS
```

## 8. 上游镜像

- **AUR**：提交 `dsh-desktop` 包 + PKGBUILD 更新
- **Debian/Ubuntu PPA**：上传 `.deb` 到 Launchpad / OBS
- **Fedora COPR / openSUSE OBS**：上传 `.src.rpm` 或 spec
- **AUR 仓库地址**：`<org>/<repo>`（与 `CMakeLists.txt:9` 与 `packaging/PKGBUILD:9` 中的 `url=` 一致）

## 9. 发布后验证

```bash
# 在干净环境验证
sudo apt install ./dsh-desktop_<version>_amd64.deb   # Debian
sudo dnf install ./dsh-desktop-<version>-1.*.rpm    # Fedora
sudo pacman -U ./dsh-desktop-<version>-1-x86_64.pkg.tar.zst   # Arch

# 启动 + 冒烟
dsh-desktop --version
dsh-desktop --probe
dsh-desktop --smoke
```

## 10. 公告渠道

- GitHub Release 页面（用户主要信息源）
- 项目 README 顶部版本徽章（如已配置 CI）
- 项目 Issue 模板中的 "Latest release" 链接

## 11. 回滚

如果发布后发现严重问题：

1. 在 GitHub 上把 release 标记为 **pre-release**（仅隐藏，不删除）。
2. 修 hotfix → 重新走步骤 2–10。
3. 在 CHANGELOG 与 release notes 加 "Known issues / Regressions" 段。
