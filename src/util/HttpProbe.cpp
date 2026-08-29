// SPDX-License-Identifier: MIT
// @author zhouwr
#include "HttpProbe.h"

#include <QByteArray>
#include <QProcess>

namespace dsh::util {

namespace {
constexpr int kProbeTimeoutMs = 1500;
constexpr int kCurlMaxTimeSec = 1;
}  // namespace

bool httpProbe(const QString& url) {
    QProcess curl;
    curl.start(QStringLiteral("curl"),
               {QStringLiteral("-s"), QStringLiteral("-o"), QStringLiteral("/dev/null"),
                QStringLiteral("-w"), QStringLiteral("%{http_code}"),
                QStringLiteral("--max-time"), QString::number(kCurlMaxTimeSec),
                url + QStringLiteral("/")});
    if (!curl.waitForFinished(kProbeTimeoutMs)) return false;
    // 退出状态非 NormalExit（被信号杀掉等）也视为失败：与 SystemdBackend 原版一致。
    if (curl.exitStatus() != QProcess::NormalExit) return false;
    bool ok = false;
    const int code =
        QString::fromLocal8Bit(curl.readAllStandardOutput()).toInt(&ok);
    return ok && code >= 200 && code < 500;
}

}  // namespace dsh::util
