#!/usr/bin/env bash
# @author zhouwr
#
# /usr/lib/dsh-desktop/icon-brightness.sh
# 决定 dsh-whale 普通色版应装黑色版还是白色版（stdout 输出 "dark" / "light"）。
# 所有发行版包 post-install hook（PKGBUILD post_install / debian postinst /
# rpm %post）+ packaging/install.sh 通过 subshell 调用这一个脚本，避免逻辑
# 在四处重复并漂移。
#
# 用法：
#   brightness="$(/usr/lib/dsh-desktop/icon-brightness.sh)"  # 输出 "dark"/"light"
#
# 设计：脚本本身用 bash（KDE 部署机器一定有 bash），调用方用 sh / bash / zsh
# 任意都行——subshell 隔离 bash 依赖到本脚本进程，调用方不需 source 也不需
# 兼容 bash 语法（deb postinst 的 dash、rpm %post 的 sh 都安全）。
#
# 检测顺序（取众数；多重 KDE 配置场景下确保至少有一个真实用户偏好）：
#   1) SUDO_USER 真实用户 home（install.sh sudo 场景；普通用户主动 sudo 装）
#   2) $HOME（用户直接跑脚本调试；非 sudo 场景）
#   3) /home/* 下 UID>=1000 普通用户的 home（deb/rpm/Arch 包安装 root 场景，
#      SUDO_USER 不存在——包管理器后台守护）
# 平局场景默认 light（保守：黑色图标在亮色背景可见；多数发行版默认亮色
# Plasma）。candidates 为空时也直接返回 light。
#
# 颜色判定规则：kdeglobals 的 LookAndFeelPackage / ColorScheme 字段任一含
# 'dark' / 'Dark' 关键字即认为暗色（KDE 主题命名约定：breath-dark /
# BreezeDark / DarkBreeze 等）。

# shellcheck shell=bash

set -u

dark=0
light=0

# helper：解析单个 kdeglobals，返回暗色=1 亮色=0 累加到全局计数
classify() {
    local conf="$1"
    [[ -r "$conf" ]] || return 0
    local combined
    combined="$(grep -E '^(LookAndFeelPackage|ColorScheme)=' "$conf" 2>/dev/null | tr '\n' ' ' || true)"
    case "$combined" in
        *-dark*|*Dark*|*[Dd]ark*) dark=$((dark + 1)) ;;
        *)                        light=$((light + 1)) ;;
    esac
}

# 1) SUDO_USER 真实用户 home
if [[ -n "${SUDO_USER:-}" ]]; then
    real_home="$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6 || true)"
    # 排除 root / service account home（root 通常不跑 KDE，service account
    # 没在 /home 下），保证 SUDO_USER 是真实桌面用户。
    if [[ -n "$real_home" && "$real_home" == /home/* ]]; then
        classify "$real_home/.config/kdeglobals"
    fi
fi

# 2) 当前 HOME（用户直接跑脚本调试；非 sudo 场景）—— 同样排除 root
if [[ "$HOME" == /home/* ]]; then
    classify "$HOME/.config/kdeglobals"
fi

# 3) 遍历 /home/*（包安装 root 场景；SUDO_USER 不存在）。不用 getent passwd
# 是因为部分发行版 NSS 配置（passwd: files systemd / sssd 等）下 getent
# passwd 不列出 UID >= 1000 的真实用户——getent passwd zhouwr 能查到但
# getent passwd 全部列表漏掉。直接 glob /home/*/ 是最可靠的"真实桌面用户
# 列表"——root / service account home 不在 /home/ 下，glob 自动排除。
for home in /home/*/; do
    [[ -d "$home" ]] || continue
    classify "$home/.config/kdeglobals"
done

if [[ "$dark" -gt "$light" ]]; then
    echo "dark"
else
    echo "light"
fi