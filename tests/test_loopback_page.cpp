// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::web::LoopbackWebPage::isInternal 的纯函数测试。避免构造完整的
// QWebEngineProfile（offscreen 下会触发 Chromium 的 zygote 启动失败）。

#include <QTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QFile>
#include <QSignalSpy>

#include "../src/backend/Backend.h"
#include "../src/theme/ThemeWatcher.h"
#include "../src/web/LoopbackWebPage.h"

class TestLoopback : public QObject {
    Q_OBJECT
private slots:
    void loopbackHostsAreInternal();
    void externalHostsAreExternal();
    void builtInSchemesAreInternal();
    void httpUrlsDistinguished();
    void configuredRemoteOriginIsInternal();
    void remoteOriginCannotNavigateToLoopback();
    void clipboardOriginMustMatchApplication();
    void backendUsesExplicitUrl();
    void loopbackUrlUsesManagedBackend();
    void forcedThemeSurvivesWatcherStart();
    void kdeLookAndFeelOverridesColorScheme();
    void runtimeThemeMarkerDrivesChanges();
};

void TestLoopback::loopbackHostsAreInternal() {
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("http://127.0.0.1:3080/")));
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("http://localhost:9000/api")));
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("http://[::1]:3080/")));
}

void TestLoopback::externalHostsAreExternal() {
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(QUrl("https://example.com/article")));
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(QUrl("http://github.com/foo/bar")));
}

void TestLoopback::builtInSchemesAreInternal() {
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("data:text/plain,hello")));
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("about:blank")));
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("mailto:foo@bar.com")));
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("tel:+86-138-0000-0000")));
}

void TestLoopback::httpUrlsDistinguished() {
    // 协议即使相同，宿主仍决定归属
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(QUrl("http://127.0.0.1:3080/path?q=1")));
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(QUrl("https://127.0.0.2:3080/path")));
}

void TestLoopback::configuredRemoteOriginIsInternal() {
    const QUrl applicationUrl("https://dsh.example.com:8443/app");
    QVERIFY(dsh::web::LoopbackWebPage::isInternal(
        QUrl("https://dsh.example.com:8443/session/1"), applicationUrl));
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(
        QUrl("https://dsh.example.com/session/1"), applicationUrl));
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(
        QUrl("http://dsh.example.com:8443/session/1"), applicationUrl));
}

void TestLoopback::remoteOriginCannotNavigateToLoopback() {
    const QUrl applicationUrl("https://dsh.example.com:8443/app");
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(
        QUrl("http://127.0.0.1:8080/admin"), applicationUrl));
    QVERIFY(!dsh::web::LoopbackWebPage::isInternal(
        QUrl("http://localhost:9090/"), applicationUrl));
}

void TestLoopback::clipboardOriginMustMatchApplication() {
    const QUrl applicationUrl("http://127.0.0.1:3080/app");
    QVERIFY(dsh::web::LoopbackWebPage::isSameOrigin(
        QUrl("http://127.0.0.1:3080"), applicationUrl));
    QVERIFY(!dsh::web::LoopbackWebPage::isSameOrigin(
        QUrl("http://localhost:3080"), applicationUrl));
    QVERIFY(!dsh::web::LoopbackWebPage::isSameOrigin(
        QUrl("http://127.0.0.1:9090"), applicationUrl));
}

void TestLoopback::backendUsesExplicitUrl() {
    const QString explicitUrl = QStringLiteral("https://dsh.example.com:8443");
    const auto backend = dsh::backend::Backend::createForHost(explicitUrl);
    QCOMPARE(backend->url(), explicitUrl);
    QCOMPARE(backend->mode(), dsh::backend::Mode::External);
}

void TestLoopback::loopbackUrlUsesManagedBackend() {
    const auto backend = dsh::backend::Backend::createForHost(
        QStringLiteral("http://127.0.0.1:3080"));
    QVERIFY(backend->mode() != dsh::backend::Mode::External);
}

void TestLoopback::forcedThemeSurvivesWatcherStart() {
    dsh::theme::ThemeWatcher watcher;
    watcher.setForcedScheme(QStringLiteral("dark"));
    QCOMPARE(watcher.current(), QStringLiteral("dark"));
    watcher.start();
    QCOMPARE(watcher.current(), QStringLiteral("dark"));
    watcher.stop();
}

void TestLoopback::kdeLookAndFeelOverridesColorScheme() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString stalePath = directory.filePath(QStringLiteral("plasmarc"));
    const QString authoritativePath = directory.filePath(QStringLiteral("kdeglobals"));

    QFile stale(stalePath);
    QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Text));
    stale.write("[General]\nColorScheme=BreezeLight\n");
    stale.close();

    QFile authoritative(authoritativePath);
    QVERIFY(authoritative.open(QIODevice::WriteOnly | QIODevice::Text));
    authoritative.write("[KDE]\nLookAndFeelPackage=org.kde.breezedark.desktop\n");
    authoritative.close();

    QCOMPARE(dsh::theme::ThemeWatcher::detectKdeTheme(
                 {stalePath, authoritativePath}),
             QStringLiteral("dark"));
}

void TestLoopback::runtimeThemeMarkerDrivesChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString markerPath = directory.filePath(QStringLiteral("theme"));
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Text));
    marker.write("light\n");
    marker.close();

    qputenv("DSH_DESKTOP_THEME_FILE", markerPath.toUtf8());
    dsh::theme::ThemeWatcher watcher;
    watcher.start();
    QCOMPARE(watcher.current(), QStringLiteral("light"));
    QSignalSpy spy(&watcher, &dsh::theme::ThemeWatcher::schemeChanged);

    QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    marker.write("dark\n");
    marker.close();
    watcher.refresh();

    QCOMPARE(watcher.current(), QStringLiteral("dark"));
    QCOMPARE(spy.count(), 1);
    watcher.stop();
    qunsetenv("DSH_DESKTOP_THEME_FILE");
}

QTEST_GUILESS_MAIN(TestLoopback)
#include "test_loopback_page.moc"
