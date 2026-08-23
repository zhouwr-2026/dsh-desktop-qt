// SPDX-License-Identifier: MIT
// @author zhouwr

#include "ServiceProvisioner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace dsh::service {

namespace {

/// 从刚生成的 unit 文本中提取 ``ExecStart=`` 行的值（去除行首前缀与空白），
/// 用于计算所有权记录里的 ExecStart 指纹。
QString extractExecStart(const QString& unitText) {
    const QString prefix = QStringLiteral("ExecStart=");
    const int idx = unitText.indexOf(prefix);
    if (idx < 0) return {};
    const int valueStart = idx + prefix.size();
    const int nl = unitText.indexOf(QLatin1Char('\n'), valueStart);
    QString value = unitText.mid(valueStart, nl < 0 ? -1 : nl - valueStart);
    return value.trimmed();
}

/// 把计划折叠为供给结果（用于 preflight 失败 / 已存在 / 无写入器上报）。
ProvisionResult planToResult(const ProvisionPlan& plan, bool newlyProvisioned) {
    ProvisionResult r;
    r.status = plan.status;
    r.unitName = plan.unitName;
    r.scope = plan.scope;
    r.destinationPath = plan.destinationPath;
    r.unitText = plan.unitText;
    r.error = plan.error;
    r.newlyProvisioned = newlyProvisioned;
    return r;
}

}  // namespace

ServiceProvisioner::ServiceProvisioner(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<ProvisionStatus>("dsh::service::ProvisionStatus");
    qRegisterMetaType<ProvisionPlan>("dsh::service::ProvisionPlan");
    qRegisterMetaType<ProvisionResult>("dsh::service::ProvisionResult");
    qRegisterMetaType<ServiceOperation>("dsh::service::ServiceOperation");
    qRegisterMetaType<ServiceResult>("dsh::service::ServiceResult");
    qRegisterMetaType<ServiceError>("dsh::service::ServiceError");
    qRegisterMetaType<OperationResult>("dsh::service::OperationResult");

    // 把底层 systemd 管理器的 enable / daemon-reload 结果转发给使用者。
    connect(&serviceManager_, &DshServiceManager::operationFinished,
            this, &ServiceProvisioner::operationFinished);
}

ServiceProvisioner::~ServiceProvisioner() = default;

// ---------------------------------------------------------------------------
// 路径（纯函数）
// ---------------------------------------------------------------------------

QString ServiceProvisioner::unitFilename() {
    return ServiceUnitBuilder::unitName();
}

QString ServiceProvisioner::userUnitDirectory(const QString& homePath) {
    const QString home = homePath.isEmpty() ? QDir::homePath() : homePath;
    return home + QStringLiteral("/.config/systemd/user");
}

QString ServiceProvisioner::userUnitPath(const QString& homePath) {
    return userUnitDirectory(homePath) + QLatin1Char('/') + unitFilename();
}

QString ServiceProvisioner::systemUnitPath(const QString& systemDir) {
    const QString dir =
        systemDir.isEmpty() ? QStringLiteral("/etc/systemd/system") : systemDir;
    return dir + QLatin1Char('/') + unitFilename();
}

// ---------------------------------------------------------------------------
// 同步计划（纯函数）
// ---------------------------------------------------------------------------

ProvisionPlan ServiceProvisioner::plan(const ServiceUnitSpec& spec,
                                       const QString& homePath,
                                       const QString& systemDir,
                                       const ISystemUnitWriter* systemWriter) {
    ProvisionPlan plan;
    plan.unitName = ServiceUnitBuilder::unitNameForScope(spec.scope);
    plan.scope = spec.scope;

    // 1) 先做语义校验/生成单元文本。
    const ServiceUnitResult built = ServiceUnitBuilder::build(spec);
    if (!built.ok) {
        plan.status = ProvisionStatus::InvalidSpec;
        plan.error = built.error;
        return plan;
    }
    plan.unitText = built.unitText;

    // 2) 按 scope 确定目标路径。
    plan.destinationPath = (spec.scope == ServiceScope::User)
        ? userUnitPath(homePath)
        : systemUnitPath(systemDir);

    // 3) 绝不覆盖既有单元：目标已存在则保持原样。
    plan.destinationExists = QFileInfo::exists(plan.destinationPath);
    if (plan.destinationExists) {
        plan.status = ProvisionStatus::ExistingUnitUnchanged;
        plan.error = QStringLiteral("目标 unit 已存在，保持原样：") + plan.destinationPath;
        return plan;
    }

    // 4) 系统级必须显式注入受权写入器，绝不静默写 /etc。
    if (spec.scope == ServiceScope::System && systemWriter == nullptr) {
        plan.status = ProvisionStatus::NoSystemWriter;
        plan.error = QStringLiteral(
            "系统级供给需要显式受权写入器（未注入，拒绝写入 /etc）");
        return plan;
    }

    plan.status = ProvisionStatus::Ready;
    return plan;
}

