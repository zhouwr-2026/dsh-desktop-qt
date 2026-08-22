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
