// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::DesktopReleaseDownloader 的单元测试。
//
// 只覆盖纯函数路径，**不触发网络、不依赖真实缓存目录**：
//   * 资产选择（``selectBestAsset`` / ``assetScore``）；
//   * URL 校验（``isAllowedUrl``）；
//   * 文件名净化（``sanitizeFileName``）；
//   * 缓存路径存取（``setCachePath`` / ``cachePath``）。
//
// 异步下载（``start`` / ``finished``）需要网络与事件循环，测试刻意不触发，
// 以确保测试在无网环境下稳定、可复现。

#include <QString>
#include <QTest>
#include <QVector>

#include "../src/updater/DesktopReleaseDownloader.h"

using dsh::updater::DesktopReleaseAsset;
using dsh::updater::DesktopReleaseDownloader;

namespace {

/// 构造一个测试资产。
DesktopReleaseAsset makeAsset(const QString& name, const QString& url,
                              qint64 size = 1024) {
    DesktopReleaseAsset asset;
    asset.name = name;
    asset.url = url;
    asset.size = size;
    return asset;
}

const char* const kGoodUrl = "https://gitee.com/eruditeLoong/dsh-desktop-qt/releases/download/v0.1.0/dsh-desktop.AppImage";

}  // namespace

class TestDesktopReleaseDownloader : public QObject {
    Q_OBJECT
private slots:
    // --- URL 校验 ---
    void allowedUrlHttpsGitee();
    void allowedUrlRejectsHttp();
    void allowedUrlRejectsNonGiteeHost();
    void allowedUrlRejectsLookalikeHost();
    void allowedUrlRejectsEmptyAndRelative();
    void allowedUrlAllowsSubdomainAndPort();

    // --- 文件名净化 ---
    void sanitizePlainName();
    void sanitizeRejectsEmpty();
    void sanitizeRejectsSeparators();
    void sanitizeRejectsDotEntries();
    void sanitizeRejectsTraversal();
    void sanitizeRejectsNul();
    void sanitizeRejectsWhitespace();

    // --- 资产打分 / 选择 ---
    void assetScorePrefersAppImage();
    void assetScoreSkipsAuxiliary();
    void assetScoreSkipsBadUrl();
    void selectBestPrefersAppImage();
    void selectBestSkipsAuxiliary();
    void selectBestPrefersMatchingArch();
    void selectBestPrefersMatchingArchOnArm();
    void selectBestReturnsNullWhenEmpty();
    void selectBestReturnsNullWhenOnlyAuxiliary();
    void selectBestSkipsBadUrlAsset();

    // --- 缓存路径 ---
    void cachePathExplicit();
};

void TestDesktopReleaseDownloader::allowedUrlHttpsGitee() {
    QString reason;
    QVERIFY2(DesktopReleaseDownloader::isAllowedUrl(
                 QString::fromLatin1(kGoodUrl), &reason), qPrintable(reason));
    QVERIFY(DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://gitee.com/a/b"), &reason));
    QVERIFY(DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("HTTPS://Gitee.com/a/b"),
                &reason));  // 大小写不敏感
}

void TestDesktopReleaseDownloader::allowedUrlRejectsHttp() {
    QString reason;
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("http://gitee.com/a/b"), &reason));
    QVERIFY(!reason.isEmpty());
}

void TestDesktopReleaseDownloader::allowedUrlRejectsNonGiteeHost() {
    QString reason;
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://example.com/a"), &reason));
    QVERIFY(!reason.isEmpty());
}

void TestDesktopReleaseDownloader::allowedUrlRejectsLookalikeHost() {
    QString reason;
    // 主机名恰好以 gitee.com 结尾但前面有额外子域，都不等于 gitee.com / *.gitee.com。
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://gitee.com.evil.com/a"), &reason));
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://evilgitee.com/a")));
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://gitee.com.cn/a")));
}

void TestDesktopReleaseDownloader::allowedUrlRejectsEmptyAndRelative() {
    QString reason;
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(QString(), &reason));
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("gitee.com/a")));  // 相对路径：无 scheme
    QVERIFY(!DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://")));  // 无主机
}

