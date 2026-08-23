// SPDX-License-Identifier: MIT
// @author zhouwr

#include "UninstallPlan.h"

#include <utility>

namespace dsh::service {

namespace {

/// 后台是否"由桌面端拥有且未被改动"：已检测到 service、来源为
/// ProvisionedByDesktop、且归属一致性为 Match（所有权记录中的 ExecStart
/// 指纹与当前实测一致）。
bool isOwnedAndMatched(const UninstallContext& context) {
    return context.backendDetected
        && context.origin == ServiceOrigin::ProvisionedByDesktop
        && context.ownershipConsistency == ConsistencyResult::Match;
}

/// 后台是否"允许被卸载"：在 ownedAndMatched 的基础上，还要求目标
/// scope/path 已知、且服务文件/数据可移除。
bool canRemoveBackend(const UninstallContext& context) {
    return isOwnedAndMatched(context)
        && context.targetScopePathKnown
        && context.serviceRemovalAvailable;
}

}  // namespace

UninstallPlan::UninstallPlan(const UninstallContext& context,
                             UninstallAction action,
                             bool desktopRemoval,
                             bool backendRemoval,
                             bool backendRetained,
                             bool blocked)
    : context_(context),
      action_(action),
      desktopRemoval_(desktopRemoval),
      backendRemoval_(backendRemoval),
      backendRetained_(backendRetained),
      blocked_(blocked) {}

UninstallPlan UninstallPlan::make(const UninstallContext& context) {
    // 桌面端是否卸载单独建模：只由 desktopInstalled 决定，与后台决策无关。
    const bool desktopRemoval = context.desktopInstalled;

    // 规则 1：复选框未勾选（默认）-> 仅卸载桌面端，后台保留。
    if (!context.removeBackendService) {
        if (isOwnedAndMatched(context)) {
            // 恰好存在"拥有且匹配"的后台 service：因未勾选而保留，显式报告原因。
            return UninstallPlan(context, UninstallAction::RetainBackendBecauseUnchecked,
                                 desktopRemoval,
                                 /*backendRemoval=*/false,
                                 /*backendRetained=*/true,
                                 /*blocked=*/false);
        }
        // 无任何可移除的拥有后台：纯默认卸载，仅卸载桌面端。
        return UninstallPlan(context, UninstallAction::RemoveDesktopOnly,
                             desktopRemoval,
                             /*backendRemoval=*/false,
                             /*backendRetained=*/true,
                             /*blocked=*/false);
    }

    // 复选框已勾选 -> 进入后台移除授权评估。

    // 规则 2：必须完成二次确认；未确认时后台移除被阻塞（桌面端仍卸载）。
    if (!context.secondaryConfirmed) {
        return UninstallPlan(context, UninstallAction::BlockedMissingConfirmation,
                             desktopRemoval,
                             /*backendRemoval=*/false,
                             /*backendRetained=*/true,
                             /*blocked=*/true);
    }

    // 规则 3：勾选 + 二次确认 + 拥有且匹配 + 目标已知 + 服务可移除 -> 允许移除。
    if (canRemoveBackend(context)) {
        return UninstallPlan(context, UninstallAction::RemoveDesktopAndOwnedBackend,
                             desktopRemoval,
                             /*backendRemoval=*/true,
                             /*backendRetained=*/false,
                             /*blocked=*/false);
    }

    // 规则 4：勾选 + 确认，但后台为 ExistingOfficial / Foreign / Unmanaged /
    // 归属不一致(Mismatch) / 目标未知 / 不可移除 -> 绝不移除，保留并报告原因。
    return UninstallPlan(context, UninstallAction::RetainBackendBecauseUnownedOrForeign,
                         desktopRemoval,
                         /*backendRemoval=*/false,
                         /*backendRetained=*/true,
                         /*blocked=*/false);
}

UninstallPlan makeUninstallPlan(const UninstallContext& context) {
    return UninstallPlan::make(context);
}

QString uninstallActionToString(UninstallAction action) {
    switch (action) {
        case UninstallAction::RemoveDesktopOnly:
            return QStringLiteral("RemoveDesktopOnly");
        case UninstallAction::RemoveDesktopAndOwnedBackend:
            return QStringLiteral("RemoveDesktopAndOwnedBackend");
        case UninstallAction::RetainBackendBecauseUnchecked:
            return QStringLiteral("RetainBackendBecauseUnchecked");
        case UninstallAction::RetainBackendBecauseUnownedOrForeign:
            return QStringLiteral("RetainBackendBecauseUnownedOrForeign");
        case UninstallAction::BlockedMissingConfirmation:
            return QStringLiteral("BlockedMissingConfirmation");
    }
    return QStringLiteral("Unknown");
}

QString consistencyResultToString(ConsistencyResult result) {
    switch (result) {
        case ConsistencyResult::NotRecorded:
            return QStringLiteral("NotRecorded");
        case ConsistencyResult::Match:
            return QStringLiteral("Match");
        case ConsistencyResult::Mismatch:
            return QStringLiteral("Mismatch");
    }
    return QStringLiteral("Unknown");
}

}  // namespace dsh::service
