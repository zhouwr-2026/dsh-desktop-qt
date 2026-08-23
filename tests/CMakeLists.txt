# @author zhouwr
#
# DSH Desktop 单元测试
#
# 每个 test_*.cpp 编译成独立可执行，ctest 一次性跑完。

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# semver 测试：只需要 core，不依赖 Qt Widgets
add_executable(dsh-semver-test test_semver.cpp)
target_link_libraries(dsh-semver-test PRIVATE
    Qt6::Core Qt6::Test
    dsh_desktop_core
)
target_include_directories(dsh-semver-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# 桌面版本检查器测试：纯解析逻辑，不触发网络；仅依赖 Qt Core + core 库
add_executable(dsh-desktop-version-test test_desktop_version_checker.cpp)
target_link_libraries(dsh-desktop-version-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-desktop-version-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# loopback 测试：需要 Widgets + WebEngine（QWebEngineProfile 间接依赖）
add_executable(dsh-loopback-test test_loopback_page.cpp)
target_link_libraries(dsh-loopback-test PRIVATE
    Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::WebEngineCore Qt6::WebEngineWidgets
    dsh_desktop_core
)
target_include_directories(dsh-loopback-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# session-export 拦截识别测试
add_executable(dsh-session-export-test session_export_test.cpp)
target_link_libraries(dsh-session-export-test PRIVATE
    Qt6::Core Qt6::Network Qt6::Test
    dsh_desktop_core
)
target_include_directories(dsh-session-export-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_executable(dsh-logo-test
    test_logo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons.qrc
)
target_link_libraries(dsh-logo-test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    dsh_desktop_core
)
target_include_directories(dsh-logo-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(NAME semver          COMMAND dsh-semver-test)
add_test(NAME desktop-version COMMAND dsh-desktop-version-test)
add_test(NAME loopback        COMMAND dsh-loopback-test)
add_test(NAME session-export  COMMAND dsh-session-export-test)
add_test(NAME logo            COMMAND dsh-logo-test)
set_tests_properties(logo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(dsh-rendering-policy-test test_rendering_policy.cpp)
target_link_libraries(dsh-rendering-policy-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-rendering-policy-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME rendering-policy COMMAND dsh-rendering-policy-test)

# service 发现解析测试：纯 Qt Core，不依赖 QProcess/systemctl
add_executable(dsh-service-test test_service_discovery.cpp)
target_link_libraries(dsh-service-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-service-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME service-discovery COMMAND dsh-service-test)

# service 所有权记录/一致性测试：纯 Qt Core，依赖 dsh_desktop_core
add_executable(dsh-service-ownership-test test_service_ownership.cpp)
target_link_libraries(dsh-service-ownership-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-service-ownership-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME service-ownership COMMAND dsh-service-ownership-test)

# backend Status 元数据默认值与 applyServiceMetadata 纯映射测试：纯 Qt Core，
# 不启动进程、不调用 systemctl/journalctl，可稳定运行。
add_executable(dsh-backend-status-test test_backend_status.cpp)
target_link_libraries(dsh-backend-status-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-backend-status-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME backend-status COMMAND dsh-backend-status-test)

# 统一更新计划模型测试：纯数据合并，不触发网络/进程/UI，可稳定运行。
add_executable(dsh-update-plan-test test_update_plan.cpp)
target_link_libraries(dsh-update-plan-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-update-plan-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME update-plan COMMAND dsh-update-plan-test)

# ServiceUnitBuilder 测试：纯 Qt Core，校验 dsh web 单元文本生成、非法输入
# 拒绝与 systemd 安全转义；不触发文件/进程操作。
add_executable(dsh-service-unit-test test_service_unit_builder.cpp)
target_link_libraries(dsh-service-unit-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-service-unit-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME service-unit COMMAND dsh-service-unit-test)

# ServiceProvisioner 测试：路径选择 / 既有单元拒绝 / 原子写入 / 所有权记录 /
# 非法 spec 拒绝 / 系统级无写入器拒绝 / reload-enable 仅对新写入放行。
# 只写临时目录，绝不触碰真实 systemd 或 /etc。
add_executable(dsh-service-provisioner-test test_service_provisioner.cpp)
target_link_libraries(dsh-service-provisioner-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-service-provisioner-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME service-provisioner COMMAND dsh-service-provisioner-test)

# DshServiceManager 测试：纯函数（命令参数构建 / 提权 / unit 名校验 /
# timeout-result 映射 / 归属策略）+ 用测试替身驱动真实 QProcess 的异步路径。
add_executable(dsh-service-manager-test test_service_manager.cpp)
target_link_libraries(dsh-service-manager-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-service-manager-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME service-manager COMMAND dsh-service-manager-test)

# 安装决策模型测试：纯数据/纯函数，不触发网络/进程/UI/systemd，可稳定运行。
add_executable(dsh-installation-plan-test test_installation_plan.cpp)
target_link_libraries(dsh-installation-plan-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-installation-plan-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME installation-plan COMMAND dsh-installation-plan-test)

# 卸载决策模型测试：纯数据/纯函数，不触发网络/进程/UI/systemd/磁盘，可稳定运行。
add_executable(dsh-uninstall-plan-test test_uninstall_plan.cpp)
target_link_libraries(dsh-uninstall-plan-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-uninstall-plan-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME uninstall-plan COMMAND dsh-uninstall-plan-test)

# 自更新助手测试：纯校验/原子替换逻辑，仅依赖 Qt Core + 助手静态库。
add_executable(dsh-update-helper-test test_update_helper.cpp)
target_link_libraries(dsh-update-helper-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_updater_helper
)
target_include_directories(dsh-update-helper-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME update-helper COMMAND dsh-update-helper-test)

# 桌面发布附件下载器测试：纯函数（资产选择 / URL 校验 / 文件名净化 /
# 缓存路径存取），仅依赖 Qt Core + Network + 核心库，不触发网络。
add_executable(dsh-release-downloader-test test_desktop_release_downloader.cpp)
target_link_libraries(dsh-release-downloader-test PRIVATE
    Qt6::Core Qt6::Network Qt6::Test dsh_desktop_core
)
target_include_directories(dsh-release-downloader-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
add_test(NAME release-downloader COMMAND dsh-release-downloader-test)

# 构建版本头测试：编译期引用 CMake 生成的 BuildVersion.h（含 DSH_DESKTOP_VERSION），
# 运行期校验其为非空且合法的 SemVer。
add_executable(dsh-build-version-test test_build_version.cpp)
target_link_libraries(dsh-build-version-test PRIVATE
    Qt6::Core Qt6::Test dsh_desktop_core
)
# BuildVersion.h 通过 dsh_desktop_core 的 PUBLIC include 目录暴露，无需额外路径。
add_test(NAME build-version COMMAND dsh-build-version-test)
