// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 卸载决策模型（纯函数，无 UI、无网络、无进程、无磁盘）。
//
// 本模块把"卸载时检测到的环境与用户授权"这两组输入，确定性归结为一个
// 卸载动作/决策（UninstallAction）。它只做决策，不执行任何会改变系统状态
// 的操作；真正的卸载、服务停止/禁用/删除、数据清理由上层卸载器（本任务
// 要求不改动 UI/安装器，仅落地模型与测试）在决策之后完成。
//
// "桌面端卸载"与"后台服务卸载"被刻意分开建模：
//   * desktopRemoval() 只回答桌面端本身是否会被卸载（由 desktopInstalled
//     决定，与后台无关）；
//   * action() / backendRemoval() / backendRetained() 回答后台服务如何处理。
// 因此即便后台被"保留"，桌面端仍然会被卸载，两者互不绑定。
//
// 进入本模型前，上层已经完成了只读的检测（桌面端是否已安装、后台 service
// 是否被探测到、其 scope/origin、归属一致性）并把结果折叠成
// UninstallContext。归属一致性复用 ServiceOwnership 的一致性检查：由上层
// 调用 ServiceOwnership::checkConsistency(unitName, scope, currentExecStart)
// 得到 ConsistencyResult 后填入 ownershipConsistency，本模型不触碰任何
// 状态文件，保持纯函数。
//
// 决策规则取自 docs/DSH-DESKTOP-SERVICE-PLAN.zh.md 第 5/9 节：
//   1. 复选框 removeBackendService 默认不勾选；未勾选时一律"仅卸载桌面端，
//      后台保留"。若此时恰好存在"由桌面端拥有且指纹匹配"的后台 service，
//      则显式报告 RetainBackendBecauseUnchecked（后台因未勾选而被保留），
//      否则为默认的 RemoveDesktopOnly。
//   2. 勾选后必须完成二次确认（secondaryConfirmed）；未确认时后台移除被
//      阻塞 -> BlockedMissingConfirmation（桌面端仍卸载）。
//   3. 只有"勾选 + 二次确认 + origin==ProvisionedByDesktop + 所有权记录与
//      当前 ExecStart 指纹一致(Match) + 目标 scope/path 已知 + 服务文件/数据
//      可移除"同时成立，才允许 RemoveDesktopAndOwnedBackend。
//   4. 绝不移除 ExistingOfficial / Foreign / Unmanaged，也绝不移除被用户或
//      其它工具改动过（Mismatch）的后台；这些情况一律
//      RetainBackendBecauseUnownedOrForeign。
//
// 所有枚举均为强类型，输入输出为纯数据，因而可以由 Qt Test 稳定、确定性
// 地覆盖全部场景。
//
// 注意：本文件引用 ServiceOwnership 的 ConsistencyResult 只是为了复用
// "归属一致性"这一既有判据；本模型本身不读写所有权状态文件。

#pragma once

#include "ServiceInfo.h"
#include "ServiceOwnership.h"

#include <QString>

namespace dsh::service {

/// 卸载决策：桌面端 + 后台服务如何处理的最终动作/决策。
///
/// 每种决策都隐含"桌面端被卸载"（见 desktopRemoval()），差异仅在后台服务
/// 的处理方式；"后台保留"的三种决策通过各自的原因区分。
enum class UninstallAction {
    RemoveDesktopOnly,               // 默认：仅卸载桌面端，后台保留（无"可移除的拥有后台"）。
    RemoveDesktopAndOwnedBackend,    // 卸载桌面端 + 移除由桌面端拥有且指纹匹配的后台服务。
    RetainBackendBecauseUnchecked,   // 卸载桌面端；后台保留（存在拥有后台，但因复选框未勾选）。
    RetainBackendBecauseUnownedOrForeign, // 卸载桌面端；后台保留（非桌面端拥有/foreign/非官方/指纹不匹配/不可移除）。
    BlockedMissingConfirmation,      // 卸载桌面端；后台移除被阻塞（缺少二次确认）。
};

/// 卸载决策输入的只读快照：检测结果 + 用户授权。纯数据。
struct UninstallContext {
    /// 桌面端是否已安装（决定 desktopRemoval()）。
    bool desktopInstalled{true};

