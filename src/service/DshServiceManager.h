// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop — 异步 systemd service 管理器。
//
// 本模块以 Qt QProcess 异步（信号/槽）方式管理系统级与用户级 dsh-web.service
// 单元的只读发现、状态查询与生命周期操作（start / stop / restart），并异步
// 跟进 journal 尾部输出。它只构造并执行显式的命令参数（QProcess 的 program +
// arguments，绝不经 shell），从而避免任何 shell 注入面。
//
// 安全边界：
//   * 本模块绝不「补齐 / 覆盖 / 删除」任何 unit，也绝不写任何 unit 文件。
//     「补齐单元」由 ServiceUnitBuilder / 安装器切片负责；本模块只管理已经
//     被只读发现层验证为官方 dsh web 的既有单元。
//   * 变更类操作（start / stop / restart）只对「已通过官方验证 + 归属策略」的
//     目标开放。目标由调用方用已验证的 DetectedService 设定，或先执行
//     validate()/requestDiscovery() 通过官方验证与归属校验；未验证、外来
//     (Foreign)、不可管理 (Unmanaged) 的目标一律拒绝，绝不静默触碰。
//   * 系统级变更在非 root 下经 /usr/bin/pkexec（polkit）授权，且始终以显式
//     参数列表调用，绝不拼接 shell。
//
// 并发模型：同一时刻只执行一个操作（拒绝重叠，返回 Busy）。每个异步操作都
// 通过 operationStarted / operationOutput / operationFinished 上报进度与结果，
// 带可配置的有界超时与主动取消（cancel()/stopJournalTail()）。
//
// 命令参数构建与结果映射均为纯静态函数，不接触进程，便于单元测试。

#pragma once

#include "ServiceInfo.h"
#include "ServiceDiscovery.h"
#include "ServiceOperation.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

