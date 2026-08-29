// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SupervisedBackend.h"

#include "../updater/Updater.h"
#include "../util/HttpProbe.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>

#include <signal.h>
#include <unistd.h>

namespace dsh::backend {

// httpProbe 已下沉到 ``dsh::util::httpProbe``（util/HttpProbe.{h,cpp}）。

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
    return dsh::util::httpProbe(url_);
}

Status SupervisedBackend::status() {
    Status s;
    s.running = isRunning();
    s.mode = Mode::Supervised;
    s.url = url_;
    s.origin = dsh::service::ServiceOrigin::SupervisedFallback;
    s.manageable = true;
    s.owner = currentUserName();
    // Supervised 模式必须知道当前 dsh CLI 版本，以供上层在启动前做最低版本
    // 校验（见 dsh::updater::kMinimumDshVersion）。复用 Updater::readLocalVersion
    // 的 package.json 探测，避免重复维护路径列表。
    // (变更理由: 依赖审查建议 P0-3, 启动期校验)
    s.dshVersion = dsh::updater::Updater::readLocalVersion();
    // DSH_HOME：子进程启动时遵循环境的 DSH_HOME，缺省为 ~/.dsh。
    const QByteArray envHome = qgetenv("DSH_HOME");
    s.dshHome = envHome.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.dsh")
        : QString::fromLocal8Bit(envHome);

    if (proc_) {
        s.detail = QStringLiteral("supervised 子进程 pid=%1 退出码=%2")
                       .arg(proc_->processId())
                       .arg(proc_->exitCode());
        if (proc_->state() == QProcess::Running) {
            s.state = dsh::service::LifecycleState::Active;
        } else {
            s.state = dsh::service::LifecycleState::Failed;
            s.failureReason = QStringLiteral("supervised 子进程已退出（code=%1）")
                                  .arg(proc_->exitCode());
        }
    } else {
        s.detail = QStringLiteral("supervised：尚未启动");
        s.state = dsh::service::LifecycleState::Inactive;
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
    p->setChildProcessModifier([]() {
        if (::setsid() == -1) ::_exit(127);
    });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.value("DSH_HOME").isEmpty()) {
        env.insert("DSH_HOME", QDir::homePath() + QStringLiteral("/.dsh"));
    }
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    proc_ = p;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p]() {
        emit log(QStringLiteral("dsh web: %1")
                     .arg(QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed()));
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int code, QProcess::ExitStatus st) {
        emit log(QStringLiteral("supervised: dsh web 退出 code=%1 状态=%2")
                     .arg(code).arg(static_cast<int>(st)));
        if (proc_ == p) proc_ = nullptr;
        p->deleteLater();
    });
    p->start();
    if (!p->waitForStarted(5000)) {
        emit log(QStringLiteral("supervised: 启动失败 (%1)").arg(p->errorString()));
        if (proc_ == p) proc_ = nullptr;
        p->deleteLater();
        return false;
    }
    emit log(QStringLiteral("supervised: dsh web 已启动，等待页面健康检查确认就绪"));
    return true;
}

bool SupervisedBackend::stop(bool force) {
    QProcess* process = proc_;
    if (!process) return true;
    // QProcess 状态：NotRunning 必然是"已结束"。其他状态（Starting /
    // Running）下 processId 可能瞬时为 0（子进程尚未派生完成），所以再做
    // 一次 end-of-process 探测：exitCode / error 已被设置 ⇒ 实际已结束。
    if (process->state() == QProcess::NotRunning) return true;
    const qint64 pid = process->processId();
    if (pid <= 0) {
        // 子进程已经在内核层面消失（finished 信号可能尚未派发）。
        // 此时不应再尝试发信号，直接判成功以免上层误报"无法停止"。
        emit log("supervised: 子进程已结束但 proc_ 仍持有 QProcess，按已停止处理");
        if (proc_ == process) proc_ = nullptr;
        process->deleteLater();
        return true;
    }

    const pid_t processGroup = -static_cast<pid_t>(pid);
    ::kill(processGroup, force ? SIGKILL : SIGTERM);
    if (!process->waitForFinished(force ? 3000 : 8000) && !force) {
        emit log("supervised: 优雅退出超时，升级为 SIGKILL");
        ::kill(processGroup, SIGKILL);
        process->waitForFinished(3000);
    }
    if (process->state() != QProcess::NotRunning) {
        emit log(QStringLiteral("supervised: 进程未在超时内退出 (state=%1)")
                     .arg(static_cast<int>(process->state())));
        return false;
    }
    return true;
}

bool SupervisedBackend::restart() {
    stop(true);
    return start();
}

}  // namespace dsh::backend
