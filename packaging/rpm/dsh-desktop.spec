Name:           dsh-desktop
Version:        0.1.0
Release:        1%{?dist}
Summary:        DeepSeek Harness 原生 Linux 桌面包装器
License:        MIT
URL:            https://github.com/anywhere-labs/deepseek-harness-desktop
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.19
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  libxcb-devel >= 1.17
BuildRequires:  qt6-qtbase-devel >= 6.6
BuildRequires:  qt6-qtsvg-devel >= 6.6
BuildRequires:  qt6-qtwebengine-devel >= 6.6
BuildRequires:  qt6-qttools-devel
BuildRequires:  systemd-rpm-macros

Requires:       qt6-qtbase >= 6.6
Requires:       qt6-qtsvg >= 6.6
Requires:       qt6-qtwebengine >= 6.6
Requires:       libxcb >= 1.17
Requires:       systemd
Requires:       polkit
Requires:       procps-ng
Requires:       nodejs-npm
Recommends:     polkit-kde-agent

%description
DSH Desktop 使用 Qt 6 WebEngine 嵌入 DeepSeek Harness Web 界面，
提供系统托盘、原生对话框、主题图标、更新与安全的 systemd 服务管理。

%prep
%autosetup

%build
%cmake -G Ninja -DDSH_DESKTOP_BUILD_TESTS=ON
%cmake_build

%check
%ctest

%install
%cmake_install
mkdir -p %{buildroot}%{_unitdir} %{buildroot}%{_sysconfdir}/xdg/autostart
cp packaging/dsh-theme-export.service %{buildroot}%{_unitdir}/
cp packaging/dsh-theme-export.path %{buildroot}%{_unitdir}/
cp packaging/dsh-desktop.desktop %{buildroot}%{_sysconfdir}/xdg/autostart/

%files
%license LICENSE
%{_bindir}/dsh-desktop
%{_bindir}/dsh-desktop-updater
%{_bindir}/dsh-desktop-uninstaller
%{_bindir}/dsh-profile-check
%{_datadir}/applications/dsh-desktop.desktop
%{_datadir}/icons/hicolor/scalable/apps/dsh-whale.svg
%{_datadir}/icons/hicolor/scalable/apps/dsh-whale-black.svg
%{_datadir}/icons/hicolor/scalable/apps/dsh-whale-white.svg
%{_libdir}/dsh-desktop/dsh-theme-export
%{_libdir}/dsh-desktop/icon-brightness.sh
%config(noreplace) %{_unitdir}/dsh-theme-export.service
%config(noreplace) %{_unitdir}/dsh-theme-export.path
%config %{_sysconfdir}/xdg/autostart/dsh-desktop.desktop

%post
%systemd_post dsh-theme-export.path
# 按本机真实 KDE users 的 colorscheme 覆盖 hicolor/scalable/apps/dsh-whale.svg
# 普通色版——避免全部用户 fallback 到 CMake 装的固定黑色版（暗色 look-and-feel
# 下看不见）。共享脚本 packaging/icon-brightness.sh 已由 CMake install 装到
# /usr/lib/dsh-desktop/icon-brightness.sh 并 chmod +x，本脚本通过 subshell 调
# 用它（不 source）——这样 %post 跑在 sh 上下文不会失败（脚本内部用 bash 写法
# 但 subshell 隔离）。rpm 跑此 hook 时是 root 上下文，SUDO_USER 不存在——
# 脚本内部遍历 /home/* 找 KDE 配置。
brightness_lib=%{_libdir}/dsh-desktop/icon-brightness.sh
if [ -x "$brightness_lib" ]; then
    brightness="$("$brightness_lib")"
    case "$brightness" in
      dark)  src=%{_datadir}/icons/hicolor/scalable/apps/dsh-whale-white.svg ;;
      *)     src=%{_datadir}/icons/hicolor/scalable/apps/dsh-whale-black.svg ;;
    esac
    dst=%{_datadir}/icons/hicolor/scalable/apps/dsh-whale.svg
    if [ -r "$src" ] && [ -r "$dst" ]; then
        cp -f "$src" "$dst"
        chmod 0644 "$dst"
        echo "dsh-desktop: hicolor dsh-whale.svg 已按本机 KDE colorscheme ($brightness) 覆盖为 $(basename "$src")"
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f %{_datadir}/icons/hicolor 2>/dev/null || true
    fi
fi

%preun
%systemd_preun dsh-theme-export.path

%postun
%systemd_postun_with_restart dsh-theme-export.path

%changelog
* Thu Aug 28 2026 DSH Desktop 维护团队 - 0.1.0-1
- 首次正式发布