namespace dsh::service {

/// 高层操作结果（operationFinished 的 result 字段）。
enum class ServiceResult {
    Success,          ///< 成功。
    Failed,           ///< 进程以非零退出码结束，或其它未被细分失败。
    Timeout,          ///< 操作超时。
    Cancelled,        ///< 被 cancel()/stopJournalTail() 主动取消。
    InvalidUnit,      ///< 目标 unit 名非法。
    NotValidated,     ///< 目标尚未通过官方验证 / 归属校验。
    ForeignService,   ///< 目标 ExecStart 非官方 dsh web，拒管理。
    Unmanaged,        ///< 目标属于其他用户，只读、不可管理。
    NotLoaded,        ///< 目标 LoadState 不是 loaded。
    SystemctlMissing, ///< 找不到 systemctl 可执行文件。
    JournalctlMissing,///< 找不到 journalctl 可执行文件。
    BusUnavailable,   ///< systemd 系统/用户总线不可用。
    Busy,             ///< 已有操作正在进行。
    InvalidRequest,   ///< 目标未设置 / 参数非法。
};

/// 细粒度错误码（供日志 / 展示 / 结果映射）。
enum class ServiceError {
    None,                ///< 无错误。
    UnitNameInvalid,     ///< unit 名非法。
    TargetNotValidated,  ///< 目标未通过验证。
    TargetForeign,       ///< 外来服务。
    TargetUnmanaged,     ///< 不可管理的其他用户服务。
    LoadStateNotLoaded,  ///< LoadState 不是 loaded。
    SystemctlNotFound,   ///< 找不到 systemctl。
    JournalctlNotFound,  ///< 找不到 journalctl。
    BusUnavailable,      ///< 总线不可用。
    ProcessStartFailed,  ///< 进程启动失败。
    ProcessTimedOut,     ///< 进程超时。
    Cancelled,           ///< 主动取消。
    NonZeroExit,         ///< 非零退出码。
    OutputParseFailed,   ///< 输出解析失败。
    OverlappingOperation,///< 重叠操作。
    InvalidRequest,      ///< 请求非法。
};

/// 一次异步操作的完整结果（operationFinished 参数）。
struct OperationResult {
    ServiceOperation operation{ServiceOperation::Status};
    ServiceResult result{ServiceResult::Failed};
    ServiceError error{ServiceError::None};
    int exitCode{0};         ///< QProcess::exitCode()；未启动/被取消时为 0。
    qint64 elapsedMs{-1};    ///< 操作耗时；未完成时为 -1。
    QString message;         ///< 人类可读的结果 / 错误说明。
    QString stdoutText;      ///< 捕获到的完整 stdout。
    QString stderrText;      ///< 捕获到的完整 stderr。
};

/// ``ProcessOutcome`` 用的退出状态枚举，独立于 Qt 的 ``QProcess`` 类型，
/// 让本头不再需要 include ``<QProcess>``（避免传染其它 include 此头的 TU）。
/// ``fromQProcessExitStatus`` 在 .cpp 内部做映射。
/// (变更理由: 头文件卫生，``DshServiceManager.h`` 移除 ``<QProcess>`` 传染)
enum class ProcessExitStatus { Normal, Crashed };

/// ``ProcessOutcome`` 用的进程错误枚举，覆盖 ``QProcess::ProcessError`` 的
/// 子集。``fromQProcessError`` 在 .cpp 内部做映射。
enum class ProcessErrorCode {
    None,            ///< 无错误（成功退出或仍在运行）
    FailedToStart,   ///< 进程无法启动（程序缺失 / 权限不足）
    Crashed,         ///< 进程崩溃（被信号杀）
    Timedout,        ///< waitFor* 超时
    WriteError,      ///< 写管道失败
    ReadError,       ///< 读管道失败
    Unknown,         ///< 其它未分类错误
};

/// 进程运行原始结果（供 mapProcessResult / mapProcessError 映射）。纯数据。
struct ProcessOutcome {
    bool started{true};                       ///< 进程是否成功启动。
    bool timedOut{false};                     ///< 是否超时。
    bool cancelled{false};                    ///< 是否被主动取消。
    ProcessExitStatus exitStatus{ProcessExitStatus::Normal};
    int exitCode{0};
    ProcessErrorCode processError{ProcessErrorCode::Unknown};
};

// 注：ResolvedCommand struct 已迁移到 SystemctlCommandBuilder.h

/// 只读状态查询的结果（requestStatus 通过 statusResult 信号上报）。
struct ServiceStatus {
    bool valid{false};                       ///< 快照是否有效（解析成功）。
    bool running{false};                     ///< 是否运行中（ActiveState=active）。
    bool manageable{false};                  ///< 桌面端能否管理其生命周期。
    ServiceScope scope{ServiceScope::System};
    ServiceOrigin origin{ServiceOrigin::ExistingOfficial};
    LifecycleState state{LifecycleState::Unknown};
    QString unitName;
    QString detail;                          ///< 人类可读状态描述。
    QString host{kDefaultHost};
    int port{kDefaultPort};
    QString user;
    QString dshHome;
    QString failureReason;
    QString journalSummary;
    qint64 mainPid{-1};
};

/// 异步 systemd service 管理器。其余约定见文件头注释。
class DshServiceManager : public QObject {
    Q_OBJECT
public:
    explicit DshServiceManager(QObject* parent = nullptr);
    ~DshServiceManager() override;

    // -------------------------------------------------------------------
    // 目标配置
    // -------------------------------------------------------------------

    /// 用只读发现层已验证的结果设定目标。``detected.valid`` 为真时目标被
    /// 标记为已通过官方验证（可直接变更）；为假时保持未验证，变更会被拒绝。
    void setDetectedService(const DetectedService& detected);

    /// 用 unit 名 + scope 设定目标。目标初始为「未验证」，需先执行
    /// validate()/requestDiscovery() 通过官方验证与归属校验后方可变更；
    /// 变更类操作在未验证时会以 NotValidated 拒绝。
    bool setUnit(const QString& unitName, ServiceScope scope);

    /// 是否存在已设定的目标。
    bool hasTarget() const { return hasTarget_; }

    /// 当前目标 unit 名。
    QString unitName() const { return unitName_; }

    /// 当前目标范围。
    ServiceScope scope() const { return scope_; }

    /// 当前目标是否已通过官方验证 / 归属校验。
    bool isValidated() const { return validated_; }

