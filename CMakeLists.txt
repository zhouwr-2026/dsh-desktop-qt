# @author zhouwr
#
# DSH Desktop — KDE Plasma 6 上的 DSH Web 原生包装器
#
# 构建：
#   mkdir build && cd build
#   cmake -DCMAKE_BUILD_TYPE=Release ..
#   ninja
#
# 安装（用户级）：
#   DESTDIR=$HOME/.local ninja install
# 或作为系统包（packaging/PKGBUILD）：
#   makepkg -si

cmake_minimum_required(VERSION 3.19)
project(dsh-desktop
    VERSION 0.1.0
    DESCRIPTION "DSH Desktop — KDE Plasma 6 上的 DSH Web 原生包装器"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# 构建版本
# ---------------------------------------------------------------------------

# 由 PROJECT_VERSION 生成版本头（含 DSH_DESKTOP_VERSION 宏），供主程序、
# 自更新助手、About 对话框与 User-Agent 版本串共用同一来源。
set(DSH_DESKTOP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildVersion.h.in
    ${DSH_DESKTOP_GENERATED_DIR}/BuildVersion.h
    @ONLY
)

# ---------------------------------------------------------------------------
# 编译选项
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---------------------------------------------------------------------------
# 依赖
# ---------------------------------------------------------------------------

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    DBus
    Svg
    WebEngineCore
    WebEngineWidgets
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

# ---------------------------------------------------------------------------
# 源文件清单
# ---------------------------------------------------------------------------

set(DSH_CORE_SOURCES
    src/platform/RenderingPolicy.cpp
    src/util/Logger.cpp
    src/util/Notify.cpp
    src/icon/IconLoader.cpp
    src/theme/ThemeWatcher.cpp
    src/backend/Backend.cpp
    src/backend/SystemdBackend.cpp
    src/backend/SupervisedBackend.cpp
    src/service/SystemctlShowParser.cpp
    src/service/ServiceDiscovery.cpp
    src/service/ServiceOwnership.cpp
    src/service/ServiceUnitBuilder.cpp
    src/service/ServiceProvisioner.cpp
    src/service/DshServiceManager.cpp
    src/service/InstallationPlan.cpp
    src/service/UninstallPlan.cpp
    src/service/Uninstaller.cpp
    src/updater/Updater.cpp
    src/updater/DesktopVersionChecker.cpp
    src/updater/DesktopReleaseDownloader.cpp
    src/updater/UpdatePlan.cpp
    src/web/LoopbackWebPage.cpp
    src/web/SuppressExportToast.cpp
    src/web/DownloadInterceptor.cpp
    src/app/ExitDialog.cpp
    src/app/RestartDialog.cpp
    src/app/UpdateDialog.cpp
    src/app/AboutDialog.cpp
    src/app/UninstallDialog.cpp
    src/app/LogViewer.cpp
    src/app/TrayController.cpp
    src/app/DshWindow.cpp
    src/app/DshDesktopApp.cpp
)

set(DSH_CORE_HEADERS
    src/platform/RenderingPolicy.h
    src/util/Logger.h
    src/util/Notify.h
    src/icon/IconLoader.h
    src/theme/ThemeWatcher.h
    src/backend/Backend.h
    src/backend/SystemdBackend.h
    src/backend/SupervisedBackend.h
    src/service/ServiceInfo.h
    src/service/SystemctlShowParser.h
    src/service/ServiceDiscovery.h
    src/service/ServiceOwnership.h
    src/service/ServiceUnitBuilder.h
    src/service/ServiceProvisioner.h
    src/service/DshServiceManager.h
    src/service/InstallationPlan.h
    src/service/UninstallPlan.h
    src/service/Uninstaller.h
    src/updater/Updater.h
    src/updater/DesktopVersionChecker.h
    src/updater/DesktopReleaseDownloader.h
    src/updater/UpdatePlan.h
    src/web/LoopbackWebPage.h
    src/web/SuppressExportToast.h
    src/web/DownloadInterceptor.h
    src/app/ExitDialog.h
    src/app/RestartDialog.h
    src/app/UpdateDialog.h
    src/app/AboutDialog.h
    src/app/UninstallDialog.h
    src/app/LogViewer.h
    src/app/TrayController.h
    src/app/DshWindow.h
    src/app/DshDesktopApp.h
)

# 把核心代码编译成静态库，既给主可执行用，也给测试用
add_library(dsh_desktop_core STATIC ${DSH_CORE_SOURCES} ${DSH_CORE_HEADERS})
target_include_directories(dsh_desktop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DSH_DESKTOP_GENERATED_DIR}
)
target_link_libraries(dsh_desktop_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    Qt6::Svg
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
    PkgConfig::XCB
)
target_compile_options(dsh_desktop_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
)

