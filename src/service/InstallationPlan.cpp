// SPDX-License-Identifier: MIT
// @author zhouwr
#include "InstallationPlan.h"

#include <utility>

namespace dsh::service {

namespace {

/// 是否存在"不应被桌面端启动/覆盖"的服务：显式标记为 foreign/unmanaged，
/// 或"存在但未通过官方 + 归属验证"（serviceOfficial 为 false 的
/// Active/Inactive/Failed 也按非官方处理，绝不启动/覆盖）。
bool isForeignLike(const InstallationContext& context) {
    if (context.service == ServiceState::Foreign
        || context.service == ServiceState::Unmanaged) {
        return true;
    }
    if (context.service == ServiceState::Active
        || context.service == ServiceState::Inactive
        || context.service == ServiceState::Failed) {
        return !context.serviceOfficial;
    }
    return false;
}

}  // namespace

InstallationPlan::InstallationPlan(const InstallationContext& context,
                                   QVector<InstallationStage> stages,
                                   bool blocked,
                                   bool needsConsent)
    : context_(context),
      stages_(std::move(stages)),
      blocked_(blocked),
      needsConsent_(needsConsent) {}

InstallationStage InstallationPlan::primaryStage() const {
    return stages_.isEmpty() ? InstallationStage::DoNothing : stages_.first();
}

ServiceOrigin InstallationPlan::resultingOrigin() const {
    if (stages_.contains(InstallationStage::ProvisionService)) {
        return ServiceOrigin::ProvisionedByDesktop;
    }
    if (stages_.contains(InstallationStage::UseSupervisedFallback)) {
        return ServiceOrigin::SupervisedFallback;
    }
    return context_.origin;
}

ServiceScope InstallationPlan::provisionScope() const {
    return context_.shareWithOtherUsers ? ServiceScope::System : ServiceScope::User;
}

InstallationPlan InstallationPlan::make(const InstallationContext& context) {
    QVector<InstallationStage> stages;

    // 规则 6：systemd 或用户总线不可用（Unavailable）时，一律走受管子进程
    // 兜底。若 CLI 缺失，先安装官方包再兜底，否则仅兜底。
    const bool systemdUsable =
        context.systemdAvailable && context.service != ServiceState::Unavailable;
    if (!systemdUsable) {
        if (context.cli == CliState::Missing) {
            stages.append(InstallationStage::InstallOfficialDsh);
        }
        stages.append(InstallationStage::UseSupervisedFallback);
        return InstallationPlan(context, std::move(stages),
                                /*blocked=*/false, /*needsConsent=*/false);
    }

    // 规则 5：foreign/unmanaged（含"存在但非官方"）的服务，桌面端绝不自启动、
    // 不覆盖，按只读/不可管理处理。
    if (isForeignLike(context)) {
        stages.append(InstallationStage::DoNothing);
        return InstallationPlan(context, std::move(stages),
                                /*blocked=*/true, /*needsConsent=*/false);
    }

    // 规则 1：已有有效官方服务（Active）绝不覆盖，直接复用。
    if (context.service == ServiceState::Active) {
        stages.append(InstallationStage::ReuseExistingService);
        return InstallationPlan(context, std::move(stages),
                                /*blocked=*/false, /*needsConsent=*/false);
    }

    // 规则 2：inactive/failed 的已有官方服务，必须获得"启动授权"才启动；
    // 未授权时保持停止，仅读给出服务管理入口。
    if (context.service == ServiceState::Inactive
        || context.service == ServiceState::Failed) {
        const bool needsConsent = true;
        if (context.consentToStartExisting) {
            stages.append(InstallationStage::StartExistingServiceWithConsent);
        } else {
            stages.append(InstallationStage::DoNothing);
        }
        return InstallationPlan(context, std::move(stages),
                                /*blocked=*/false, needsConsent);
    }

    // 规则 3/4：无任何可复用服务（Missing）时补齐（创建标准 unit）；若 CLI
    // 缺失，先安装官方 @deepseek-ai/dsh 再补齐。
    if (context.cli == CliState::Missing) {
        stages.append(InstallationStage::InstallOfficialDsh);
    }
    stages.append(InstallationStage::ProvisionService);
    return InstallationPlan(context, std::move(stages),
                            /*blocked=*/false, /*needsConsent=*/false);
}

InstallationPlan makeInstallationPlan(const InstallationContext& context) {
    return InstallationPlan::make(context);
}

QString cliStateToString(CliState state) {
    switch (state) {
        case CliState::Missing: return QStringLiteral("Missing");
        case CliState::Installed: return QStringLiteral("Installed");
    }
    return QStringLiteral("Unknown");
}

QString serviceStateToString(ServiceState state) {
    switch (state) {
        case ServiceState::Missing: return QStringLiteral("Missing");
        case ServiceState::Active: return QStringLiteral("Active");
        case ServiceState::Inactive: return QStringLiteral("Inactive");
        case ServiceState::Failed: return QStringLiteral("Failed");
        case ServiceState::Foreign: return QStringLiteral("Foreign");
        case ServiceState::Unmanaged: return QStringLiteral("Unmanaged");
        case ServiceState::Unavailable: return QStringLiteral("Unavailable");
    }
    return QStringLiteral("Unknown");
}

QString installationStageToString(InstallationStage stage) {
    switch (stage) {
        case InstallationStage::InstallOfficialDsh:
            return QStringLiteral("InstallOfficialDsh");
        case InstallationStage::ReuseExistingService:
            return QStringLiteral("ReuseExistingService");
        case InstallationStage::StartExistingServiceWithConsent:
            return QStringLiteral("StartExistingServiceWithConsent");
        case InstallationStage::ProvisionService:
            return QStringLiteral("ProvisionService");
        case InstallationStage::UseSupervisedFallback:
            return QStringLiteral("UseSupervisedFallback");
        case InstallationStage::DoNothing:
            return QStringLiteral("DoNothing");
    }
    return QStringLiteral("Unknown");
}

}  // namespace dsh::service
