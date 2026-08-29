#!/usr/bin/env bash
#
# DSH Desktop 通用 Linux 打包脚本。
#
# 产物（自动按本机工具选择）：
#   * dsh-desktop-<版本>-Linux.tar.gz（所有发行版，cpack TGZ 生成器）
#   * .deb：环境具备 dpkg-deb 时由 cpack 生成；否则提示用 dpkg-buildpackage
#   * .rpm：环境具备 rpmbuild 时由 cpack 生成；否则提示用 rpmbuild -ba
#   * .pkg.tar.zst：Arch 环境按 docs/INSTALL-LINUX.zh.md 的 PKGBUILD 章节生成
#
# 退出码：
#   0 - 至少 TGZ 生成成功
#   1 - 编译/测试失败，或 TGZ 都未能生成

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/release"
OUTPUT_DIR="${ROOT}/dist"

log() { printf '[打包] %s\n' "$*"; }
warn() { printf '[打包] 提示：%s\n' "$*" >&2; }
fail() { printf '[打包] 错误：%s\n' "$*" >&2; exit 1; }

command -v cmake >/dev/null 2>&1 || fail "未找到 cmake"
command -v cpack >/dev/null 2>&1 || fail "未找到 cpack"
command -v ninja >/dev/null 2>&1 || fail "未找到 ninja"

mkdir -p "$OUTPUT_DIR"

log "配置发布构建"
cmake --preset release
log "编译"
cmake --build "$BUILD_DIR" --parallel
log "运行测试"
ctest --test-dir "$BUILD_DIR" --output-on-failure

log "生成通用 tar.gz（所有发行版可用）"
cpack --config "$BUILD_DIR/CPackConfig.cmake" -G TGZ -B "$OUTPUT_DIR"

if command -v dpkg-deb >/dev/null 2>&1; then
  log "检测到 dpkg-deb，生成 DEB"
  if ! cpack --config "$BUILD_DIR/CPackConfig.cmake" -G DEB -B "$OUTPUT_DIR"; then
    warn "cpack DEB 生成失败；本机环境可能缺少 dpkg-dev、file、fakeroot。"
    warn "备用：执行 'dpkg-buildpackage -us -uc -b'（需 dpkg-dev）。"
  fi
else
  warn "未检测到 dpkg-deb，跳过 DEB。"
  warn "Debian/Ubuntu 系统请安装 dpkg-dev 后执行 'dpkg-buildpackage -us -uc -b'。"
fi

if command -v rpmbuild >/dev/null 2>&1; then
  log "检测到 rpmbuild，生成 RPM"
  if ! cpack --config "$BUILD_DIR/CPackConfig.cmake" -G RPM -B "$OUTPUT_DIR"; then
    warn "cpack RPM 生成失败；本机环境可能缺少 rpm-build。"
    warn "备用：执行 'rpmbuild -ba packaging/rpm/dsh-desktop.spec'（需 rpm-build）。"
  fi
else
  warn "未检测到 rpmbuild，跳过 RPM。"
  warn "Fedora/RHEL/openSUSE 系统请安装 rpm-build 后执行 'rpmbuild -ba packaging/rpm/dsh-desktop.spec'。"
fi

if command -v makepkg >/dev/null 2>&1; then
  log "检测到 makepkg，Arch 打包见 docs/INSTALL-LINUX.zh.md（PKGBUILD 在 packaging/）"
else
  warn "未检测到 makepkg，Arch 打包需要手动执行 'cd packaging && makepkg -si'。"
fi

# 发布者统一生成 SHA256SUMS，便于镜像校验
mapfile -t PKG_FILES < <(find "$OUTPUT_DIR" -maxdepth 1 -type f \(     -name "*.tar.gz" -o -name "*.deb" -o -name "*.rpm" -o -name "*.pkg.tar.*" \))
if (( ${#PKG_FILES[@]} == 0 )); then
  warn "未发现任何产物。"
  exit 1
fi
log "生成 SHA256SUMS"
(cd "$OUTPUT_DIR" && sha256sum "${PKG_FILES[@]##*/}" > SHA256SUMS)
log "产物目录：$OUTPUT_DIR"
ls -lh "${PKG_FILES[@]}" "$OUTPUT_DIR/SHA256SUMS"
