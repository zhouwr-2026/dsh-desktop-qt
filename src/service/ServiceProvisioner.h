// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop — 原子补齐 ``dsh-web.service`` 的服务供给器。
//
// 本模块把 "ServiceUnitBuilder 生成的单元文本" 安全地写成一个新的 systemd
// unit，并负责之后的一次性收尾（daemon-reload / enable）。它严格遵循以下规则：
//
//   * 绝不覆盖既有单元：目标文件已存在时返回 ``ExistingUnitUnchanged`` 且
//     不做任何写入、不启用、不重启。
//   * 用户级 (User) 写到 ``~/.config/systemd/user/dsh-web.service``。系统级
//     (System) 绝不直接写 /etc，必须通过显式注入的受权写入器
//     （ISystemUnitWriter，绝不经 shell）；未注入写入器时拒绝（NoSystemWriter）。
//   * 写入用 QSaveFile 原子完成（用户级），失败时不改变目标并返回 WriteFailed。
//   * 只有 "本次确实新写入了一个 unit"（newlyProvisioned）后，才允许发起
//     daemon-reload / enable；这两步通过 DshServiceManager 以显式命令下发，
//     且绝不 start 任何既有服务。
//   * 只有写入成功后才通过 ServiceOwnership 记录归属（unit 名 + scope +
//     ExecStart 指纹）。
//
// 静态的 ``plan`` / 路径辅助均为纯函数（不写盘、不执行进程），便于单元测试；
// 实例提供异步式 QObject 操作（返回请求 id、发出 provisionFinished /
// operationFinished），其中写入本身同步、reload/enable 经 DshServiceManager
// 异步执行。

#pragma once

#include "DshServiceManager.h"
#include "ServiceInfo.h"
#include "ServiceOwnership.h"
#include "ServiceUnitBuilder.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

namespace dsh::service {

/// 供给动作判定结果。
enum class ProvisionStatus {
    Ready,                  // 校验通过、目标不存在，允许写入。
    ExistingUnitUnchanged,  // 目标已存在，保持原样，不写入、不启用、不重启。
    InvalidSpec,            // ServiceUnitBuilder 校验失败。
    NoSystemWriter,         // 系统级但未注入受权写入器，拒绝（绝不写 /etc）。
    WriteFailed,            // 写入失败（QSaveFile / 受权写入器返回失败）。
    NotProvisioned,         // 本会话没有"新写入的 unit"，reload/enable 被拒。
};

/// 同步计划结果：校验/路径决策（纯数据，无副作用）。
struct ProvisionPlan {
    ProvisionStatus status{ProvisionStatus::InvalidSpec};
    QString unitName;               // ``dsh-web.service``
    ServiceScope scope{ServiceScope::User};
    QString destinationPath;        // 目标 unit 文件绝对路径。
    QString unitText;               // 校验通过的单元文本（InvalidSpec 时为空）。
    QString error;                  // 失败/拒绝原因。
    bool destinationExists{false};  // 目标文件是否已存在。
};

/// 一次供给操作的完整结果（provisionFinished 上报）。
struct ProvisionResult {
    ProvisionStatus status{ProvisionStatus::Ready};
    QString unitName;
    ServiceScope scope{ServiceScope::User};
    QString destinationPath;
    QString unitText;
    QString error;
    bool newlyProvisioned{false};   // 本次是否真的写入了一个新 unit。
};

/// 系统级 unit 文件写入抽象：以受权方式（root 直写、pkexec+tee 等）原子写入，
/// 绝不拼接 shell。实现方自行决定提权与原子写细节。
class ISystemUnitWriter {
public:
    virtual ~ISystemUnitWriter() = default;

    /// 原子写入 ``contents`` 到 ``path``（覆盖语义，由调用方保证目标不存在）。
    /// 成功返回 true；失败把原因写入 \p error（可空）。
    virtual bool writeUnit(const QString& path, const QByteArray& contents,
                           QString* error) = 0;
};

/// 原子补齐 dsh-web.service 的服务供给器。其余约定见文件头注释。
class ServiceProvisioner : public QObject {
    Q_OBJECT
public:
    explicit ServiceProvisioner(QObject* parent = nullptr);
    ~ServiceProvisioner() override;

    // -----------------------------------------------------------------------
    // 路径（纯函数，不写盘）
    // -----------------------------------------------------------------------

    /// unit 文件名（用户级/系统级共用）：``dsh-web.service``。
    static QString unitFilename();

    /// 用户级 unit 目录：``~/.config/systemd/user``；\p homePath 为空时取家目录。
    static QString userUnitDirectory(const QString& homePath = QString());

    /// 用户级 unit 文件绝对路径：``<home>/.config/systemd/user/dsh-web.service``。
    static QString userUnitPath(const QString& homePath = QString());

