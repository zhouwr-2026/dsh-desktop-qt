// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::compareVersions 的单元测试。

#include <QTest>
#include <QString>

#include "../src/updater/Updater.h"

using dsh::updater::compareVersions;

class TestSemver : public QObject {
    Q_OBJECT
private slots:
    void parseStableBeatsRc();
    void rcOrdering();
    void patchUpgrade();
    void garbageCompareIsZero();
    void missingBuildMetadata();
    void numericVsAlphabetic();
};

void TestSemver::parseStableBeatsRc() {
    // 0.1.0 > 0.1.0-rc.9（稳定版高于同号预发布版）
    QVERIFY(compareVersions("0.1.0", "0.1.0-rc.9") > 0);
    QVERIFY(compareVersions("0.1.0-rc.9", "0.1.0") < 0);
}

void TestSemver::rcOrdering() {
    QVERIFY(compareVersions("0.1.0-rc.8", "0.1.0-rc.7") > 0);
    QVERIFY(compareVersions("0.1.0-rc.7", "0.1.0-rc.8") < 0);
    QVERIFY(compareVersions("0.1.0-rc.7", "0.1.0-rc.7") == 0);
}

void TestSemver::patchUpgrade() {
    QVERIFY(compareVersions("1.2.4", "1.2.3") > 0);
    QVERIFY(compareVersions("1.2.3", "1.2.4") < 0);
}

void TestSemver::garbageCompareIsZero() {
    // 无法解析时退化为 0（不安全但可预期）
    QCOMPARE(compareVersions("not-a-version", "0.1.0"), 0);
    QCOMPARE(compareVersions("0.1.0", "garbage"), 0);
}

void TestSemver::missingBuildMetadata() {
    // 构建元数据不影响优先级（SemVer 2.0 §10）
    QCOMPARE(compareVersions("1.0.0+build.1", "1.0.0"), 0);
    QCOMPARE(compareVersions("1.0.0+build.1", "1.0.0+build.2"), 0);
}

void TestSemver::numericVsAlphabetic() {
    // 数字标识符 < 非数字标识符
    QVERIFY(compareVersions("1.0.0-alpha.1", "1.0.0-alpha.beta") < 0);
    QVERIFY(compareVersions("1.0.0-alpha.beta", "1.0.0-alpha.1") > 0);
    // 数字与数字按大小比
    QVERIFY(compareVersions("1.0.0-alpha.10", "1.0.0-alpha.2") > 0);
}

QTEST_GUILESS_MAIN(TestSemver)
#include "test_semver.moc"