    /// 设定「当前用户」用于归属判定（默认取环境变量）。测试可注入确定性值。
    void setCurrentUser(const QString& user) { currentUser_ = user.trimmed(); }

    /// 当前用于归属判定的用户。
    QString currentUser() const { return currentUser_; }

    /// 最近一次只读发现结果（candidates 与 selectedIndex）；未运行过为默认值。
    const DiscoveryResult& lastDiscovery() const { return lastDiscovery_; }

    /// 最近一次只读状态快照；未运行过为默认值。
    const ServiceStatus& lastStatus() const { return lastStatus_; }

    // -------------------------------------------------------------------
    // 超时 / 取消
    // -------------------------------------------------------------------

    /// 设置一次性操作的超时（毫秒）。默认 20000；journal 跟随模式不受限。
    void setOperationTimeoutMs(int ms);
    int operationTimeoutMs() const { return operationTimeoutMs_; }

    /// 是否正有一个操作在运行。
    bool isBusy() const { return currentRequestId_ >= 0; }

    /// 当前运行中操作的请求 id；无操作时为 -1。
    qint64 currentRequestId() const { return currentRequestId_; }

    /// 主动取消指定请求（-1 表示取消当前操作）。只对进行中的操作生效。
    void cancel(qint64 requestId = -1);

    // -------------------------------------------------------------------
    // 可执行文件覆盖（测试 / 嵌入式环境用）
    // -------------------------------------------------------------------

    void setSystemctlExecutable(const QString& path) { systemctlExe_ = path; }
    void setJournalctlExecutable(const QString& path) { journalctlExe_ = path; }
    void setPkexecExecutable(const QString& path) { pkexecExe_ = path; }

    // -------------------------------------------------------------------
    // 异步操作：返回请求 id；请求被拒绝时返回 -1 并通过 operationFinished
    // 以对应 ServiceResult 上报原因。
    // -------------------------------------------------------------------

    /// 只读发现：系统域 + 用户域各执行一次 systemctl show，解析、按官方入口
    /// 与 LoadState 校验并选出唯一目标；成功后更新本管理器目标。
    qint64 requestDiscovery();

    /// 只读状态：对当前目标执行 systemctl show 并解析为 ServiceStatus。
    qint64 requestStatus();

    /// 启动目标服务（需已验证）。
    qint64 start();

    /// 停止目标服务（需已验证）。
    qint64 stop();

    /// 重启目标服务（需已验证）。
    qint64 restart();

    /// 重新加载 systemd 管理器配置（``systemctl daemon-reload``）。
    ///
    /// 只让 systemd 重新扫描 unit 目录，不启动/不启用任何服务。用于"刚补齐
    /// 一个 unit 后刷新配置"。系统级在非 root 下经 pkexec 提权；用户级无需提权。
    /// 不要求目标已通过官方验证（不属于对既有服务的管理）。
    qint64 daemonReload();

    /// 启用目标单元（``systemctl enable <unit>``）：设置开机自启，绝不 start。
    ///
    /// 仅用于"刚补齐且已成功写入"的单元；系统级在非 root 下经 pkexec 提权，
    /// 用户级无需提权。不要求目标已通过官方验证（不属于对既有服务的管理）。
    qint64 enable();

    /// 异步跟进目标服务 journal 尾部（长期运行）。返回请求 id；失败返回 -1。
    qint64 startJournalTail(int lines = 50);

    /// 停止 journal 尾部跟随。成功取消返回 true；未在跟随返回 false。
    bool stopJournalTail();

    // -------------------------------------------------------------------
    // 进程结果映射（不接触进程，便于单元测试）
    // -------------------------------------------------------------------
    //
    // 纯命令构造类已迁移到 SystemctlCommandBuilder
    // （unit 名校验 / systemctl/journalctl argv 构造 / pkexec 包裹），
    // 减小本类职责面；剩下三个函数强依赖 ProcessOutcome 与 ServiceInfo，
    // 留在本类更自然。

    /// 把进程运行原始结果映射为高层 ServiceResult。
    static ServiceResult mapProcessResult(const ProcessOutcome& outcome);

