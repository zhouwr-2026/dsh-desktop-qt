// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 构建版本头的单元测试。
//
// ``BuildVersion.h`` 由 CMake 根据 ``PROJECT_VERSION`` 自动生成（包含
// ``DSH_DESKTOP_VERSION`` 宏）。本测试在编译期引用该宏（编译使用）并在运行期
// 断言它是一个非空、合法 SemVer 的版本号，从而保证所有使用同一宏的主程序、
// 自更新助手、About 对话框与 User-Agent 版本串拥有一个可信的单一事实来源。

#include <QTest>

#include "../src/updater/Updater.h"
#include "BuildVersion.h"

using dsh::updater::isValidSemVer;

class TestBuildVersion : public QObject {
    Q_OBJECT
private slots:
    void macroIsDefinedAndNonEmpty();
    void macroIsValidSemVer();
};

void TestBuildVersion::macroIsDefinedAndNonEmpty() {
    // 编译期已引用宏；运行期校验它确实被 CMake 填充。
    QVERIFY(!QString::fromLatin1(DSH_DESKTOP_VERSION).isEmpty());
}

void TestBuildVersion::macroIsValidSemVer() {
    // 版本号必须满足严格 SemVer 2.0（与更新检查器共用同一套校验）。
    QVERIFY2(isValidSemVer(QString::fromLatin1(DSH_DESKTOP_VERSION)),
             qPrintable(QString::fromLatin1(DSH_DESKTOP_VERSION)));
}

QTEST_GUILESS_MAIN(TestBuildVersion)
#include "test_build_version.moc"
