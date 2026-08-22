// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 会话日志下载拦截器。
//
// 官方 DSH Web UI 通过注入 ``<a download="…">`` 并点击触发下载，在
// ``QWebEngineView`` 中体现为 ``QWebEngineProfile::downloadRequested`` 信
// 号。我们拦截这个信号并：
//   1. 弹出原生 ``QFileDialog::getSaveFileName``，默认文件名遵循官方约
//      定（``dsh-session-<id>.zip``）。
//   2. 通过异步 QNetworkReply 流式下载，并汇报 0..100 进度。
//   3. 期间显示 ``QProgressDialog``；完成后弹原生信息框 + KDE 通知。
//
// 只拦截官方会话日志导出路径（``/api/session.export``），其他下载请求透
// 传给 Chromium 自带的下载管理器（XDG 默认落到 ``~/Downloads``）。
//
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QProgressDialog;
class QWebEngineDownloadRequest;
QT_END_NAMESPACE

namespace dsh::web {

class DownloadInterceptor : public QObject {
    Q_OBJECT
public:
    explicit DownloadInterceptor(QObject* parent = nullptr);

    /// 判断 URL 是否属于本拦截器应当处理的"会话日志导出"。
    static bool shouldIntercept(const QUrl& url);

    /// 弹出原生保存对话框并在用户确认后启动下载。
    void handle(QWebEngineDownloadRequest* request);

    /// 从 ``?sessionId=...`` 之类的查询字符串派生默认文件名。
    static QString defaultFilename(const QUrl& url);

signals:
    void log(const QString& message);

private:
    struct InFlight {
        QPointer<QWebEngineDownloadRequest> request;
        QPointer<QProgressDialog> progress;
    };
    void finish(QWebEngineDownloadRequest* request);
    QList<InFlight> in_flight_;
};

}  // namespace dsh::web