    /// 系统级 unit 文件绝对路径：``<systemDir>/dsh-web.service``，\p systemDir
    /// 为空时取 ``/etc/systemd/system``。
    static QString systemUnitPath(const QString& systemDir = QString());

    // -----------------------------------------------------------------------
    // 同步校验 / 计划（纯函数，不写盘、不执行进程）
    // -----------------------------------------------------------------------

    /// 校验 spec 并做路径决策。\p homePath / \p systemDir 为空时使用默认值；
    /// \p systemWriter 为系统级受权写入器（系统级且目标不存在时必须非空）。
    ///
    /// 优先级：spec 非法 -> InvalidSpec；目标文件已存在 -> ExistingUnitUnchanged；
    /// 系统级且无写入器 -> NoSystemWriter；否则 Ready。
    static ProvisionPlan plan(const ServiceUnitSpec& spec,
                              const QString& homePath = QString(),
                              const QString& systemDir = QString(),
                              const ISystemUnitWriter* systemWriter = nullptr);

    // -----------------------------------------------------------------------
    // 实例配置（测试注入）
    // -----------------------------------------------------------------------

    /// 设置用户级 unit 的家目录根（默认取 QDir::homePath()）。
    void setUserHomePath(const QString& homePath);

    /// 设置系统级 unit 目录（默认 /etc/systemd/system）。
    void setSystemUnitDir(const QString& dir);

    /// 注入系统级受权写入器。未注入时系统级供给被拒绝（NoSystemWriter）。
    void setSystemUnitWriter(std::shared_ptr<ISystemUnitWriter> writer);

    /// 设定所有权状态文件路径（默认
    /// ``ServiceOwnership::defaultStateFilePath()``；测试传临时路径）。
    void setOwnershipStatePath(const QString& stateFilePath);

    // -----------------------------------------------------------------------
    // 访问器
    // -----------------------------------------------------------------------

    /// 本次会话是否已经真正写入了一个新 unit（只有为真时才允许 reload/enable）。
    bool newlyProvisioned() const { return newlyProvisioned_; }

    /// 最近一次成功写入的 unit 名；未写入时为空。
    QString provisionedUnitName() const { return provisionedUnitName_; }

    /// 最近一次成功写入的 scope；未写入时默认 User。
    ServiceScope provisionedScope() const { return provisionedScope_; }

    /// 所有权记录存储（用于查询/测试，非线程安全）。
    ServiceOwnership& ownership() { return ownership_; }

    /// 供 reload/enable 使用的底层 systemd 管理器。
    DshServiceManager& serviceManager() { return serviceManager_; }

    // -----------------------------------------------------------------------
    // 异步 QObject 操作（返回请求 id；被拒时返回 -1 并通过信号上报原因）
    // -----------------------------------------------------------------------

    /// 原子补齐一个 unit（用户级 QSaveFile；系统级经受权写入器）。
    ///
    /// 校验失败 / 目标已存在 / 系统级无写入器 / 写入失败时均不写盘并立即通过
    /// ``provisionFinished`` 上报对应状态；只有写入成功才记录归属并把
    /// ``newlyProvisioned`` 置真（随后才允许 daemonReload()/enable()）。
    qint64 provision(const ServiceUnitSpec& spec);

    /// 刷新 systemd 配置（``systemctl daemon-reload``），仅对"刚写入的 unit"放行。
    qint64 daemonReload();

    /// 启用开机自启（``systemctl enable <unit>``），仅对"刚写入的 unit"放行，
    /// 绝不 start。
    qint64 enable();

signals:
    /// 一次供给操作结束（成功或失败），携带完整结果。
    void provisionFinished(const dsh::service::ProvisionResult& result);

    /// 转发自底层 DshServiceManager 的 daemon-reload / enable 结果。
    void operationFinished(const dsh::service::OperationResult& result);

private:
    ProvisionResult executeProvision(const ServiceUnitSpec& spec);
    bool writeUnitFile(const QString& path, ServiceScope scope,
                       const QString& unitText, QString* error);
    /// reload/enable 在没有新写入 unit 时同步拒绝并上报。
    void rejectReloadEnable(ServiceOperation operation);

    QString homePath_;            // 用户级 unit 的家目录根（默认 QDir::homePath()）。
    QString systemDir_;           // 系统级 unit 目录（默认 /etc/systemd/system）。
    std::shared_ptr<ISystemUnitWriter> systemWriter_;
    ServiceOwnership ownership_;
    DshServiceManager serviceManager_;
    bool newlyProvisioned_{false};
    QString provisionedUnitName_;
    ServiceScope provisionedScope_{ServiceScope::User};
    qint64 nextRequestId_{1};
};

}  // namespace dsh::service

Q_DECLARE_METATYPE(dsh::service::ProvisionStatus)
Q_DECLARE_METATYPE(dsh::service::ProvisionPlan)
Q_DECLARE_METATYPE(dsh::service::ProvisionResult)