void TestDesktopReleaseDownloader::allowedUrlAllowsSubdomainAndPort() {
    QString reason;
    QVERIFY(DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://gitee.com:8443/path"), &reason));
    QVERIFY(DesktopReleaseDownloader::isAllowedUrl(
                QStringLiteral("https://gitee.com/a?x=1#frag")));
}

void TestDesktopReleaseDownloader::sanitizePlainName() {
    QString error;
    const QString name = QStringLiteral("dsh-desktop-1.0.0.AppImage");
    QCOMPARE(DesktopReleaseDownloader::sanitizeFileName(name, &error), name);
    QVERIFY(error.isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsEmpty() {
    QString error;
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(QString(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsSeparators() {
    QString error;
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("a/b"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("a\\b")).isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsDotEntries() {
    QString error;
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("."), &error).isEmpty());
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("..")).isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsTraversal() {
    QString error;
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("../evil"), &error).isEmpty());
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("sub/../evil")).isEmpty());
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("..\\evil")).isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsNul() {
    QString error;
    const QString bad = QStringLiteral("evil") + QLatin1Char('\0') + QStringLiteral("name");
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(bad, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestDesktopReleaseDownloader::sanitizeRejectsWhitespace() {
    QString error;
    QVERIFY(DesktopReleaseDownloader::sanitizeFileName(
                QStringLiteral("  spaced.AppImage "), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestDesktopReleaseDownloader::assetScorePrefersAppImage() {
    const DesktopReleaseAsset appimage =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QString::fromLatin1(kGoodUrl));
    const DesktopReleaseAsset deb =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.deb"),
                  QString::fromLatin1(kGoodUrl));
    const DesktopReleaseAsset rpm =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.rpm"),
                  QString::fromLatin1(kGoodUrl));

    QVERIFY(DesktopReleaseDownloader::assetScore(appimage, "linux", "x86_64")
            > DesktopReleaseDownloader::assetScore(deb, "linux", "x86_64"));
    QVERIFY(DesktopReleaseDownloader::assetScore(deb, "linux", "x86_64")
            > DesktopReleaseDownloader::assetScore(rpm, "linux", "x86_64"));
}

void TestDesktopReleaseDownloader::assetScoreSkipsAuxiliary() {
    const DesktopReleaseAsset appimage =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QString::fromLatin1(kGoodUrl));
    const DesktopReleaseAsset sha = makeAsset(
        QStringLiteral("dsh-desktop-1.0.0.AppImage.sha256"), QString::fromLatin1(kGoodUrl));
    const DesktopReleaseAsset sig =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage.asc"),
                  QString::fromLatin1(kGoodUrl));

    QVERIFY(DesktopReleaseDownloader::assetScore(appimage, "linux", "x86_64")
            > DesktopReleaseDownloader::assetScore(sha, "linux", "x86_64"));
    QVERIFY(DesktopReleaseDownloader::assetScore(appimage, "linux", "x86_64")
            > DesktopReleaseDownloader::assetScore(sig, "linux", "x86_64"));
}

void TestDesktopReleaseDownloader::assetScoreSkipsBadUrl() {
    const DesktopReleaseAsset httpAsset =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QStringLiteral("http://gitee.com/x"));
    const DesktopReleaseAsset httpsAsset =
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QString::fromLatin1(kGoodUrl));
    QVERIFY(DesktopReleaseDownloader::assetScore(httpsAsset, "linux", "x86_64")
            > DesktopReleaseDownloader::assetScore(httpAsset, "linux", "x86_64"));
}

void TestDesktopReleaseDownloader::selectBestPrefersAppImage() {
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.rpm"), QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"), QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.deb"), QString::fromLatin1(kGoodUrl)),
    };
    const DesktopReleaseAsset* best =
        DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64");
    QVERIFY(best != nullptr);
    QCOMPARE(best->name, QStringLiteral("dsh-desktop-1.0.0.AppImage"));
}

void TestDesktopReleaseDownloader::selectBestSkipsAuxiliary() {
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage.sha256"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage.asc"),
                  QString::fromLatin1(kGoodUrl)),
    };
    const DesktopReleaseAsset* best =
        DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64");
    QVERIFY(best != nullptr);
    QCOMPARE(best->name, QStringLiteral("dsh-desktop-1.0.0.AppImage"));
}

void TestDesktopReleaseDownloader::selectBestPrefersMatchingArch() {
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0-aarch64.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0-x86_64.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
    };
    const DesktopReleaseAsset* best =
        DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64");
    QVERIFY(best != nullptr);
    QCOMPARE(best->name, QStringLiteral("dsh-desktop-1.0.0-x86_64.AppImage"));
}

void TestDesktopReleaseDownloader::selectBestPrefersMatchingArchOnArm() {
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0-x86_64.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0-aarch64.AppImage"),
                  QString::fromLatin1(kGoodUrl)),
    };
    const DesktopReleaseAsset* best =
        DesktopReleaseDownloader::selectBestAsset(assets, "linux", "aarch64");
    QVERIFY(best != nullptr);
    QCOMPARE(best->name, QStringLiteral("dsh-desktop-1.0.0-aarch64.AppImage"));
}

void TestDesktopReleaseDownloader::selectBestReturnsNullWhenEmpty() {
    const QVector<DesktopReleaseAsset> assets;
    QVERIFY(DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64") == nullptr);
}

void TestDesktopReleaseDownloader::selectBestReturnsNullWhenOnlyAuxiliary() {
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage.sha256"),
                  QString::fromLatin1(kGoodUrl)),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage.asc"),
                  QString::fromLatin1(kGoodUrl)),
    };
    QVERIFY(DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64") == nullptr);
}

void TestDesktopReleaseDownloader::selectBestSkipsBadUrlAsset() {
    // AppImage 是好格式但 URL 是 http（不合法），应被跳过，改选合法的 deb。
    const QVector<DesktopReleaseAsset> assets = {
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.AppImage"),
                  QStringLiteral("http://gitee.com/x")),
        makeAsset(QStringLiteral("dsh-desktop-1.0.0.deb"),
                  QString::fromLatin1(kGoodUrl)),
    };
    const DesktopReleaseAsset* best =
        DesktopReleaseDownloader::selectBestAsset(assets, "linux", "x86_64");
    QVERIFY(best != nullptr);
    QCOMPARE(best->name, QStringLiteral("dsh-desktop-1.0.0.deb"));
}

void TestDesktopReleaseDownloader::cachePathExplicit() {
    const QString custom = QStringLiteral("/tmp/dsh-test-cache");
    DesktopReleaseDownloader downloader(custom);
    QCOMPARE(downloader.cachePath(), custom);

    downloader.setCachePath(QStringLiteral("/tmp/dsh-test-cache-2"));
    QCOMPARE(downloader.cachePath(), QStringLiteral("/tmp/dsh-test-cache-2"));

    // 默认构造：未显式指定 -> 空串（后续 start 会用 QStandardPaths::CacheLocation）。
    DesktopReleaseDownloader defaultDownloader;
    QVERIFY(defaultDownloader.cachePath().isEmpty());
    QVERIFY(defaultDownloader.lastError().isEmpty());
    QVERIFY(!defaultDownloader.isActive());
}

QTEST_GUILESS_MAIN(TestDesktopReleaseDownloader)
#include "test_desktop_release_downloader.moc"
