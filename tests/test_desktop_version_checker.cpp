// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::DesktopVersionChecker 的单元测试。
//
// 聚焦纯解析逻辑（不触发网络）：有效发布、缺失/非法 tag、非 2xx 与空 JSON。
// SemVer 复用 ``Updater::compareVersions`` / ``isValidSemVer``。

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "../src/updater/DesktopVersionChecker.h"
#include "../src/updater/Updater.h"

using dsh::updater::DesktopReleaseInfo;
using dsh::updater::DesktopVersionChecker;
using dsh::updater::compareVersions;
using dsh::updater::isValidSemVer;
using dsh::updater::VersionCheckStatus;

namespace {

QByteArray singleReleaseJson(const char* tag, bool prerelease = false) {
    QJsonObject obj;
    obj.insert("tag_name", QString::fromUtf8(tag));
    obj.insert("name", QString::fromUtf8(tag));
    obj.insert("body", QStringLiteral("release notes"));
    obj.insert("prerelease", prerelease);
    QJsonArray assets;
    assets.append(QJsonObject{
        {"name", QStringLiteral("dsh-desktop-0.2.0.AppImage")},
        {"browser_download_url", QStringLiteral("https://gitee.com/example/asset")},
        {"size", 12345678.0},
    });
    obj.insert("assets", assets);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

}  // namespace

class TestDesktopVersionChecker : public QObject {
    Q_OBJECT
private slots:
    void validReleaseObject();
    void validReleaseArrayPicksHighest();
    void missingTagIsInvalid();
    void invalidTagIsInvalid();
    void prereleaseFlagParsed();
    void assetsParsed();
    void emptyBodyIsNoRelease();
    void nullJsonIsNoRelease();
    void emptyArrayIsNoRelease();
    void malformedJsonIsInvalid();
    void non2xxIsInvalid();
    void notFoundIsNoRelease();
    void isUpgradeSemver();
};

void TestDesktopVersionChecker::validReleaseObject() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s =
        DesktopVersionChecker::parseRelease(singleReleaseJson("v0.2.0"), info);
    QCOMPARE(s, VersionCheckStatus::Ok);
    QCOMPARE(info.tagName, QStringLiteral("v0.2.0"));
    QCOMPARE(info.name, QStringLiteral("v0.2.0"));
    QCOMPARE(info.body, QStringLiteral("release notes"));
    QCOMPARE(info.prerelease, false);
    QCOMPARE(info.assets.size(), 1);
}

void TestDesktopVersionChecker::validReleaseArrayPicksHighest() {
    QJsonArray arr;
    arr.append(QJsonObject{ {"tag_name", QStringLiteral("v0.9.0")},
                            {"name", QStringLiteral("v0.9.0")},
                            {"body", QStringLiteral("old")},
                            {"assets", QJsonArray()}, });
    arr.append(QJsonObject{ {"tag_name", QStringLiteral("v1.0.0")},
                            {"name", QStringLiteral("v1.0.0")},
                            {"body", QStringLiteral("new")},
                            {"assets", QJsonArray()}, });
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);

    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease(json, info);
    QCOMPARE(s, VersionCheckStatus::Ok);
    QCOMPARE(info.tagName, QStringLiteral("v1.0.0"));
    QCOMPARE(info.body, QStringLiteral("new"));
}

void TestDesktopVersionChecker::missingTagIsInvalid() {
    const QByteArray json =
        R"({"name":"no-tag","body":"x","assets":[]})";
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease(json, info);
    QCOMPARE(s, VersionCheckStatus::InvalidResponse);
}

void TestDesktopVersionChecker::invalidTagIsInvalid() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s =
        DesktopVersionChecker::parseRelease(singleReleaseJson("not-a-version"), info);
    QCOMPARE(s, VersionCheckStatus::InvalidResponse);
}

void TestDesktopVersionChecker::prereleaseFlagParsed() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s =
        DesktopVersionChecker::parseRelease(singleReleaseJson("v0.2.0-rc.1", true), info);
    QCOMPARE(s, VersionCheckStatus::Ok);
    QCOMPARE(info.prerelease, true);
    QCOMPARE(info.tagName, QStringLiteral("v0.2.0-rc.1"));
}

void TestDesktopVersionChecker::assetsParsed() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s =
        DesktopVersionChecker::parseRelease(singleReleaseJson("v0.2.0"), info);
    QCOMPARE(s, VersionCheckStatus::Ok);
    QCOMPARE(info.assets.size(), 1);
    QCOMPARE(info.assets[0].name, QStringLiteral("dsh-desktop-0.2.0.AppImage"));
    QCOMPARE(info.assets[0].url,
             QStringLiteral("https://gitee.com/example/asset"));
    QCOMPARE(info.assets[0].size, static_cast<qint64>(12345678));
}

void TestDesktopVersionChecker::emptyBodyIsNoRelease() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease(QByteArray(), info);
    QCOMPARE(s, VersionCheckStatus::NoRelease);
}

void TestDesktopVersionChecker::nullJsonIsNoRelease() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease("null", info);
    QCOMPARE(s, VersionCheckStatus::NoRelease);
}

void TestDesktopVersionChecker::emptyArrayIsNoRelease() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease("[]", info);
    QCOMPARE(s, VersionCheckStatus::NoRelease);
}

void TestDesktopVersionChecker::malformedJsonIsInvalid() {
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseRelease("not json {", info);
    QCOMPARE(s, VersionCheckStatus::InvalidResponse);
}

void TestDesktopVersionChecker::non2xxIsInvalid() {
    const QByteArray json = singleReleaseJson("v0.2.0");
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseHttpResponse(500, json, info);
    QCOMPARE(s, VersionCheckStatus::InvalidResponse);
}

void TestDesktopVersionChecker::notFoundIsNoRelease() {
    const QByteArray json = singleReleaseJson("v0.2.0");
    DesktopReleaseInfo info;
    const VersionCheckStatus s = DesktopVersionChecker::parseHttpResponse(404, json, info);
    QCOMPARE(s, VersionCheckStatus::NoRelease);
}

void TestDesktopVersionChecker::isUpgradeSemver() {
    DesktopReleaseInfo release;
    const VersionCheckStatus s =
        DesktopVersionChecker::parseRelease(singleReleaseJson("v1.2.3"), release);
    QCOMPARE(s, VersionCheckStatus::Ok);

    QVERIFY(DesktopVersionChecker::isUpgrade(release, QStringLiteral("1.2.2")));
    QVERIFY(!DesktopVersionChecker::isUpgrade(release, QStringLiteral("1.2.3")));
    QVERIFY(!DesktopVersionChecker::isUpgrade(release, QStringLiteral("2.0.0")));
    // 本地版本非法时不视为升级
    QVERIFY(!DesktopVersionChecker::isUpgrade(release, QStringLiteral("garbage")));
    // SemVer 校验工具本身
    QVERIFY(isValidSemVer(QStringLiteral("v0.2.0")));
    QVERIFY(!isValidSemVer(QStringLiteral("not-a-version")));
    QVERIFY(compareVersions(QStringLiteral("v1.2.3"), QStringLiteral("1.2.2")) > 0);
}

QTEST_GUILESS_MAIN(TestDesktopVersionChecker)
#include "test_desktop_version_checker.moc"
