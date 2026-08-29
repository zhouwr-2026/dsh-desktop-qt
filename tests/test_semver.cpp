// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::compareVersions 的单元测试。

#include <QTest>
#include <QString>

#include "../src/updater/Updater.h"

using dsh::updater::compareVersions;
using dsh::updater::checkMinimumDshVersion;
using dsh::updater::MinimumVersionCheck;

class TestSemver : public QObject {
    Q_OBJECT
private slots:
    void parseStableBeatsRc();
    void rcOrdering();
    void patchUpgrade();
    void garbageCompareIsZero();
    void missingBuildMetadata();
    void numericVsAlphabetic();
    void rejectsInvalidSemVer();
    void comparesLargeNumericIdentifiers();
    void minimumDshVersionAtBoundary();
    void minimumDshVersionBelowThreshold();
    void minimumDshVersionEmpty();
    void minimumDshVersionInvalid();
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

void TestSemver::rejectsInvalidSemVer() {
    QCOMPARE(compareVersions("01.0.0", "1.0.0"), 0);
    QCOMPARE(compareVersions("1.0.0-alpha..1", "1.0.0"), 0);
    QCOMPARE(compareVersions("1.0.0-alpha.01", "1.0.0-alpha.1"), 0);
    QCOMPARE(compareVersions("1.0.0+build..1", "1.0.0"), 0);
}

void TestSemver::comparesLargeNumericIdentifiers() {
    QVERIFY(compareVersions("999999999999999999999.0.0",
                            "999999999999999999998.0.0") > 0);
    QVERIFY(compareVersions("1.0.0-alpha.999999999999999999999",
                            "1.0.0-alpha.2") > 0);
}

void TestSemver::minimumDshVersionAtBoundary() {
    // 恰好等于最低版本：放行
    QCOMPARE(checkMinimumDshVersion(QString::fromLatin1(
                 dsh::updater::kMinimumDshVersion)),
             MinimumVersionCheck::Ok);
    // 稳定版高于最低版本：放行
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("0.1.0")),
             MinimumVersionCheck::Ok);
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("1.2.3")),
             MinimumVersionCheck::Ok);
}

void TestSemver::minimumDshVersionBelowThreshold() {
    // 低于 rc.7 的预发布版：拒绝
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("0.1.0-rc.6")),
             MinimumVersionCheck::TooOld);
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("0.1.0-rc.1")),
             MinimumVersionCheck::TooOld);
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("0.0.99")),
             MinimumVersionCheck::TooOld);
}

void TestSemver::minimumDshVersionEmpty() {
    // 探测不到版本（dsh 不在 PATH / 无 package.json）：Unknown，不阻塞
    QCOMPARE(checkMinimumDshVersion(QString()),
             MinimumVersionCheck::Unknown);
}

void TestSemver::minimumDshVersionInvalid() {
    // package.json 损毁 / dsh --version 输出非 SemVer：Invalid，调用方按
    // Unknown 处理（仅记录日志、不阻塞启动）。
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("not-a-version")),
             MinimumVersionCheck::Invalid);
    QCOMPARE(checkMinimumDshVersion(QStringLiteral("01.0.0")),  // 前导零非法
             MinimumVersionCheck::Invalid);
}

QTEST_GUILESS_MAIN(TestSemver)
#include "test_semver.moc"