# 嵌入图标资源到主可执行（图标只在运行时需要，不需要在测试里嵌入）
# Qt 6.11 的 qt6_add_resources 不直接支持 QRC 文件，所以直接列出文件
# 并通过 FILES 模式交给它处理。
add_executable(dsh-desktop
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons.qrc
)
# AUTOMOC/AUTORCC 会自动处理 .qrc → qrc_icons.cpp → 嵌入到二进制
set_target_properties(dsh-desktop PROPERTIES
    AUTORCC ON
)
target_link_libraries(dsh-desktop PRIVATE
    dsh_desktop_core
)
target_compile_options(dsh-desktop PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
)

# ---------------------------------------------------------------------------
# 自更新助手核心（仅依赖 Qt Core，供 CLI 与单元测试复用）
# ---------------------------------------------------------------------------

# 独立小型静态库：纯校验/原子替换逻辑，不依赖 Widgets/WebEngine/DBus/进程，
# 保证 ``dsh-desktop-updater`` CLI 与测试都只带最小依赖即可复用。
add_library(dsh_desktop_updater_helper STATIC
    src/updater/DesktopUpdateHelper.cpp
    src/updater/DesktopUpdateHelper.h
)
target_include_directories(dsh_desktop_updater_helper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(dsh_desktop_updater_helper PUBLIC
    Qt6::Core
)
target_compile_options(dsh_desktop_updater_helper PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
)

# 独立自更新助手 CLI：由旧实例退出前拉起，等待 PID 退出 -> 校验 -> 原子替换。
add_executable(dsh-desktop-updater
    src/updater/DesktopUpdateHelperMain.cpp
)
target_include_directories(dsh-desktop-updater PRIVATE
    ${DSH_DESKTOP_GENERATED_DIR}
)
target_link_libraries(dsh-desktop-updater PRIVATE
    dsh_desktop_updater_helper
    Qt6::Core
)
target_compile_options(dsh-desktop-updater PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
)

# ---------------------------------------------------------------------------
# 卸载器 CLI：原生对话框 + 按 UninstallPlan 显式 QFile/QDir 删除。复用
# dsh_desktop_core（UninstallPlan / ServiceOwnership / ServiceDiscovery /
# DshServiceManager / UninstallDialog）。
# ---------------------------------------------------------------------------
add_executable(dsh-desktop-uninstaller
    src/service/UninstallerMain.cpp
)
target_include_directories(dsh-desktop-uninstaller PRIVATE
    ${DSH_DESKTOP_GENERATED_DIR}
)
target_link_libraries(dsh-desktop-uninstaller PRIVATE
    dsh_desktop_core
)
target_compile_options(dsh-desktop-uninstaller PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow>
)

# ---------------------------------------------------------------------------
# 安装
# ---------------------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS dsh-desktop
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(TARGETS dsh-desktop-updater
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(TARGETS dsh-desktop-uninstaller
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES packaging/dsh-desktop.desktop
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)
# /etc/xdg/autostart 对本机所有桌面用户生效；使用绝对目标保证
# CMAKE_INSTALL_PREFIX=/usr 时不会误装到 /usr/etc。
install(FILES packaging/dsh-desktop.desktop
    DESTINATION "/etc/xdg/autostart"
)
# KWin/Plasma 对已关联 .desktop 的窗口优先使用桌面条目的 Icon，而不是
# _NET_WM_ICON。同一个图标名在 Breeze 与 Breeze Dark 中分别安装黑/白
# SVG，才能让标题栏和任务栏随 KDE 图标主题切换。
foreach(icon_size 16 22 24 32 48 64)
    install(FILES assets/dsh-whale-black.svg
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/breeze/apps/${icon_size}"
        RENAME dsh-whale.svg
    )
    install(FILES assets/dsh-whale-white.svg
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/breeze-dark/apps/${icon_size}"
        RENAME dsh-whale.svg
    )
endforeach()
install(PROGRAMS packaging/dsh-theme-export
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/dsh-desktop"
)
install(FILES
    packaging/dsh-theme-export.service
    packaging/dsh-theme-export.path
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------

option(DSH_DESKTOP_BUILD_TESTS "编译单元测试" ON)
if(DSH_DESKTOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
