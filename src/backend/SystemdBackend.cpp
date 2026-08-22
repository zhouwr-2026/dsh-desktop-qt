// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SystemdBackend.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <array>
#include <unistd.h>

namespace dsh::backend {

namespace {
constexpr int kProbeTimeoutMs = 1500;

// 通过 curl 做 HTTP 健康检查：返回 2xx/3xx/4xx 都算"服务可达"。
bool httpProbe(const QString& url) {
    QProcess curl;
    curl.start("curl", {"-s", "-o", "/dev/null",
                        "-w", "%{http_code}",
                        "--max-time", "1",
                        url + QStringLiteral("/")});
    if (!curl.waitForFinished(kProbeTimeoutMs)) return false;
    if (curl.exitStatus() != QProcess::NormalExit) return false;
    bool ok = false;
    const int code = QString::fromLocal8Bit(curl.readAllStandardOutput()).toInt(&ok);
    return ok && code >= 200 && code < 500;
}

int countActiveTasks(const QJsonValue& value) {
    if (value.isArray()) {
        int total = 0;
        for (const QJsonValue& item : value.toArray()) total += countActiveTasks(item);
        return total;
    }
    if (!value.isObject()) return 0;

    const QJsonObject object = value.toObject();
    const QString status = object.value(QStringLiteral("status")).toString().toLower();
    const bool explicitlyActive = object.value(QStringLiteral("running")).toBool()
        || object.value(QStringLiteral("active")).toBool()
        || status == QStringLiteral("running") || status == QStringLiteral("active");
    if (explicitlyActive) return 1;

    int total = 0;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        total += countActiveTasks(iterator.value());
    }
    return total;
}

// 探测活跃任务数：尝试若干公开端点，成功解析 JSON 后按明确状态统计。
int activeTaskProbe(const QString& url) {
    for (const QString path : {"/api/jobs", "/api/sessions?limit=1", "/api/runs?limit=1"}) {
        QNetworkAccessManager nam;
        QEventLoop loop;
        QObject::connect(&nam, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
        QNetworkRequest req(QUrl(url + path));
        QNetworkReply* reply = nam.get(req);
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, reply, [reply, &loop]() {
            reply->abort();
            loop.quit();
        });
        timer.start(800);
        loop.exec();
        if (!reply) continue;
        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            continue;
        }
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError) continue;
        return countActiveTasks(document.isArray()
                                    ? QJsonValue(document.array())
                                    : QJsonValue(document.object()));
    }
    return 0;
}
}  // namespace

SystemdBackend::SystemdBackend(const QString& unitName, const QString& url, QObject* parent)
    : Backend(parent), unitName_(unitName), url_(url) {
    unitIsSystem_ = QFile::exists(QStringLiteral("/etc/systemd/system/%1").arg(unitName_)) ||
                    QFile::exists(QStringLiteral("/usr/lib/systemd/system/%1").arg(unitName_));
    if (!unitIsSystem_) {
        const QString userUnit = QDir::homePath() +
                                 QStringLiteral("/.config/systemd/user/") + unitName_;
        if (QFile::exists(userUnit)) unitName_ = userUnit.split('/').last();
    }
}

QString SystemdBackend::detectUnit() {
    auto exists = [](const QString& p) { return QFile::exists(p); };
    if (exists("/etc/systemd/system/dsh-web.service")) return "dsh-web.service";
    if (exists("/usr/lib/systemd/system/dsh-web.service")) return "dsh-web.service";
    const QString user = QDir::homePath() + "/.config/systemd/user/dsh-web.service";
    if (exists(user)) return "dsh-web.service";
    return {};
}

bool SystemdBackend::isRunning() const {
    return httpProbe(url_);
}

Status SystemdBackend::status() {
    Status s;
    s.running = isRunning();
    s.mode = Mode::Systemd;
    s.url = url_;
    if (QStandardPaths::findExecutable("systemctl").isEmpty()) {
        s.detail = QStringLiteral("systemctl 未安装");
    } else {
        QProcess p;
        QStringList args;
        if (!unitIsSystem_) args << "--user";
        args << "--no-pager" << "is-active" << unitName_;
        p.start("systemctl", args);
        p.waitForFinished(3000);
        s.detail = QStringLiteral("systemd 单元 %1: %2")
                       .arg(unitName_,
                            QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed());
    }
    s.activeTasks = s.running ? activeTaskProbe(url_) : 0;
    return s;
}

bool SystemdBackend::systemctl(const QString& verb, bool escalateIfNeeded) {
    auto exe = QStandardPaths::findExecutable("systemctl");
    if (exe.isEmpty()) {
        emit log("systemctl 未安装");
        return false;
    }
    QStringList systemctlArgs;
    if (!unitIsSystem_) systemctlArgs << "--user";
    systemctlArgs << verb << unitName_;

    QString program = exe;
    QStringList args = systemctlArgs;
    if (escalateIfNeeded && unitIsSystem_ &&
        (verb == "start" || verb == "stop" || verb == "restart")) {
        // 非 root 用户改系统 unit 时借助 pkexec 弹 polkit 框
        auto pkexec = QStandardPaths::findExecutable("pkexec");
        if (!pkexec.isEmpty() && ::geteuid() != 0) {
            program = pkexec;
            args = {"--disable-internal-agent", exe};
            args.append(systemctlArgs);
        }
    }
    QProcess p;
    emit log(QStringLiteral("backend: %1 %2").arg(program, args.join(' ')));
    p.start(program, args);
    if (!p.waitForFinished(20000)) {
        emit log(QStringLiteral("backend: systemctl %1 超时").arg(verb));
        return false;
    }
    const bool ok = (p.exitCode() == 0);
    emit log(QStringLiteral("backend: systemctl %1 -> rc=%2").arg(verb).arg(p.exitCode()));
    return ok;
}

bool SystemdBackend::start() {
    if (isRunning()) return true;
    return systemctl("start");
}
bool SystemdBackend::stop(bool force) {
    Q_UNUSED(force);
    return systemctl("stop");
}
bool SystemdBackend::restart() {
    return systemctl("restart");
}

}  // namespace dsh::backend
