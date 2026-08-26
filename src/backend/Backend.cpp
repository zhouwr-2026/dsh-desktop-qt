// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Backend.h"
#include "SupervisedBackend.h"
#include "SystemdBackend.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
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
        // 远程模式：没有本机 unit，scope 无意义，保留默认 System；
        // 生命周期只反映"是否可达"，不管理本机生命周期。
        result.origin = dsh::service::ServiceOrigin::External;
        result.state = result.running ? dsh::service::LifecycleState::Active
                                      : dsh::service::LifecycleState::Unknown;
        result.manageable = false;
        if (!result.running) {
            result.failureReason = QStringLiteral("远程后端不可达");
        }
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

QString currentUserName() {
    const QByteArray user = qgetenv("USER");
    if (!user.isEmpty()) return QString::fromLocal8Bit(user);
    const QByteArray login = qgetenv("LOGNAME");
    if (!login.isEmpty()) return QString::fromLocal8Bit(login);
    return QDir(QDir::homePath()).dirName();
}

void applyServiceMetadata(Status& status,
                          const dsh::service::ServiceInfo& info,
                          const QString& currentUser) {
    status.scope = info.scope;
    status.state = info.state;
    status.dshHome = info.dshHome;

    status.owner = info.user;
    if (status.owner.isEmpty()
        && info.scope == dsh::service::ServiceScope::User
        && !currentUser.isEmpty()) {
        status.owner = currentUser;
    }

    status.failureReason.clear();
    if (!info.loadState.isEmpty() && info.loadState != QLatin1String("loaded")) {
        status.failureReason = QStringLiteral("LoadState=%1").arg(info.loadState);
    } else if (info.state == dsh::service::LifecycleState::Failed) {
        status.failureReason = info.subState.isEmpty()
            ? QStringLiteral("ActiveState=failed")
            : QStringLiteral("ActiveState=failed, SubState=%1").arg(info.subState);
    }
}

QString profileRepairHint(const QString& journalSummary) {
    QStringList hints;
    if (journalSummary.contains(QStringLiteral("ERR_MODULE_NOT_FOUND"))
        || journalSummary.contains(QStringLiteral("Cannot find module"))) {
        hints.append(QStringLiteral(
            "检测到 DSH profile 插件构建产物缺失。先运行 `dsh-profile-check`；"
            "修复可执行 `dsh plugin --profile web install`。若重装后仍缺失，应改用"
            "包含构建产物的发布包或显式构建该插件，反复重启不能修复缺失文件。"));
    }
    if (journalSummary.contains(
            QStringLiteral("initial connection or tool synchronization failed"))) {
        hints.append(QStringLiteral(
            "检测到辅助 MCP 初始化失败。非关键 MCP 应设置 `failOnStartupError: false` "
            "并启用重连，避免工具故障中断 DSH Web 和正在执行的任务。"));
    }
    return hints.join(QStringLiteral("\n"));
}

bool backendHealthObservationStable(bool running,
                                    int& consecutiveFailures,
                                    int failureThreshold) {
    if (running) {
        consecutiveFailures = 0;
        return true;
    }
    ++consecutiveFailures;
    return consecutiveFailures >= qMax(1, failureThreshold);
}

QString systemdInvocationJournalMatch(const QString& invocationId) {
    if (invocationId.size() != 32) return {};
    for (const QChar character : invocationId) {
        const ushort code = character.unicode();
        const bool hexadecimal = (code >= '0' && code <= '9')
            || (code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F');
        if (!hexadecimal) return {};
    }
    return QStringLiteral("_SYSTEMD_INVOCATION_ID=") + invocationId.toLower();
}

bool requiresStartConfirmation(const Status& status) {
    return status.mode == Mode::Systemd
        && (status.state == dsh::service::LifecycleState::Inactive
            || status.state == dsh::service::LifecycleState::Failed);
}

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
    const dsh::service::DetectedService detected = SystemdBackend::detect();
    if (detected.valid) {
        // 使用发现层解析到的实际监听地址组装 URL（而非默认值），并保留选中
        // 候选的 scope，避免重新从文件系统推断 scope 或丢掉 host/port。
        QUrl actual;
        actual.setScheme(QStringLiteral("http"));
        actual.setHost(detected.host);
        actual.setPort(detected.port);
        return std::make_unique<SystemdBackend>(
            detected, actual.toString(QUrl::FullyEncoded), parent);
    }
    return std::make_unique<SupervisedBackend>(QString(), resolvedUrl, parent);
}

}  // namespace dsh::backend
