// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SystemdBackend.h"
#include "service/ServiceDiscovery.h"
#include "service/ServiceOwnership.h"

#include <QEventLoop>
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

SystemdBackend::SystemdBackend(const dsh::service::DetectedService& detected,
                               const QString& url, QObject* parent)
    : Backend(parent),
      unitName_(detected.unitName),
      scope_(detected.scope) {
    if (url.isEmpty()) {
        QUrl actual;
        actual.setScheme(QStringLiteral("http"));
        actual.setHost(detected.host);
        actual.setPort(detected.port);
        url_ = actual.toString(QUrl::FullyEncoded);
    } else {
        url_ = url;
    }
}

dsh::service::DetectedService SystemdBackend::detect() {
    // 使用只读实况发现：仅当 systemctl show 验证 ``LoadState=loaded`` 且
    // ExecStart 调用官方 ``dsh web`` 时才返回选中候选；否则拒绝未验证的、
    // 外来的或陈旧的 unit（valid=false），让上层退化为子进程兜底。
    // 结果取自发现层的选中项，因此遵循其文档化偏好（两者都有效时优先
    // 当前用户的用户级服务），并把选中的 scope 与解析到的 host/port 一起
    // 传给调用方，避免上层重新推断 scope 或丢掉实际监听地址。
    const dsh::service::DiscoveryResult result =
        dsh::service::discoverDshWebService(QStringLiteral("dsh-web.service"));
    return result.detected();
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
        if (!isSystemUnit()) args << "--user";
        args << "--no-pager" << "is-active" << unitName_;
        p.start("systemctl", args);
        p.waitForFinished(3000);
        s.detail = QStringLiteral("systemd 单元 %1: %2")
                       .arg(unitName_,
                            QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed());
    }
    // 若上一次 start/stop/restart 失败，把真实错误原因追加到 detail 里，
    // 这样上层 QMessageBox 能给到比"无法停止"更具体的提示。
    if (!lastOperationError_.isEmpty()) {
        s.detail = s.detail + QStringLiteral("\n最近一次失败：") + lastOperationError_;
    }
    s.activeTasks = s.running ? activeTaskProbe(url_) : 0;

    // 服务元数据：使用只读实况发现得到当前选中的 ServiceInfo 快照，再派生
    // scope/state/owner/dshHome/failureReason；origin 按只读所有权记录判定，
    // journalSummary 读取只读 journal 摘要。发现不可用时回退到构造时记录的
    // scope，并把 state 由运行探测推断，保持合理默认。本方法不改变任何
    // 系统状态（无 start/stop/restart）。
    const QString user = currentUserName();
    const dsh::service::DiscoveryResult result =
        dsh::service::discoverDshWebService(unitName_);
    const dsh::service::DiscoveredService* selected = result.selected();
    if (selected && selected->valid) {
        applyServiceMetadata(s, selected->info, user);
        s.origin = resolveOrigin(selected->info);
        s.manageable = true;
        // 失败/不可达时才拉取 journal 摘要，避免健康态多的一次进程调用。
        if (!s.running || s.state == dsh::service::LifecycleState::Failed) {
            s.journalSummary = journalSummary(selected->info);
        }
    } else {
        // 快照不可用（systemctl 缺失/发现失败）：保留构造时 scope，给出保守默认。
        s.scope = scope_;
        s.origin = dsh::service::ServiceOrigin::ExistingOfficial;
        s.state = s.running ? dsh::service::LifecycleState::Active
                            : dsh::service::LifecycleState::Unknown;
        s.manageable = true;
        s.owner = user;
    }
    return s;
}

dsh::service::ServiceOrigin SystemdBackend::resolveOrigin(
    const dsh::service::ServiceInfo& info) const {
    // 只读：加载所有权记录，若该 unit+scope 由桌面端补齐则标记为
    // ProvisionedByDesktop，否则按已有官方服务处理。
    dsh::service::ServiceOwnership ownership;
    if (ownership.load() && ownership.contains(info.unitName, info.scope)) {
        return dsh::service::ServiceOrigin::ProvisionedByDesktop;
    }
    return dsh::service::ServiceOrigin::ExistingOfficial;
}

QString SystemdBackend::journalSummary(const dsh::service::ServiceInfo& info) const {
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("journalctl"));
    if (exe.isEmpty()) return {};
    QProcess p;
    QStringList args;
    if (info.scope == dsh::service::ServiceScope::User) args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager") << QStringLiteral("-n") << QStringLiteral("5")
         << QStringLiteral("-u") << info.unitName;
    p.start(exe, args);
    if (!p.waitForFinished(2000)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return {};
    return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
}

