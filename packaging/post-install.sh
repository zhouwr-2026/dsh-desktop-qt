#!/usr/bin/env bash
#
# DSH Desktop 后置安装钩子（由发行版包机制与 packaging/install.sh 调用）。
#
# 负责把绝对系统路径（/usr/lib/systemd/system、/etc/xdg/autostart）的 systemd unit
# 与桌面自启动条目复制到位。这些路径被 CMake install 排除在 PACKAGING 阶段
# 之外（避免普通用户权限失败），由本脚本在 root 上下文执行。
#
# 用法：
#   sudo bash packaging/post-install.sh [--prefix /usr]

set -euo pipefail

prefix="${PREFIX:-/usr}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            prefix="$2"
            shift 2
            ;;
        *)
            echo "未知参数：$1" >&2
            exit 2
            ;;
    esac
done

src_root="$(cd "$(dirname "$0")/.." && pwd)"
libdir="$prefix/lib"
sysconfdir="$prefix/etc"

if [[ ! -r "$src_root/packaging/dsh-theme-export.service" ]]; then
    echo "错误：找不到 packaging/dsh-theme-export.service" >&2
    exit 1
fi

echo "[post-install] 安装 systemd 单元"
install -d -m 755 "$libdir/systemd/system"
install -m 644 "$src_root/packaging/dsh-theme-export.service" "$libdir/systemd/system/"
install -m 644 "$src_root/packaging/dsh-theme-export.path" "$libdir/systemd/system/"

echo "[post-install] 安装桌面自启动条目"
install -d -m 755 "$sysconfdir/xdg/autostart"
install -m 644 "$src_root/packaging/dsh-desktop.desktop" "$sysconfdir/xdg/autostart/"

echo "[post-install] 完成。"
