// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SupervisedBackend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <signal.h>

namespace dsh::backend {

namespace {
bool httpProbe(const QString& url) {
    QProcess curl;
    curl.start("curl", {"-s", "-o", "/dev/null", "-w", "%{http_code}",
                        "--max-time", "1", url + QStringLiteral("/")});
    if (!curl.waitForFinished(1500)) return false;
    bool ok = false;
    const int code = QString::fromLocal8Bit(curl.readAllStandardOutput()).toInt(&ok);
    return ok && code >= 200 && code < 500;
}
}  // namespace

QString SupervisedBackend::resolveDshBin() {
    const QByteArray env = qgetenv("DSH_BIN");
    if (!env.isEmpty() && QFile::exists(QString::fromLocal8Bit(env)))
        return QString::fromLocal8Bit(env);
    auto found = QStandardPaths::findExecutable("dsh");
    if (!found.isEmpty()) return found;
    const QStringList candidates = {
        "/usr/bin/dsh", "/usr/local/bin/dsh",
        QDir::homePath() + "/.local/bin/dsh",
    };
    for (const auto& c : candidates) {
        if (QFile::exists(c)) return c;
    }
    return {};
}

SupervisedBackend::SupervisedBackend(const QString& dshBin, const QString& url, QObject* parent)
    : Backend(parent), dshBin_(dshBin.isEmpty() ? resolveDshBin() : dshBin), url_(url) {}

SupervisedBackend::~SupervisedBackend() {
    if (proc_) {
        stop(true);
    }
}

bool SupervisedBackend::isRunning() const {
    return httpProbe(url_);
}

Status SupervisedBackend::status() {
    Status s;
    s.running = isRunning();
    s.mode = Mode::Supervised;
    s.url = url_;
    if (proc_) {
        s.detail = QStringLiteral("supervised 子进程 pid=%1 退出码=%2")
                       .arg(proc_->processId())
                       .arg(proc_->exitCode());
    } else {
        s.detail = QStringLiteral("supervised：尚未启动");
    }
    return s;
}

bool SupervisedBackend::start() {
    if (dshBin_.isEmpty()) {
        emit log("supervised: 找不到 dsh 二进制");
        return false;
    }
    if (proc_ && proc_->state() != QProcess::NotRunning) return true;

    const QUrl configuredUrl(url_);
    QString port = QString::fromLocal8Bit(qgetenv("DSH_DESKTOP_PORT"));
    if (port.isEmpty()) port = QString::number(configuredUrl.port(3080));
    QString host = configuredUrl.host();
    if (host.isEmpty() || host == QStringLiteral("localhost")) {
        host = QStringLiteral("127.0.0.1");
    }

    auto* p = new QProcess(this);
    p->setProgram(dshBin_);
    p->setArguments({"web", "--host", host, "--port", port});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.value("DSH_HOME").isEmpty()) {
        env.insert("DSH_HOME", QDir::homePath() + QStringLiteral("/.dsh"));
    }
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    proc_ = p;
    connect(p, &QProcess::readyReadStandardOutput, this, [this]() {
        emit log(QStringLiteral("dsh web: %1")
                     .arg(QString::fromLocal8Bit(proc_->readAllStandardOutput()).trimmed()));
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st) {
        emit log(QStringLiteral("supervised: dsh web 退出 code=%1 状态=%2")
                     .arg(code).arg(static_cast<int>(st)));
    });
    p->start();
    if (!p->waitForStarted(5000)) {
        emit log(QStringLiteral("supervised: 启动失败 (%1)").arg(p->errorString()));
        return false;
    }
    // 等待 HTTP 可达，最多 15 秒
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (p->state() == QProcess::NotRunning) {
            emit log("supervised: 子进程提前退出");
            return false;
        }
        if (isRunning()) return true;
        QThread::msleep(250);
    }
    emit log("supervised: 15 秒内未就绪");
    return false;
}

bool SupervisedBackend::stop(bool force) {
    if (!proc_ || proc_->state() == QProcess::NotRunning) return true;
    const qint64 pid = proc_->processId();
    if (pid <= 0) return true;
    // SIGTERM 整个进程组（start_new_session=true 时成立）
    ::kill(static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM);
    if (!proc_->waitForFinished(force ? 3000 : 8000)) {
        if (!force) {
            emit log("supervised: 优雅退出超时，升级为 SIGKILL");
            return stop(true);
        }
    }
    return true;
}

bool SupervisedBackend::restart() {
    stop(true);
    return start();
}

}  // namespace dsh::backend
