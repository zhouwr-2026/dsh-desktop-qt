// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SyncHttp.h"

#include "BuildVersion.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace dsh::util {

namespace {
QByteArray defaultUserAgent() {
    return QByteArrayLiteral("dsh-desktop/") + QByteArray(DSH_DESKTOP_VERSION)
           + QByteArrayLiteral(" (Qt6)");
}
}  // namespace

SyncHttpResult syncHttpGet(const QUrl& url, int timeoutSeconds,
                           const QByteArray& userAgent) {
    SyncHttpResult result;
    QNetworkAccessManager nam;
    QEventLoop loop;
    QObject::connect(&nam, &QNetworkAccessManager::finished, &loop,
                     &QEventLoop::quit);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent",
                         userAgent.isEmpty() ? defaultUserAgent() : userAgent);

    QNetworkReply* reply = nam.get(request);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply, &loop]() {
        reply->abort();
        loop.quit();
    });
    timer.start(qMax(1, timeoutSeconds) * 1000);
    loop.exec();

    if (!reply) {
        result.errorString = QStringLiteral("no network reply");
        return result;
    }

    // Qt 对 HTTP 错误状态（如 404）也会把 ``reply->error()`` 置为非 NoError，
    // 但这类回复带有 HTTP 状态码；真正的网络故障（超时/拒绝连接）没有状态码。
    // 因此先读取状态码：有状态码则交给调用方语义化处理，无状态码才判定为
    // 网络失败（ok=false）。
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && httpStatus <= 0) {
        result.errorString = reply->errorString();
        reply->deleteLater();
        return result;
    }

    result.httpStatus = httpStatus;
    result.body = reply->readAll();
    result.ok = true;
    reply->deleteLater();
    return result;
}

}  // namespace dsh::util