    /// 是否检测到后台 DSH service（存在 unit）。
    bool backendDetected{false};

    /// 已发现后台 service 的 scope（存在时才有意义）。
    ServiceScope scope{ServiceScope::System};

    /// 已发现/已配置后台 service 的来源（ExistingOfficial /
    /// ProvisionedByDesktop / SupervisedFallback / External）。卸载决策只接受
    /// ProvisionedByDesktop 作为可移除来源，其余一律保留。
    ServiceOrigin origin{ServiceOrigin::ExistingOfficial};

    /// 归属一致性（复用 ServiceOwnership 的一致性检查）。由上层调用
    /// ServiceOwnership::checkConsistency(unitName, scope, currentExecStart)
    /// 得到并填入；只有 Match 才可视为"由桌面端拥有且未被改动"。
    ConsistencyResult ownershipConsistency{ConsistencyResult::NotRecorded};

    /// 复选框"同时卸载 DSH 后台服务"。默认 false（不勾选）。
    bool removeBackendService{false};

    /// 二次确认（勾选后的高风险确认）。后台移除的必要条件。
    bool secondaryConfirmed{false};

    /// 服务文件/数据移除的可用性（权限、提权手段、路径可写等）。
    bool serviceRemovalAvailable{false};

    /// 目标 scope/path 是否已知：上层是否已解析出确切的 unit scope 与
    /// unit 文件路径。后台移除的必要条件。
    bool targetScopePathKnown{false};
};

/// 卸载决策结果：最终动作 + 分开建模的桌面端/后台处理标志，以及为展示
/// 保留的检测上下文。
class UninstallPlan {
public:
    /// 对给定环境与授权快照做确定性决策（纯函数，无副作用）。
    static UninstallPlan make(const UninstallContext& context);

    /// 最终卸载动作/决策（见 UninstallAction）。
    UninstallAction action() const { return action_; }

    /// 桌面端是否被卸载（模型化"桌面卸载"；由 desktopInstalled 决定，
    /// 与后台决策无关）。
    bool desktopRemoval() const { return desktopRemoval_; }

    /// 后台服务是否被卸载（仅 RemoveDesktopAndOwnedBackend 为真）。
    bool backendRemoval() const { return backendRemoval_; }

    /// 后台服务是否被保留（决策为"保留"的三种情况为真）。
    bool backendRetained() const { return backendRetained_; }

    /// 后台移除是否被阻塞（仅 BlockedMissingConfirmation 为真；桌面端卸载
    /// 不受影响）。
    bool blocked() const { return blocked_; }

    // 为展示保留的检测上下文。
    bool desktopInstalled() const { return context_.desktopInstalled; }
    bool backendDetected() const { return context_.backendDetected; }
    ServiceScope scope() const { return context_.scope; }
    ServiceOrigin origin() const { return context_.origin; }
    ConsistencyResult ownershipConsistency() const { return context_.ownershipConsistency; }
    bool removeBackendService() const { return context_.removeBackendService; }
    bool secondaryConfirmed() const { return context_.secondaryConfirmed; }
    bool serviceRemovalAvailable() const { return context_.serviceRemovalAvailable; }
    bool targetScopePathKnown() const { return context_.targetScopePathKnown; }
    const UninstallContext& context() const { return context_; }

private:
    UninstallPlan(const UninstallContext& context,
                  UninstallAction action,
                  bool desktopRemoval,
                  bool backendRemoval,
                  bool backendRetained,
                  bool blocked);

    UninstallContext context_;
    UninstallAction action_{UninstallAction::RemoveDesktopOnly};
    bool desktopRemoval_{false};
    bool backendRemoval_{false};
    bool backendRetained_{false};
    bool blocked_{false};
};

/// 对给定快照做确定性卸载决策（等价于 ``UninstallPlan::make``）。
UninstallPlan makeUninstallPlan(const UninstallContext& context);

/// 把枚举转为稳定、人类可读的字符串（用于日志/展示/测试）。
QString uninstallActionToString(UninstallAction action);

/// 把归属一致性结果转为稳定、人类可读的字符串（用于日志/展示/测试）。
QString consistencyResultToString(ConsistencyResult result);

}  // namespace dsh::service
