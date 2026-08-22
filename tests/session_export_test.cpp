// SPDX-License-Identifier: MIT
// @author zhouwr
// 端到端验证 DownloadInterceptor::shouldIntercept 能识别所有合法变体
#include <QTest>
#include <QUrl>
#include "../src/web/DownloadInterceptor.h"

class TestSessionExport : public QObject {
    Q_OBJECT
private slots:
    void shouldInterceptSessionExport();
    void shouldInterceptSessionExportWithExtraParams();
    void shouldNotInterceptOtherDownloads();
};

void TestSessionExport::shouldInterceptSessionExport() {
    QVERIFY(dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session.export?sessionId=abc")));
}

void TestSessionExport::shouldInterceptSessionExportWithExtraParams() {
    QVERIFY(dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session.export?sessionId=abc&includeDescendants=true")));
    QVERIFY(dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session.export?sessionId=xyz&format=zip")));
}

void TestSessionExport::shouldNotInterceptOtherDownloads() {
    // 只关心内部路径——拦截器只在 DSH web 上下文内被调用，外部链接根本
    // 不会进到 downloadRequested 信号（外部跳转被 LoopbackWebPage 拦截）。
    QVERIFY(!dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/some/other/path.zip")));
    QVERIFY(!dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/sessions")));
    QVERIFY(!dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session-export")));  // 相似但不是 export
    QVERIFY(!dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session.export.backup")));
    QVERIFY(!dsh::web::DownloadInterceptor::shouldIntercept(
        QUrl("http://127.0.0.1:3080/api/session.json")));
}

QTEST_GUILESS_MAIN(TestSessionExport)
#include "session_export_test.moc"