bool SystemdBackend::systemctl(const QString& verb, bool escalateIfNeeded) {
    const QString exe = QStandardPaths::findExecutable("systemctl");
    if (exe.isEmpty()) {
        const QString err = QStringLiteral("systemctl 未安装");
        emit log(err);
        lastOperationError_ = err;
        return false;
    }

    QStringList systemctlArgs;
    if (!isSystemUnit()) systemctlArgs << "--user";
    systemctlArgs << verb << unitName_;

    // 提权策略（仅对 system unit + start/stop/restart；user unit 直接跑）：
    //   1. 已是 root：直接调
    //   2. sudo -n -l <systemctl> 探测 NOPASSWD：成功就 sudo -n 调
    //   3. pkexec --disable-internal-agent（polkit 认证框）
    //   4. 直接以当前用户身份跑（很可能因权限不足失败，但错误信息明确）
    //
    // 历史教训：polkit 127 + 部分 KDE 会话里 pkexec 报
    // "No authentication agent found"，但 sudo NOPASSWD 走通是常态
    // （Arch/Ubuntu 装机默认给主用户免密 sudo）。优先 sudo 可避开 polkit agent。
    QString program = exe;
    QStringList args = systemctlArgs;
    if (escalateIfNeeded && isSystemUnit()
        && (verb == "start" || verb == "stop" || verb == "restart")
        && ::geteuid() != 0) {
        const QString sudo = QStandardPaths::findExecutable("sudo");
        if (!sudo.isEmpty()) {
            // 探测 `sudo -n -l <cmd>` 是否免密
            QProcess probe;
            probe.start(sudo, {"-n", "-l", exe});
            if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
                program = sudo;
                args = {"-n", exe};
                args.append(systemctlArgs);
                emit log(QStringLiteral("backend: 检测到 sudo NOPASSWD，使用 sudo 提权"));
            } else {
                const QString pkexec = QStandardPaths::findExecutable("pkexec");
                if (!pkexec.isEmpty()) {
                    program = pkexec;
                    args = {"--disable-internal-agent", exe};
                    args.append(systemctlArgs);
                    emit log(QStringLiteral(
                        "backend: sudo NOPASSWD 不可用，回退到 pkexec"));
                } else {
                    emit log(QStringLiteral(
                        "backend: 无可用的提权方式（sudo 无 NOPASSWD 且无 pkexec），"
                        "将以当前用户身份尝试 systemctl（很可能失败）"));
                }
            }
        } else {
            const QString pkexec = QStandardPaths::findExecutable("pkexec");
            if (!pkexec.isEmpty()) {
                program = pkexec;
                args = {"--disable-internal-agent", exe};
                args.append(systemctlArgs);
            }
        }
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    emit log(QStringLiteral("backend: %1 %2").arg(program, args.join(' ')));
    p.start(program, args);
    if (!p.waitForStarted(5000)) {
        const QString err = QStringLiteral("backend: 启动 %1 失败（%2）")
                                .arg(program, p.errorString());
        emit log(err);
        lastOperationError_ = err;
        return false;
    }
    if (!p.waitForFinished(20000)) {
        const QString err = QStringLiteral("backend: systemctl %1 超时（>20s）").arg(verb);
        emit log(err);
        lastOperationError_ = err;
        p.kill();
        p.waitForFinished(3000);
        return false;
    }
    const QString output = QString::fromLocal8Bit(p.readAll()).trimmed();
    // 综合判定：进程必须正常退出（未被信号杀），且 exit code 为 0；再叠加
    // QProcess::error() 看是否有更细粒度的失败原因（如超时被 kill）。
    const bool normalExit = (p.exitStatus() == QProcess::NormalExit);
    const bool cleanExit = normalExit && (p.exitCode() == 0);
    if (cleanExit && p.error() == QProcess::UnknownError) {
        emit log(QStringLiteral("backend: systemctl %1 -> ok").arg(verb));
        lastOperationError_.clear();
        return true;
    }
    const QString reason = !normalExit
        ? QStringLiteral("进程被信号终止（status=%1）")
              .arg(static_cast<int>(p.exitStatus()))
        : (p.error() != QProcess::UnknownError
              ? QStringLiteral("QProcess 错误：%1").arg(p.errorString())
              : QStringLiteral("rc=%1").arg(p.exitCode()));
    const QString err = output.isEmpty()
        ? QStringLiteral("backend: systemctl %1 失败：%2").arg(verb).arg(reason)
        : QStringLiteral("backend: systemctl %1 失败：%2\n%3")
              .arg(verb).arg(reason).arg(output);
    emit log(err);
    lastOperationError_ = err;
    return false;
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
