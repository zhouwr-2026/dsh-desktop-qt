// SPDX-License-Identifier: MIT
// @author zhouwr
// dsh web 后端主管。
//
// 三种实现：
//   * ``SystemdBackend``（默认）：对接 ``/etc/systemd/system/dsh-web.service``
//     （或 ``~/.config/systemd/user/dsh-web.service``）。
//   * ``SupervisedBackend``：在无 systemd unit 时直接拉起 ``dsh web`` 子进程。
//   * 外部模式：连接远程 dsh-web，不管理本机服务生命周期。
//
// 抽象类 ``Backend`` 是上层 UI 唯一的对外接口。

#pragma once

#include "service/ServiceInfo.h"

#include <QObject>
#include <QString>
#include <memory>

namespace dsh::backend {

enum class Mode {
    Systemd,     // 由 systemd 托管
    Supervised,  // 由桌面端拉起的子进程
    External,    // 远程后端，不由桌面端管理
};

struct Status {
    bool running{false};
    Mode mode{Mode::Systemd};
    QString url;          // 例如 http://127.0.0.1:3080
    QString detail;       // 人类可读的描述（systemd state / 子进程 pid 等）
    int activeTasks{0};   // 后端探测到的活跃任务数（粗略）

    // -------------------------------------------------------------------
    // 服务元数据：为上层 UI 提供"这是什么服务，处于什么状态，能否由桌面端
    // 管理"的完整画像。复用 dsh::service 的枚举/类型，保持与只读发现层一致。
    //
    // 这里只保存"展示性"字段；具体如何从 systemctl/状态文件派生出这些值由
    // 各后端在 status() 中按自身上下文填充。默认值即"未知/兜底"语义：
    // 空串表示"不可用/未设置"，调用方按需补默认值。
    // -------------------------------------------------------------------
    dsh::service::ServiceScope scope{dsh::service::ServiceScope::System};
    dsh::service::ServiceOrigin origin{dsh::service::ServiceOrigin::ExistingOfficial};
    dsh::service::LifecycleState state{dsh::service::LifecycleState::Unknown};
    bool manageable{false};   // 桌面端是否能管理其生命周期（start/stop/restart）
    QString owner;            // 服务所有者 / 运行用户
    QString dshHome;          // DSH_HOME；为空表示未设置（由调用方应用默认）
    QString failureReason;    // 失败原因；正常运行时为空
    QString journalSummary;   // journal 最近的日志摘要；为空表示不可用
};

/// 当前登录用户名（用于服务所有者 / 运行用户展示）。
QString currentUserName();

/// 用只读服务快照填充 ``Status`` 中可直接派生/展示的服务元数据字段。
///
/// 纯函数：不启动进程、不读磁盘、不查询 systemd 或所有权记录，只根据传入的
/// ``dsh::service::ServiceInfo`` 派生：scope、state、owner、dshHome 与
/// failureReason。以下字段由调用方按各自上下文填充，本函数不强加：
///   * running / mode / url / detail / activeTasks（各自的 status() 语义）
///   * origin（是否由桌面端补齐 / 外部 / 兜底，需要所有权或后端知识）
///   * manageable（是否可管理，需要后端知识）
///   * journalSummary（来自 journalctl 之类的外部只读数据，非快照派生）
///
/// \param currentUser 当前用户；当快照的 ``User`` 为空且服务是用户级时用于
///                    填充 ``owner``。保持参数化便于纯测试注入确定性值。
void applyServiceMetadata(Status& status,
                          const dsh::service::ServiceInfo& info,
                          const QString& currentUser = QString());

/// 从 journal 摘要识别常见的 profile 构建产物缺失，并返回可执行的修复提示。
QString profileRepairHint(const QString& journalSummary);

/// 将单次健康探测折叠为稳定状态：成功立即生效并清零失败计数；失败只有连续
/// 达到 ``failureThreshold`` 次才生效，避免短暂 TCP 抖动触发停服通知。
bool backendHealthObservationStable(bool running,
                                    int& consecutiveFailures,
                                    int failureThreshold);

/// 将 systemd InvocationID 转成 journalctl 精确字段匹配；非法或空 ID 返回空串。
QString systemdInvocationJournalMatch(const QString& invocationId);

/// 启动路径的决策判定：systemd 模式下当官方后端处于 ``Inactive``/``Failed``
/// 时，桌面端不应静默自动拉起，而应先询问用户是否启动（尊重用户有意停用或
/// 正在修复失败服务的意图）。
///
/// 纯函数：只根据传入的 ``Status`` 判定，不启动进程、不读状态、不查询
/// systemd，因此可被单元测试稳定调用。
///
/// \return true 表示需要用户确认后再调用 ``Backend::start()``。
bool requiresStartConfirmation(const Status& status);

class Backend : public QObject {
    Q_OBJECT
public:
    /// 默认 URL：官方 DSH web 默认绑定的 loopback 地址 + 端口。
    static QString defaultUrl();

    /// 根据当前系统环境选择最合适的后端实现：发现 systemd unit 优先用
    /// systemd，否则退化为子进程模式。
    static std::unique_ptr<Backend> createForHost(
        const QString& url = QString(), QObject* parent = nullptr);

    explicit Backend(QObject* parent = nullptr) : QObject(parent) {}
    ~Backend() override = default;

    virtual Mode mode() const = 0;
    virtual QString url() const = 0;

    /// \return true 表示 ``http://<url>/`` 在超时内返回 2xx/3xx/4xx。
    virtual bool isRunning() const = 0;

    virtual Status status() = 0;

    /// 启动后端，幂等。
    virtual bool start() = 0;

    /// 停止后端，幂等。``force=true`` 时使用更激进的关闭方式。
    virtual bool stop(bool force = false) = 0;

    /// ``stop + start``。
    virtual bool restart() = 0;

signals:
    void log(const QString& message);
};

}  // namespace dsh::backend

Q_DECLARE_METATYPE(dsh::backend::Status)
