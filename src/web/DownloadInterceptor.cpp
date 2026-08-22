// SPDX-License-Identifier: MIT
// @author zhouwr
#include "DownloadInterceptor.h"

#include "../util/Notify.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QWebEngineDownloadRequest>
#include <QWidget>

namespace dsh::web {

namespace {
// 官方会话日志导出端点路径
constexpr const char* kSessionPath = "/api/session.export";
}

DownloadInterceptor::DownloadInterceptor(QObject* parent) : QObject(parent) {}

bool DownloadInterceptor::shouldIntercept(const QUrl& url) {
    return url.path() == QString::fromLatin1(kSessionPath);
}

QString DownloadInterceptor::defaultFilename(const QUrl& url) {
    const QString q = url.query();
    int idIdx = q.indexOf(QStringLiteral("sessionId="));
    if (idIdx >= 0) {
        int start = idIdx + QStringLiteral("sessionId=").size();
        int end = q.indexOf('&', start);
        QString id = (end < 0) ? q.mid(start) : q.mid(start, end - start);
        QString safe;
        safe.reserve(id.size());
        for (QChar c : id) {
            if (c.isLetterOrNumber() || c == '-' || c == '_') safe.append(c);
            else safe.append('_');
        }
        if (!safe.isEmpty()) return QStringLiteral("dsh-session-%1.zip").arg(safe);
    }
    QString name = QFileInfo(url.path()).fileName();
    if (name.isEmpty()) name = QStringLiteral("dsh-session.zip");
    return name;
}

void DownloadInterceptor::handle(QWebEngineDownloadRequest* request) {
    if (!request) return;
    const QUrl url = request->url();
    if (!shouldIntercept(url)) {
        emit log(QStringLiteral("downloads: 透传 %1").arg(url.toString()));
        return;
    }
    emit log(QStringLiteral("downloads: 拦截会话导出请求 %1").arg(url.path()));

    // 默认下载路径：遵守 XDG 数据目录约定，避免污染用户 ~/Downloads。
    // 用户仍可在原生保存对话框里改选其他位置。
    const QString startDir = QStandardPaths::writableLocation(
                                 QStandardPaths::AppDataLocation) +
                             "/downloads";
    QDir().mkpath(startDir);
    const QString suggested = request->suggestedFileName();
    const QString suggestedName = suggested.isEmpty() ? defaultFilename(url) : suggested;
    const QString startPath = startDir + '/' + suggestedName;
    QWidget* owner = qobject_cast<QWidget*>(parent());
    QString chosen = QFileDialog::getSaveFileName(
        owner,
        tr("保存会话日志"),
        startPath,
        tr("ZIP 压缩包 (*.zip);;所有文件 (*)"));
    if (chosen.isEmpty()) {
        emit log("downloads: 用户已取消");
        request->cancel();
        return;
    }

    auto* progress = new QProgressDialog(
        tr("正在下载 %1").arg(QFileInfo(chosen).fileName()),
        tr("取消"), 0, 100, owner);
    progress->setWindowTitle(QStringLiteral("DSH Desktop"));
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->show();

    request->setDownloadDirectory(QFileInfo(chosen).absolutePath());
    request->setDownloadFileName(QFileInfo(chosen).fileName());
    in_flight_.append({request, progress});

    connect(progress, &QProgressDialog::canceled,
            request, &QWebEngineDownloadRequest::cancel);
    connect(request, &QWebEngineDownloadRequest::receivedBytesChanged,
            this, [this, request]() {
        for (auto& entry : in_flight_) {
            if (entry.request != request || !entry.progress) continue;
            const qint64 total = request->totalBytes();
            if (total > 0) {
                entry.progress->setValue(qMin<qint64>(
                    100, request->receivedBytes() * 100 / total));
            }
            break;
        }
    });
    connect(request, &QWebEngineDownloadRequest::stateChanged,
            this, [this, request](QWebEngineDownloadRequest::DownloadState state) {
        if (state == QWebEngineDownloadRequest::DownloadCompleted
            || state == QWebEngineDownloadRequest::DownloadCancelled
            || state == QWebEngineDownloadRequest::DownloadInterrupted) {
            finish(request);
        }
    });
    request->accept();
}

void DownloadInterceptor::finish(QWebEngineDownloadRequest* request) {
    for (int i = 0; i < in_flight_.size(); ++i) {
        if (in_flight_[i].request != request) continue;
        if (in_flight_[i].progress) in_flight_[i].progress->close();
        const QString destination = request->downloadDirectory()
            + QDir::separator() + request->downloadFileName();
        if (request->state() == QWebEngineDownloadRequest::DownloadCompleted) {
            QMessageBox::information(qobject_cast<QWidget*>(parent()), tr("下载完成"),
                                     tr("会话日志已保存到：\n%1").arg(destination));
            dsh::util::notify(tr("下载完成"),
                              tr("会话日志已保存到 %1").arg(destination));
        } else if (request->state() == QWebEngineDownloadRequest::DownloadInterrupted) {
            emit log(QStringLiteral("downloads: 下载中断：%1")
                         .arg(request->interruptReasonString()));
            QMessageBox::warning(qobject_cast<QWidget*>(parent()), tr("下载失败"),
                                 tr("无法下载会话日志：\n%1")
                                     .arg(request->interruptReasonString()));
        } else {
            emit log(QStringLiteral("downloads: 用户已取消"));
        }
        if (in_flight_[i].progress) in_flight_[i].progress->deleteLater();
        in_flight_.removeAt(i);
        break;
    }
}

}  // namespace dsh::web