    /// 把进程运行原始结果映射为细粒度 ServiceError。
    static ServiceError mapProcessError(const ProcessOutcome& outcome);

    /// 官方验证 / 归属策略：给定已解析快照，判定目标是否允许被本管理器管理。
    /// 仅当 LoadState=loaded、ExecStart 调用官方 dsh web、且（用户级单元）属于
    /// 当前用户时返回 Success；否则返回对应拒绝码。纯函数。
    static ServiceResult evaluateManageability(const ServiceInfo& info,
                                               const QString& currentUser);

signals:
    /// 一个异步操作开始执行（preflight 拒绝的请求不发出本信号）。
    void operationStarted(dsh::service::ServiceOperation operation);

    /// 操作进行中的增量输出（stdout 与 stderr 合并上报）。
    void operationOutput(dsh::service::ServiceOperation operation,
                         const QString& chunk);

    /// 操作结束（成功或失败），携带完整结果。
    void operationFinished(const dsh::service::OperationResult& result);

    /// journal 尾部每输出一行。
    void journalTailLine(const QString& line);

    /// 只读状态查询的结果。
    void statusResult(const dsh::service::ServiceStatus& status);

private:
    enum class Phase {
        None,
        DiscoverySystem,  // 发现：系统域 show
        DiscoveryUser,    // 发现：用户域 show
    };

    bool canStartOperation() const;
    void rejectOperation(ServiceOperation operation, ServiceResult result,
                         ServiceError error, const QString& message);
    qint64 launchOneShot(ServiceOperation operation, const QString& program,
                         const QStringList& arguments);
    qint64 launchMutate(ServiceOperation operation);
    void beginOperation(ServiceOperation operation);
    void startProcess(const QString& program, const QStringList& arguments);
    void onProcessFinished(bool started);
    void handleDiscoveryFinished(bool started, int exitCode);
    void finalizeDiscovery();
    void cleanupCurrent();
    void emitFinished(ServiceOperation operation, ServiceResult result,
                      ServiceError error, int exitCode, qint64 elapsedMs,
                      const QString& message);
    ServiceStatus toServiceStatus(const ServiceInfo& info) const;
    QString resolveSystemctl() const;
    QString resolveJournalctl() const;
    QString resolvePkexec() const;
    QString currentUnit() const;
    void appendBuffer(QString& buffer, const QString& chunk) const;

    // 目标状态
    QString unitName_;
    ServiceScope scope_{ServiceScope::User};
    bool hasTarget_{false};
    bool validated_{false};
    QString currentUser_;

    // 进程执行状态
    QProcess* process_{nullptr};
    QTimer* timeoutTimer_{nullptr};
    QElapsedTimer elapsed_;
    qint64 nextRequestId_{1};
    qint64 currentRequestId_{-1};
    ServiceOperation currentOperation_{ServiceOperation::Status};
    Phase phase_{Phase::None};
    bool timedOut_{false};
    bool cancelled_{false};
    bool finalized_{false};
    bool processStarted_{false};
    int lastExitCode_{0};
    ProcessExitStatus lastExitStatus_{ProcessExitStatus::Normal};
    ProcessErrorCode lastProcessError_{ProcessErrorCode::Unknown};

    QString stdoutBuffer_;
    QString stderrBuffer_;
    QString journalLineBuffer_;

    // 发现中间态
    QVector<DiscoveredService> discoveryCandidates_;
    bool discoverySystemctlAvailable_{true};

    // 可执行文件覆盖
    QString systemctlExe_;
    QString journalctlExe_;
    QString pkexecExe_;

    // 结果缓存
    DiscoveryResult lastDiscovery_;
    ServiceStatus lastStatus_;

    int operationTimeoutMs_{20000};
};

}  // namespace dsh::service

Q_DECLARE_METATYPE(dsh::service::ServiceOperation)
Q_DECLARE_METATYPE(dsh::service::ServiceResult)
Q_DECLARE_METATYPE(dsh::service::ServiceError)
Q_DECLARE_METATYPE(dsh::service::OperationResult)
Q_DECLARE_METATYPE(dsh::service::ServiceStatus)
