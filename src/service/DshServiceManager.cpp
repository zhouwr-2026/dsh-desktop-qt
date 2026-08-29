// SPDX-License-Identifier: MIT
// @author zhouwr

#include "DshServiceManager.h"
#include "ShowFailure.h"
#include "SystemctlCommandBuilder.h"
#include "SystemctlShowParser.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <unistd.h>

namespace dsh::service {

namespace {

// ``ProcessOutcome`` / DshServiceManager 私有成员用独立 enum，避免在
// ``DshServiceManager.h`` 暴露 ``<QProcess>`` 头。此函数在内部把 Qt 的
// ``QProcess::ExitStatus`` / ``QProcess::ProcessError`` 映射为自定义 enum。
ProcessExitStatus fromQProcessExitStatus(QProcess::ExitStatus s) {
    return (s == QProcess::NormalExit) ? ProcessExitStatus::Normal
                                       : ProcessExitStatus::Crashed;
}

ProcessErrorCode fromQProcessError(QProcess::ProcessError e) {
    switch (e) {
        case QProcess::FailedToStart:  return ProcessErrorCode::FailedToStart;
        case QProcess::Crashed:        return ProcessErrorCode::Crashed;
        case QProcess::Timedout:       return ProcessErrorCode::Timedout;
        case QProcess::WriteError:     return ProcessErrorCode::WriteError;
        case QProcess::ReadError:      return ProcessErrorCode::ReadError;
        case QProcess::UnknownError:   return ProcessErrorCode::Unknown;
    }
    return ProcessErrorCode::Unknown;
}

}  // namespace

namespace {

constexpr qint64 kMaxCaptureBytes = 1 << 20;  // 单次操作的输出捕获上限（1 MiB）

/// 当前登录用户：取 USER/LOGNAME 环境变量，兜底使用家目录名。
QString currentSystemUser() {
    const QByteArray user = qgetenv("USER");
    if (!user.isEmpty()) return QString::fromLocal8Bit(user);
    const QByteArray login = qgetenv("LOGNAME");
    if (!login.isEmpty()) return QString::fromLocal8Bit(login);
    return QDir::home().dirName();
}

// systemctl show 失败分类已下沉到 ``dsh::service::classifyShowFailure``
// （ShowFailure.{h,cpp}），ServiceDiscovery 与本类共用同一份实现。

}  // namespace

DshServiceManager::DshServiceManager(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<ServiceOperation>("dsh::service::ServiceOperation");
    qRegisterMetaType<ServiceResult>("dsh::service::ServiceResult");
    qRegisterMetaType<ServiceError>("dsh::service::ServiceError");
    qRegisterMetaType<OperationResult>("dsh::service::OperationResult");
    qRegisterMetaType<ServiceStatus>("dsh::service::ServiceStatus");
    currentUser_ = currentSystemUser();

    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]() {
        if (currentRequestId_ < 0 || !process_) return;
        timedOut_ = true;
        process_->kill();
    });
}