// ---------------------------------------------------------------------------
// 实例配置
// ---------------------------------------------------------------------------

void ServiceProvisioner::setUserHomePath(const QString& homePath) {
    homePath_ = homePath;
}

void ServiceProvisioner::setSystemUnitDir(const QString& dir) {
    systemDir_ = dir;
}

void ServiceProvisioner::setSystemUnitWriter(
    std::shared_ptr<ISystemUnitWriter> writer) {
    systemWriter_ = std::move(writer);
}

void ServiceProvisioner::setOwnershipStatePath(const QString& stateFilePath) {
    ownership_ = ServiceOwnership(stateFilePath);
}

// ---------------------------------------------------------------------------
// 异步 QObject 操作
// ---------------------------------------------------------------------------

qint64 ServiceProvisioner::provision(const ServiceUnitSpec& spec) {
    const ProvisionResult result = executeProvision(spec);
    emit provisionFinished(result);
    // 只有成功（Ready 且真正写入）才发回一个大于 0 的请求 id；
    // 失败 / 已存在 / 无写入器时不启动任何东西，返回 -1。
    const ProvisionStatus status = result.status;
    if (status == ProvisionStatus::Ready && result.newlyProvisioned) {
        return nextRequestId_++;
    }
    return -1;
}

qint64 ServiceProvisioner::daemonReload() {
    if (!newlyProvisioned_) {
        rejectReloadEnable(ServiceOperation::DaemonReload);
        return -1;
    }
    serviceManager_.setUnit(provisionedUnitName_, provisionedScope_);
    return serviceManager_.daemonReload();
}

qint64 ServiceProvisioner::enable() {
    if (!newlyProvisioned_) {
        rejectReloadEnable(ServiceOperation::Enable);
        return -1;
    }
    serviceManager_.setUnit(provisionedUnitName_, provisionedScope_);
    return serviceManager_.enable();
}

// ---------------------------------------------------------------------------
// 供给实现
// ---------------------------------------------------------------------------

ProvisionResult ServiceProvisioner::executeProvision(const ServiceUnitSpec& spec) {
    const ProvisionPlan plan =
        ServiceProvisioner::plan(spec, homePath_, systemDir_, systemWriter_.get());

    // 已验证失败（InvalidSpec / ExistingUnitUnchanged / NoSystemWriter）。
    if (plan.status != ProvisionStatus::Ready) {
        return planToResult(plan, /*newlyProvisioned=*/false);
    }

    // 原子写入（用户级 QSaveFile；系统级经受权写入器）。
    QString writeError;
    const bool wrote =
        writeUnitFile(plan.destinationPath, spec.scope, plan.unitText, &writeError);
    if (!wrote) {
        ProvisionResult r = planToResult(plan, /*newlyProvisioned=*/false);
        r.status = ProvisionStatus::WriteFailed;
        r.error = writeError.isEmpty()
            ? QStringLiteral("写入失败：") + plan.destinationPath
            : writeError;
        return r;
    }

    // 只有写入成功才记录归属（内存 + 落盘，落盘失败不掩盖"单元已写入"）。
    ownership_.record(plan.unitName, plan.scope, extractExecStart(plan.unitText));
    ownership_.save();

    // 放行后续 daemon-reload / enable。
    newlyProvisioned_ = true;
    provisionedUnitName_ = plan.unitName;
    provisionedScope_ = plan.scope;

    ProvisionResult r = planToResult(plan, /*newlyProvisioned=*/true);
    return r;
}

bool ServiceProvisioner::writeUnitFile(const QString& path, ServiceScope scope,
                                       const QString& unitText, QString* error) {
    // 系统级：必须经显式受权写入器，绝不直接写 /etc。
    if (scope == ServiceScope::System) {
        if (!systemWriter_) {
            if (error) {
                *error = QStringLiteral("没有注入系统级受权写入器");
            }
            return false;
        }
        return systemWriter_->writeUnit(path, unitText.toUtf8(), error);
    }

    // 用户级：QSaveFile 原子写，失败不改变目标。
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error) {
            *error = QStringLiteral("无法创建目录：") + dir;
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法打开目标文件：") + path;
        }
        return false;
    }
    const QByteArray data = unitText.toUtf8();
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        if (error) {
            *error = QStringLiteral("写入不完整：") + path;
        }
        return false;
    }
    if (!file.commit()) {
        file.cancelWriting();
        if (error) {
            *error = QStringLiteral("提交失败：") + path;
        }
        return false;
    }
    return true;
}

void ServiceProvisioner::rejectReloadEnable(ServiceOperation operation) {
    OperationResult r;
    r.operation = operation;
    r.result = ServiceResult::InvalidRequest;
    r.error = ServiceError::InvalidRequest;
    r.message = QStringLiteral(
        "没有新写入的 unit；daemon-reload / enable 只对刚补齐的单元放行");
    emit operationFinished(r);
}

}  // namespace dsh::service
