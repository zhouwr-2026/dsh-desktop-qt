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
%config(noreplace) %{_unitdir}/dsh-theme-export.service
%config(noreplace) %{_unitdir}/dsh-theme-export.path
%config %{_sysconfdir}/xdg/autostart/dsh-desktop.desktop

%post
%systemd_post dsh-theme-export.path

%preun
%systemd_preun dsh-theme-export.path

%postun
%systemd_postun_with_restart dsh-theme-export.path

%changelog
* Thu Aug 28 2026 DSH Desktop 维护团队 - 0.1.0-1
- 首次正式发布
