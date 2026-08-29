# @author zhouwr
#
# DSH Desktop — KDE Plasma 6 上的 DSH Web 原生包装器
#
# 开发构建：
#   cmake --preset dev
#   cmake --build build/dev --parallel
#   ctest --test-dir build/dev --output-on-failure
# Release 构建：
#   cmake --preset release
#   cmake --build build/release --parallel
#
# 安装（系统级或作为 packaging/PKGBUILD 的打包输入）：
#   cmake --install build/release
# 或作为 Arch 系统包：
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

# Qt 最低要求抬到 6.6 LTS：6.5 开源分支已不活跃，6.6 LTS 仍处于商业 LTS 与
# 社区持续维护窗口，可拿到 Chromium / QtWebEngine backport 的安全补丁。
# (变更理由: 依赖审查 #1, 建议优先级 P0-1)
find_package(Qt6 6.6 REQUIRED COMPONENTS
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
# 钉 libxcb ≥ 1.17（与 PKGBUILD / install.sh 的 depends= 对齐，避免发行版过旧
# xcb 触发 GLX 探测时崩溃；这是 2023-2024 年 CVE 修复合入的版本）。
# (变更理由: 依赖审查 P0-2, 与 PKGBUILD/install.sh 一致)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb>=1.17)

# ---------------------------------------------------------------------------
# 源文件清单
# ---------------------------------------------------------------------------

# 自动收集 src/ 下的所有 .cpp / .h，避免手维护双胞胎表时的飘移。
# CMake 3.23+ 推荐用 HEADER_FILE_SET，但本项目保留与 3.19 的最低要求，
# 故采用 CONFIGURE_DEPENDS 让新加文件触发自动重配。
# (变更理由: 结构审查 #6, 维护负担)
file(GLOB DSH_CORE_SOURCES CONFIGURE_DEPENDS
    src/platform/*.cpp
    src/util/*.cpp
    src/icon/*.cpp
    src/theme/*.cpp
    src/backend/*.cpp
    src/service/*.cpp
    src/updater/*.cpp
    src/web/*.cpp
    src/app/*.cpp
)
file(GLOB DSH_CORE_HEADERS CONFIGURE_DEPENDS
    src/platform/*.h
    src/util/*.h
    src/icon/*.h
    src/theme/*.h
    src/backend/*.h
    src/service/*.h
    src/updater/*.h
    src/web/*.h
    src/app/*.h
)
list(REMOVE_ITEM DSH_CORE_SOURCES
    "src/updater/DesktopUpdateHelper.cpp"
)
list(REMOVE_ITEM DSH_CORE_HEADERS
    "src/updater/DesktopUpdateHelper.h"
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
# util/Sha256.cpp 也独立编译一份（与 dsh_desktop_core 中的同名目标互不耦合，
# 让 updater_helper 仍保持独立的 Qt Core 依赖），保证 SHA-256 计算逻辑单一来源。
add_library(dsh_desktop_updater_helper STATIC
    src/updater/DesktopUpdateHelper.cpp
    src/updater/DesktopUpdateHelper.h
    src/util/Sha256.cpp
    src/util/Sha256.h
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
install(PROGRAMS scripts/check-dsh-profile.sh
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME dsh-profile-check
)
install(FILES packaging/dsh-desktop.desktop
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)
# /etc/xdg/autostart 由发行版包机制或 packaging/install.sh 在打包后写入；
# 这里只装 applications/ 目录，CMake install 不触 /etc。
# CPack 与发行版打包（deb / rpm / arch）会按发行版规范处理 autostart：
#   * deb：在 debian/dsh-desktop.lintian-overrides 之外，由 dh_install 系统复制
#   * rpm：使用 %post 脚本 mkdir -p /etc/xdg/autostart 后 install -m644
#   * arch：PKGBUILD 用 install -Dm644 ... /etc/xdg/autostart
#   * install.sh：直接 install -Dm644
# KWin/Plasma 对已关联 .desktop 的窗口优先使用桌面条目的 Icon，而不是
# _NET_WM_ICON。同一个图标名在 Breeze 与 Breeze Dark 中分别安装黑/白
# SVG，才能让标题栏和任务栏随 KDE 图标主题切换。
#
# KDE Kickoff（启动器）与 Tasks plasmoid（任务栏）查图标的路径不同，必须同时装
# 普通色版（Kickoff 命中）+ symbolic SVG（Tasks plasmoid 自动按 colorscheme 配色）：
#   * Kickoff 的 QIcon::fromTheme("dsh-whale") 严格匹配 dsh-whale.svg，不识别
#     -symbolic 后缀；不命中 → 退化到 generic file icon。
#   * Tasks plasmoid 内部 fallback 路径查 dsh-whale-symbolic.svg，由 KIconEffect
#     用当前 colorscheme 前景色渲染 currentColor → 暗色配色下变白、亮色下变黑。
#
# 只装 hicolor 两个文件（普通色版 + symbolic）+ 两份命名归档，breeze / breeze-dark
# 不装：KDE 主题继承链 Inherits=hicolor 自动传播 fallback，无需在每个主题目录
# 都铺一份（之前每个 size 都装的方案 27 个文件是过度设计）。
#
# symbolic SVG 是 dsh-whale-black.svg 的派生品（fill=#000000 → currentColor），
# 源资产保持 black + white 两份；CMake 配置阶段用 execute_process + sed 派生到
# build dir，避免在 assets/ 引入第三份重复资产。不用 file(READ)+string(REPLACE)
# 是因为后者会把 ${...} 当成 CMake 变量插值——当前 SVG 无 $ 安全，但未来 SVG
# 加 & / \ 等字符可能踩坑；sed 是真正的字面替换，更稳。
execute_process(
    COMMAND sed "s/fill=\"#000000\"/fill=\"currentColor\"/g"
    INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/dsh-whale-black.svg"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/dsh-whale-symbolic.svg"
    RESULT_VARIABLE _dsh_sym_rv
)
if(NOT _dsh_sym_rv EQUAL 0)
    message(FATAL_ERROR
        "派生 dsh-whale-symbolic.svg 失败 (sed exit=${_dsh_sym_rv}); \
请检查 assets/dsh-whale-black.svg 是否存在且 sed 可用")
endif()
install(FILES assets/dsh-whale-black.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    RENAME dsh-whale.svg
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dsh-whale-symbolic.svg"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    RENAME dsh-whale-symbolic.svg
)
install(FILES assets/dsh-whale-black.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    RENAME dsh-whale-black.svg
)
install(FILES assets/dsh-whale-white.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    RENAME dsh-whale-white.svg
)
# systemd unit 与 autostart 需写到绝对系统路径（/usr/lib/systemd/system 与
# /etc/xdg/autostart）。CMake install 在当前用户运行时会因权限失败，因此
# 这两类文件由 packaging/install.sh（源码安装）与 packaging/post-install.sh
# （CPack 阶段调用）按当前用户权限复制。这样：
#   * 普通用户跑 cpack 能成功生成包
#   * 发行版包机制（deb / rpm / arch）可在 %post / postinst / package()
#     用 install -m 644 直接落到这些系统路径
install(PROGRAMS packaging/dsh-theme-export
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/dsh-desktop"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/dsh-desktop"
)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------

option(DSH_DESKTOP_BUILD_TESTS "编译单元测试" ON)
if(DSH_DESKTOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# CPack 跨发行版打包
# ---------------------------------------------------------------------------
# 使用方式：
#   cpack --config build/release/CPackConfig.cmake -G DEB
#   cpack --config build/release/CPackConfig.cmake -G RPM
#   cpack --config build/release/CPackConfig.cmake -G TGZ
set(CPACK_PACKAGE_NAME "dsh-desktop")
set(CPACK_PACKAGE_VENDOR "DSH Desktop")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DeepSeek Harness 原生 Linux 桌面包装器")
set(CPACK_PACKAGE_DESCRIPTION "基于 Qt WebEngine 的 DeepSeek Harness 原生 Linux 桌面包装器。")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.zh.md")
set(CPACK_PACKAGE_CONTACT "DSH Desktop 维护团队")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "DSH Desktop 维护团队")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-base-dev, qt6-webengine-dev, qt6-svg-dev, libxcb1 (>= 1.17), policykit-1, procps, npm, systemd")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.6, qt6-qtwebengine >= 6.6, libxcb >= 1.17, polkit, procps-ng, npm, systemd")
include(CPack)
