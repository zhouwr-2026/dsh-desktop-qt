// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Backend.h"
#include "SupervisedBackend.h"
#include "SystemdBackend.h"

#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace dsh::backend {

namespace {

bool isLoopbackUrl(const QString& rawUrl) {
    const QString host = QUrl(rawUrl).host().toLower();
    return host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1");
}

bool httpProbe(const QString& url) {
    QProcess curl;
    curl.start(QStringLiteral("curl"),
               {QStringLiteral("-s"), QStringLiteral("-o"), QStringLiteral("/dev/null"),
                QStringLiteral("-w"), QStringLiteral("%{http_code}"),
                QStringLiteral("--max-time"), QStringLiteral("1"),
                url + QStringLiteral("/")});
    if (!curl.waitForFinished(1500)) return false;
    bool ok = false;
    const int code = QString::fromLocal8Bit(curl.readAllStandardOutput()).toInt(&ok);
    return ok && code >= 200 && code < 500;
}

class ExternalBackend final : public Backend {
public:
    explicit ExternalBackend(QString url, QObject* parent)
        : Backend(parent), url_(std::move(url)) {}

    Mode mode() const override { return Mode::External; }
    QString url() const override { return url_; }
    bool isRunning() const override { return httpProbe(url_); }
    Status status() override {
        Status result;
        result.running = isRunning();
        result.mode = Mode::External;
        result.url = url_;
        result.detail = QStringLiteral("远程后端（不由桌面端管理）");
        return result;
    }
    bool start() override { return isRunning(); }
    bool stop(bool force = false) override {
        Q_UNUSED(force);
        return true;
    }
    bool restart() override { return isRunning(); }

private:
    QString url_;
};

}  // namespace

QString Backend::defaultUrl() {
    // 尊重启动器 / 测试脚本的环境变量覆盖
    const QByteArray env = qgetenv("DSH_DESKTOP_URL");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QStringLiteral("http://127.0.0.1:3080");
}

std::unique_ptr<Backend> Backend::createForHost(const QString& url, QObject* parent) {
    const QString resolvedUrl = url.isEmpty() ? defaultUrl() : url;
    if (!isLoopbackUrl(resolvedUrl)) {
        return std::make_unique<ExternalBackend>(resolvedUrl, parent);
    }
    const QString unit = SystemdBackend::detectUnit();
    if (!unit.isEmpty()) {
        return std::make_unique<SystemdBackend>(unit, resolvedUrl, parent);
    }
    return std::make_unique<SupervisedBackend>(QString(), resolvedUrl, parent);
}

}  // namespace dsh::backend