DshServiceManager::~DshServiceManager() {
    timeoutTimer_->stop();
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 目标配置
// ---------------------------------------------------------------------------

bool DshServiceManager::setUnit(const QString& unitName, ServiceScope scope) {
    QString error;
    if (!SystemctlCommandBuilder::isValidUnitName(unitName, &error)) {
        hasTarget_ = false;
        return false;
    }
    unitName_ = unitName;
    scope_ = scope;
    hasTarget_ = true;
    validated_ = false;
    return true;
}

void DshServiceManager::setDetectedService(const DetectedService& detected) {
    if (detected.unitName.isEmpty()) {
        hasTarget_ = false;
        return;
    }
    unitName_ = detected.unitName;
    scope_ = detected.scope;
    hasTarget_ = true;
    validated_ = detected.valid;
}

void DshServiceManager::setOperationTimeoutMs(int ms) {
    operationTimeoutMs_ = ms > 0 ? ms : operationTimeoutMs_;
}

// ---------------------------------------------------------------------------
// 异步操作入口
// ---------------------------------------------------------------------------

qint64 DshServiceManager::requestDiscovery() {
    if (!canStartOperation()) {
        rejectOperation(ServiceOperation::Discovery, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    const QString exe = resolveSystemctl();
    if (exe.isEmpty()) {
        discoverySystemctlAvailable_ = false;
        rejectOperation(ServiceOperation::Discovery, ServiceResult::SystemctlMissing,
                        ServiceError::SystemctlNotFound,
                        QStringLiteral("找不到 systemctl 可执行文件"));
        return -1;
    }
    discoverySystemctlAvailable_ = true;

    beginOperation(ServiceOperation::Discovery);
    startProcess(exe, SystemctlCommandBuilder::systemctlArguments(ServiceOperation::Discovery,
                                         ServiceScope::System, currentUnit()));
    return currentRequestId_;
}

qint64 DshServiceManager::requestStatus() {
    if (!canStartOperation()) {
        rejectOperation(ServiceOperation::Status, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    if (!hasTarget_) {
        rejectOperation(ServiceOperation::Status, ServiceResult::InvalidRequest,
                        ServiceError::InvalidRequest,
                        QStringLiteral("未设定目标服务"));
        return -1;
    }
    QString error;
    if (!SystemctlCommandBuilder::isValidUnitName(unitName_, &error)) {
        rejectOperation(ServiceOperation::Status, ServiceResult::InvalidUnit,
                        ServiceError::UnitNameInvalid, error);
        return -1;
    }
    const QString exe = resolveSystemctl();
    if (exe.isEmpty()) {
        rejectOperation(ServiceOperation::Status, ServiceResult::SystemctlMissing,
                        ServiceError::SystemctlNotFound,
                        QStringLiteral("找不到 systemctl 可执行文件"));
        return -1;
    }
    const ResolvedCommand cmd =
        SystemctlCommandBuilder::resolveCommand(ServiceOperation::Status, scope_, unitName_, exe,
                       resolvePkexec(), static_cast<qint64>(::geteuid()));
    return launchOneShot(ServiceOperation::Status, cmd.program, cmd.arguments);
}

qint64 DshServiceManager::start() {
    return launchMutate(ServiceOperation::Start);
}

qint64 DshServiceManager::stop() {
    return launchMutate(ServiceOperation::Stop);
}

qint64 DshServiceManager::restart() {
    return launchMutate(ServiceOperation::Restart);
}

qint64 DshServiceManager::daemonReload() {
    if (!canStartOperation()) {
        rejectOperation(ServiceOperation::DaemonReload, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    const QString exe = resolveSystemctl();
    if (exe.isEmpty()) {
        rejectOperation(ServiceOperation::DaemonReload, ServiceResult::SystemctlMissing,
                        ServiceError::SystemctlNotFound,
                        QStringLiteral("找不到 systemctl 可执行文件"));
        return -1;
    }
    // daemon-reload 无 unit 参数；只携带当前目标 scope（缺省用户级）。
    const ResolvedCommand cmd =
        SystemctlCommandBuilder::resolveCommand(ServiceOperation::DaemonReload, scope_, QString(), exe,
                       resolvePkexec(), static_cast<qint64>(::geteuid()));
    return launchOneShot(ServiceOperation::DaemonReload, cmd.program, cmd.arguments);
}

qint64 DshServiceManager::enable() {
    if (!canStartOperation()) {
        rejectOperation(ServiceOperation::Enable, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    if (!hasTarget_) {
        rejectOperation(ServiceOperation::Enable, ServiceResult::InvalidRequest,
                        ServiceError::InvalidRequest,
                        QStringLiteral("未设定目标服务"));
        return -1;
    }
    QString error;
    if (!SystemctlCommandBuilder::isValidUnitName(unitName_, &error)) {
        rejectOperation(ServiceOperation::Enable, ServiceResult::InvalidUnit,
                        ServiceError::UnitNameInvalid, error);
        return -1;
    }
    const QString exe = resolveSystemctl();
    if (exe.isEmpty()) {
        rejectOperation(ServiceOperation::Enable, ServiceResult::SystemctlMissing,
                        ServiceError::SystemctlNotFound,
                        QStringLiteral("找不到 systemctl 可执行文件"));
        return -1;
    }
    const ResolvedCommand cmd =
        SystemctlCommandBuilder::resolveCommand(ServiceOperation::Enable, scope_, unitName_, exe,
                       resolvePkexec(), static_cast<qint64>(::geteuid()));
    return launchOneShot(ServiceOperation::Enable, cmd.program, cmd.arguments);
}

qint64 DshServiceManager::startJournalTail(int lines) {
    if (!canStartOperation()) {
        rejectOperation(ServiceOperation::JournalTail, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    if (hasTarget_) {
        QString unitError;
        if (!SystemctlCommandBuilder::isValidUnitName(unitName_, &unitError)) {
            rejectOperation(ServiceOperation::JournalTail, ServiceResult::InvalidUnit,
                            ServiceError::UnitNameInvalid, unitError);
            return -1;
        }
    }
    const QString exe = resolveJournalctl();
    if (exe.isEmpty()) {
        rejectOperation(ServiceOperation::JournalTail, ServiceResult::JournalctlMissing,
                        ServiceError::JournalctlNotFound,
                        QStringLiteral("找不到 journalctl 可执行文件"));
        return -1;
    }
    beginOperation(ServiceOperation::JournalTail);
    startProcess(exe, SystemctlCommandBuilder::journalctlArguments(scope_, currentUnit(), lines, /*follow=*/true));
    return currentRequestId_;
}

bool DshServiceManager::stopJournalTail() {
    if (currentOperation_ != ServiceOperation::JournalTail || currentRequestId_ < 0) {
        return false;
    }
    cancel();
    return true;
}

void DshServiceManager::cancel(qint64 requestId) {
    if (requestId < 0) requestId = currentRequestId_;
    if (requestId != currentRequestId_ || currentRequestId_ < 0) return;
    if (!process_) return;
    cancelled_ = true;
    timeoutTimer_->stop();
    process_->kill();
}

// ---------------------------------------------------------------------------
// 变更类操作：统一在启动前做「目标 + 验证 + 提权」决策。
// ---------------------------------------------------------------------------

qint64 DshServiceManager::launchMutate(ServiceOperation operation) {
    if (!canStartOperation()) {
        rejectOperation(operation, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    if (!hasTarget_) {
        rejectOperation(operation, ServiceResult::InvalidRequest,
                        ServiceError::InvalidRequest,
                        QStringLiteral("未设定目标服务"));
        return -1;
    }
    QString error;
    if (!SystemctlCommandBuilder::isValidUnitName(unitName_, &error)) {
        rejectOperation(operation, ServiceResult::InvalidUnit,
                        ServiceError::UnitNameInvalid, error);
        return -1;
    }
    if (!validated_) {
        rejectOperation(operation, ServiceResult::NotValidated,
                        ServiceError::TargetNotValidated,
                        QStringLiteral(
                            "目标未通过官方验证与归属校验；请先 requestDiscovery() 或 "
                            "setDetectedService(valid=true) 再变更"));
        return -1;
    }

    const QString exe = resolveSystemctl();
    if (exe.isEmpty()) {
        rejectOperation(operation, ServiceResult::SystemctlMissing,
                        ServiceError::SystemctlNotFound,
                        QStringLiteral("找不到 systemctl 可执行文件"));
        return -1;
    }
    const ResolvedCommand cmd =
        SystemctlCommandBuilder::resolveCommand(operation, scope_, unitName_, exe, resolvePkexec(),
                       static_cast<qint64>(::geteuid()));
    return launchOneShot(operation, cmd.program, cmd.arguments);
}

// ---------------------------------------------------------------------------
// 进程启动与完成处理
// ---------------------------------------------------------------------------

qint64 DshServiceManager::launchOneShot(ServiceOperation operation,
                                        const QString& program,
                                        const QStringList& arguments) {
    if (!canStartOperation()) {
        rejectOperation(operation, ServiceResult::Busy,
                        ServiceError::OverlappingOperation,
                        QStringLiteral("已有操作在进行"));
        return -1;
    }
    beginOperation(operation);
    startProcess(program, arguments);
    return currentRequestId_;
}

bool DshServiceManager::canStartOperation() const {
    return currentRequestId_ < 0;
}

void DshServiceManager::beginOperation(ServiceOperation operation) {
    currentOperation_ = operation;
    currentRequestId_ = nextRequestId_++;
    phase_ = (operation == ServiceOperation::Discovery)
        ? Phase::DiscoverySystem
        : Phase::None;
    timedOut_ = false;
    cancelled_ = false;
    finalized_ = false;
    discoveryCandidates_.clear();
    emit operationStarted(operation);
    elapsed_.start();
}

void DshServiceManager::startProcess(const QString& program,
                                     const QStringList& arguments) {
    if (process_) {
        process_->disconnect(this);
        process_->deleteLater();
        process_ = nullptr;
    }
    timedOut_ = false;
    cancelled_ = false;
    finalized_ = false;
    processStarted_ = false;
    lastExitCode_ = 0;
    lastExitStatus_ = ProcessExitStatus::Normal;
    lastProcessError_ = ProcessErrorCode::Unknown;
    stdoutBuffer_.clear();
    stderrBuffer_.clear();
    journalLineBuffer_.clear();

    auto* p = new QProcess(this);
    process_ = p;
    p->setProgram(program);
    p->setArguments(arguments);
    p->setProcessChannelMode(QProcess::SeparateChannels);

    connect(p, &QProcess::started, this, [this, p]() {
        if (p != process_ || finalized_) return;
        processStarted_ = true;
    });

    connect(p, &QProcess::readyReadStandardOutput, this, [this, p]() {
        if (p != process_ || finalized_) return;
        const QString chunk = QString::fromLocal8Bit(p->readAllStandardOutput());
        appendBuffer(stdoutBuffer_, chunk);
        emit operationOutput(currentOperation_, chunk);
        if (currentOperation_ == ServiceOperation::JournalTail) {
            journalLineBuffer_ += chunk;
            int nl = -1;
            while ((nl = journalLineBuffer_.indexOf(QLatin1Char('\n'))) >= 0) {
                const QString line = journalLineBuffer_.left(nl);
                journalLineBuffer_.remove(0, nl + 1);
                emit journalTailLine(line);
            }
        }
    });

    connect(p, &QProcess::readyReadStandardError, this, [this, p]() {
        if (p != process_ || finalized_) return;
        const QString chunk = QString::fromLocal8Bit(p->readAllStandardError());
        appendBuffer(stderrBuffer_, chunk);
        emit operationOutput(currentOperation_, chunk);
    });

    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        if (p != process_ || finalized_) return;
        lastExitCode_ = code;
        lastExitStatus_ = fromQProcessExitStatus(st);
        processStarted_ = true;
        onProcessFinished(true);
    });

    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (p != process_ || finalized_) return;
        lastProcessError_ = fromQProcessError(e);
        if (e == QProcess::FailedToStart) {
            processStarted_ = false;
            onProcessFinished(false);
        } else if (e == QProcess::Crashed) {
            // 崩溃也会触发 finished；此处仅兜底记录，避免重复处理。
        }
    });

    if (currentOperation_ != ServiceOperation::JournalTail) {
        timeoutTimer_->start(operationTimeoutMs_);
    } else {
        timeoutTimer_->stop();
    }
    p->start();
}

void DshServiceManager::onProcessFinished(bool started) {
    if (finalized_) return;
    timeoutTimer_->stop();
    const qint64 elapsed = elapsed_.elapsed();

    // 发现操作：分两阶段（系统域 -> 用户域），只在用户域结束后统一结束。
    if (phase_ == Phase::DiscoverySystem || phase_ == Phase::DiscoveryUser) {
        handleDiscoveryFinished(started, lastExitCode_);
        return;
    }

    ProcessOutcome outcome;
    outcome.started = started;
    outcome.timedOut = timedOut_;
    outcome.cancelled = cancelled_;
    outcome.exitStatus = lastExitStatus_;
    outcome.exitCode = lastExitCode_;
    outcome.processError = lastProcessError_;

    ServiceResult result = mapProcessResult(outcome);
    ServiceError error = mapProcessError(outcome);

    const QString stderrText = stderrBuffer_.trimmed();

    if (currentOperation_ == ServiceOperation::Status) {
        // 状态：读到的快照还要解析为 ServiceStatus 并通过 statusResult 上报。
        if (result == ServiceResult::Success) {
            const ServiceInfo info =
                parseSystemctlShow(stdoutBuffer_, currentUnit(), scope_);
            lastStatus_ = toServiceStatus(info);
            lastStatus_.valid = true;
            lastStatus_.running = (info.activeState == QLatin1String("active"));
            lastStatus_.manageable =
                (evaluateManageability(info, currentUser_) == ServiceResult::Success);
            emit statusResult(lastStatus_);
        } else {
            lastStatus_ = ServiceStatus{};
            lastStatus_.scope = scope_;
            lastStatus_.unitName = currentUnit();
        }
    }

    QString message;
    if (result == ServiceResult::Success) {
        message = currentOperation_ == ServiceOperation::JournalTail
            ? QStringLiteral("journal 尾部跟随结束")
            : QStringLiteral("操作成功");
    } else if (result == ServiceResult::Timeout) {
        message = QStringLiteral("操作超时");
    } else if (result == ServiceResult::Cancelled) {
        message = QStringLiteral("操作已取消");
    } else if (!stderrText.isEmpty()) {
        message = stderrText;
    } else {
        message = QStringLiteral("操作失败 (退出码 %1)").arg(lastExitCode_);
    }

    emitFinished(currentOperation_, result, error, lastExitCode_, elapsed, message);
    cleanupCurrent();
}

void DshServiceManager::handleDiscoveryFinished(bool started, int exitCode) {
    const bool sysPhase = (phase_ == Phase::DiscoverySystem);
    const ServiceScope curScope =
        sysPhase ? ServiceScope::System : ServiceScope::User;
    const QString unit = currentUnit();

    DiscoveredService candidate;
    candidate.info.unitName = unit;
    candidate.info.scope = curScope;

    if (started && exitCode == 0 && !timedOut_ && !cancelled_) {
        candidate = validateCandidate(
            parseSystemctlShow(stdoutBuffer_, unit, curScope));
    } else {
        candidate.valid = false;
        if (!started) {
            candidate.rejection = RejectionReason::ProcessError;
        } else if (cancelled_) {
            candidate.rejection = RejectionReason::ProcessError;
        } else if (timedOut_) {
            candidate.rejection = RejectionReason::Timeout;
        } else {
            RejectionReason reason = RejectionReason::None;
            QString detail;
            classifyShowFailure(stderrBuffer_, unit, reason, detail);
            candidate.rejection = reason;
            candidate.rejectionDetail = detail;
        }
        if (candidate.rejectionDetail.isEmpty()) {
            candidate.rejectionDetail = stderrBuffer_.trimmed();
        }
    }
    discoveryCandidates_.append(candidate);

    if (sysPhase) {
        phase_ = Phase::DiscoveryUser;
        const QString exe = resolveSystemctl();
        if (exe.isEmpty()) {
            discoverySystemctlAvailable_ = false;
            DiscoveredService userCandidate;
            userCandidate.info.unitName = unit;
            userCandidate.info.scope = ServiceScope::User;
            userCandidate.valid = false;
            userCandidate.rejection = RejectionReason::SystemctlMissing;
            userCandidate.rejectionDetail = QStringLiteral("找不到 systemctl 可执行文件");
            discoveryCandidates_.append(userCandidate);
            finalizeDiscovery();
            return;
        }
        startProcess(exe, SystemctlCommandBuilder::systemctlArguments(ServiceOperation::Discovery,
                                             ServiceScope::User, unit));
        return;
    }

    finalizeDiscovery();
}

void DshServiceManager::finalizeDiscovery() {
    const qint64 elapsed = elapsed_.elapsed();

    lastDiscovery_.candidates = discoveryCandidates_;
    lastDiscovery_.systemctlAvailable = discoverySystemctlAvailable_;
    lastDiscovery_.selectedIndex = selectCandidateIndex(discoveryCandidates_);

    const DiscoveredService* selected = lastDiscovery_.selected();

    ServiceResult result;
    ServiceError error;
    QString message;

    if (selected && selected->valid) {
        DetectedService detected;
        detected.unitName = selected->info.unitName;
        detected.scope = selected->info.scope;
        detected.host = selected->info.host;
        detected.port = selected->info.port;
        detected.valid = true;
        setDetectedService(detected);
        result = ServiceResult::Success;
        error = ServiceError::None;
        message = QStringLiteral("发现已验证的 dsh web 服务: %1")
                      .arg(unitName_);
    } else {
        setDetectedService(DetectedService{});
        if (!discoverySystemctlAvailable_) {
            result = ServiceResult::SystemctlMissing;
            error = ServiceError::SystemctlNotFound;
            message = QStringLiteral("找不到 systemctl 可执行文件");
        } else {
            result = ServiceResult::NotValidated;
            error = ServiceError::TargetNotValidated;
            message = QStringLiteral("未发现通过 LoadState + 官方入口校验的 dsh web 服务");
        }
    }

    emitFinished(ServiceOperation::Discovery, result, error, lastExitCode_,
                 elapsed, message);
    cleanupCurrent();
}

// ---------------------------------------------------------------------------
// 结果清理与上报
// ---------------------------------------------------------------------------

void DshServiceManager::emitFinished(ServiceOperation operation,
                                     ServiceResult result, ServiceError error,
                                     int exitCode, qint64 elapsedMs,
                                     const QString& message) {
    OperationResult r;
    r.operation = operation;
    r.result = result;
    r.error = error;
    r.exitCode = exitCode;
    r.elapsedMs = elapsedMs;
    r.message = message;
    r.stdoutText = stdoutBuffer_;
    r.stderrText = stderrBuffer_;
    emit operationFinished(r);
}

void DshServiceManager::cleanupCurrent() {
    timeoutTimer_->stop();
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
    currentRequestId_ = -1;
    currentOperation_ = ServiceOperation::Status;
    phase_ = Phase::None;
    timedOut_ = false;
    cancelled_ = false;
    finalized_ = true;
}

void DshServiceManager::rejectOperation(ServiceOperation operation,
                                        ServiceResult result,
                                        ServiceError error,
                                        const QString& message) {
    OperationResult r;
    r.operation = operation;
    r.result = result;
    r.error = error;
    r.elapsedMs = -1;
    r.message = message;
    emit operationFinished(r);
}

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

QString DshServiceManager::currentUnit() const {
    return unitName_.isEmpty() ? QStringLiteral("dsh-web.service") : unitName_;
}

void DshServiceManager::appendBuffer(QString& buffer, const QString& chunk) const {
    if (buffer.size() >= kMaxCaptureBytes) return;
    buffer += chunk;
    if (buffer.size() > kMaxCaptureBytes) {
        buffer.remove(0, buffer.size() - kMaxCaptureBytes);
    }
}

QString DshServiceManager::resolveSystemctl() const {
    if (!systemctlExe_.isEmpty()) return systemctlExe_;
    return QStandardPaths::findExecutable(QStringLiteral("systemctl"));
}

QString DshServiceManager::resolveJournalctl() const {
    if (!journalctlExe_.isEmpty()) return journalctlExe_;
    return QStandardPaths::findExecutable(QStringLiteral("journalctl"));
}

QString DshServiceManager::resolvePkexec() const {
    if (!pkexecExe_.isEmpty()) return pkexecExe_;
    return QStandardPaths::findExecutable(QStringLiteral("pkexec"));
}

ServiceStatus DshServiceManager::toServiceStatus(const ServiceInfo& info) const {
    ServiceStatus s;
    s.scope = info.scope;
    s.origin = info.origin;
    s.state = info.state;
    s.unitName = info.unitName;
    s.host = info.host;
    s.port = info.port;
    s.user = info.user;
    s.dshHome = info.dshHome;
    s.mainPid = info.mainPid;
    s.failureReason.clear();
    if (!info.loadState.isEmpty() && info.loadState != QLatin1String("loaded")) {
        s.failureReason = QStringLiteral("LoadState=%1").arg(info.loadState);
    } else if (info.state == LifecycleState::Failed) {
        s.failureReason = info.subState.isEmpty()
            ? QStringLiteral("ActiveState=failed")
            : QStringLiteral("ActiveState=failed, SubState=%1").arg(info.subState);
    }
    s.detail = info.subState.isEmpty()
        ? info.activeState
        : info.activeState + QStringLiteral("/") + info.subState;
    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
// 进程结果映射
//
// 纯命令构造已迁移到 SystemctlCommandBuilder（unit 名校验 / systemctl+
// journalctl argv 构造 / pkexec 包裹），见 service/SystemctlCommandBuilder.h。
// 这里只保留与 ProcessOutcome / ServiceInfo 强绑定的三个映射函数。
// ---------------------------------------------------------------------------

ServiceResult DshServiceManager::mapProcessResult(const ProcessOutcome& outcome) {
    if (outcome.timedOut) return ServiceResult::Timeout;
    if (outcome.cancelled) return ServiceResult::Cancelled;
    if (!outcome.started) return ServiceResult::Failed;
    if (outcome.exitStatus != ProcessExitStatus::Normal) return ServiceResult::Failed;
    if (outcome.exitCode != 0) return ServiceResult::Failed;
    return ServiceResult::Success;
}

ServiceError DshServiceManager::mapProcessError(const ProcessOutcome& outcome) {
    if (outcome.timedOut) return ServiceError::ProcessTimedOut;
    if (outcome.cancelled) return ServiceError::Cancelled;
    if (!outcome.started) return ServiceError::ProcessStartFailed;
    if (outcome.exitStatus != ProcessExitStatus::Normal) return ServiceError::NonZeroExit;
    if (outcome.exitCode != 0) return ServiceError::NonZeroExit;
    return ServiceError::None;
}

ServiceResult DshServiceManager::evaluateManageability(const ServiceInfo& info,
                                                       const QString& currentUser) {
    if (info.loadState != QLatin1String("loaded")) {
        return ServiceResult::NotLoaded;
    }
    if (!info.invokesOfficialDshWeb) {
        return ServiceResult::ForeignService;
    }
    if (info.scope == ServiceScope::User && !currentUser.isEmpty()
        && !info.user.isEmpty() && info.user != currentUser) {
        return ServiceResult::Unmanaged;
    }
    return ServiceResult::Success;
}

}  // namespace dsh::service
