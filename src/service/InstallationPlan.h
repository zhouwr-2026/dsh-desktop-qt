// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 安装决策模型（纯函数，无 UI、无网络、无进程、无磁盘）。
//
// 本模块把"检测到的安装环境"与"用户授权"这两组输入，确定性归结为一个
// 有序的安装动作序列（InstallationStage）。它只做决策，不执行任何会改变
// 系统状态的操作；真正的安装、服务创建、服务启动由上层安装器/服务管理器
// 完成后阶段（本任务要求不改动安装器/UI，仅落地模型与测试）。
//
// 进入本模型前，上层已经完成了只读的检测（CLI 是否存在、service unit 的
// 加载/运行状态、是否通过官方入口与归属验证、systemd 是否可用、scope 与
// origin），把结果折叠成 InstallationContext。本模块据此回答"应该做什么"：
//
//   * ``makeInstallationPlan`` 对给定上下文返回唯一、确定性的 InstallationPlan；
//   * 计划给出有序的 InstallationStage 序列、是否"只读/不可管理"（blocked）、
//     是否依赖"启动已有服务"授权（needsConsent）、以及最终会落在哪个 origin。
//
// 决策规则取自 docs/DSH-DESKTOP-SERVICE-PLAN.zh.md：
//   1. 已有"有效官方服务"绝不覆盖，直接复用。
//   2. inactive/failed 的已有官方服务，必须先获得"启动授权"才启动；
//      未授权时保持停止（DoNothing，读只给出服务管理入口）。
//   3. 已安装 CLI 而无 service 时，补齐（创建）标准 dsh-web.service。
//   4. 缺少 CLI 时，先安装官方 @deepseek-ai/dsh，再补齐 service。
//   5. foreign/unmanaged（非官方入口、或属于其他用户）的服务，桌面端
//      绝不自启动，也不覆盖，按只读/不可管理处理。
//   6. systemd 或用户总线不可用（Unavailable）时改用 SupervisedFallback 兜底，
//      且仅在确实需要时先安装 CLI。
//
// 所有枚举均为强类型，输入输出为纯数据，因而可以由 Qt Test 稳定、确定性地
// 覆盖全部场景。

#pragma once

#include "ServiceInfo.h"

#include <QString>
#include <QVector>

namespace dsh::service {

/// 检测到的官方 DSH CLI（``dsh``）是否存在。
enum class CliState {
    Missing,   // 未检测到官方 dsh CLI
    Installed, // 已检测到官方 dsh CLI
};

/// 检测到的 dsh-web service unit 状态（只读检测折叠后的结果）。
///
/// Active/Inactive/Failed 只对"已通过 LoadState + 官方入口验证"的 service
/// 有意义；非官方入口、属于其他用户、或无法探测时分别落入下面对应状态。
enum class ServiceState {
    Missing,     // 两个 scope 都未发现任何 dsh-web.service。
    Active,      // 有效官方服务，当前运行中。
    Inactive,    // 有效官方服务，存在但未运行。
    Failed,      // 有效官方服务，存在但已失败。
    Foreign,     // 存在 unit，但 ExecStart 不是官方 dsh web（非官方入口）。
    Unmanaged,   // 服务存在，但不属于当前用户（其他会话/用户），桌面端不可管理。
    Unavailable, // systemd 或用户总线不可用，无法判定 service 状态。
};

/// 安装动作阶段：按顺序执行的确定性动作序列中的一项。
enum class InstallationStage {
    InstallOfficialDsh,             // 从官方 npm 源安装 @deepseek-ai/dsh。
    ReuseExistingService,           // 复用已有有效官方服务，绝不覆盖。
    StartExistingServiceWithConsent,// 用户已授权：启动已有的 inactive/failed 官方服务。
    ProvisionService,               // 创建由 DSH Desktop 拥有的标准 dsh-web.service。
    UseSupervisedFallback,          // 无可用 systemd 时，用受管子进程兜底。
    DoNothing,                      // 不采取动作（已满足，或只读/不可管理/被阻塞）。
};

/// 进入决策模型的安装环境快照：只读检测结果 + 用户授权。
struct InstallationContext {
    CliState cli{CliState::Missing};

    ServiceState service{ServiceState::Missing};

    /// 已发现的 service 是否"官方且已验证"：LoadState=loaded 且 ExecStart
    /// 调用官方 dsh web（见 ServiceDiscovery::validateCandidate）。对不存在
    /// 的服务（Missing）无意义，此时保持默认 false 即可。
    bool serviceOfficial{false};

    /// systemd（以及当前用户/系统总线）是否可用。为 false 时一律走
    /// SupervisedFallback 兜底；此时 service 通常是 Unavailable 或 Missing。
    bool systemdAvailable{false};

    /// 已发现 service 的 scope（存在时才有意义）。
    ServiceScope scope{ServiceScope::User};

    /// 已发现/已配置 service 的来源（ExistingOfficial / ProvisionedByDesktop /
    /// SupervisedFallback / External）。Remote(External) 的本机生命周期管理
    /// 不在本安装决策范围内。
    ServiceOrigin origin{ServiceOrigin::ExistingOfficial};

    /// 用户是否明确授权"启动已有的 inactive/failed 官方服务"。
    /// 未授权时该服务保持停止（规则 2）。
    bool consentToStartExisting{false};

    /// 用户是否选择"共享给本机其他用户"（决定了补齐时创建系统级 unit 还是
    /// 用户级 unit；仅对 ProvisionService 有意义，默认创建用户级 unit）。
    bool shareWithOtherUsers{false};
};

/// 安装决策结果：有序动作序列 + 影响后续 UI/安装器行为的标志，以及为展示
/// 保留的检测上下文。
class InstallationPlan {
public:
    /// 对给定环境快照做确定性决策（纯函数，无副作用）。
    static InstallationPlan make(const InstallationContext& context);

    /// 有序的安装动作序列。DoNothing 也可能出现（如用户已拒绝启动，
    /// 或服务只读/不可管理）。
    const QVector<InstallationStage>& stages() const { return stages_; }

    /// 是否需要执行任何动作（``stage == DoNothing`` 时不算有动作）。
    bool isEmpty() const { return stages_.isEmpty(); }

    /// 桌面端是否"只读/不可管理"（服务属于其他用户，或非官方入口），
    /// 因此不能安全地启动、停止或覆盖该服务。
    bool blocked() const { return blocked_; }

    /// 决策是否依赖"启动已有服务"授权：为真表示检测到 inactive/failed 的
    /// 已有官方服务；此时若未授权，阶段为 DoNothing（保持停止），授权后阶段
    /// 为 StartExistingServiceWithConsent。
    bool needsConsent() const { return needsConsent_; }

    /// 首个动作阶段；``isEmpty`` 时返回 DoNothing。
    InstallationStage primaryStage() const;

    /// 将按计划执行后服务将归属的来源：补齐 -> ProvisionedByDesktop，
    /// 兜底 -> SupervisedFallback，其余沿用已发现/已配置的 origin。
    ServiceOrigin resultingOrigin() const;

    /// 补齐时创建的 unit scope：默认当前用户级 unit；仅当用户选择"共享给
    /// 本机其它用户"时才创建系统级 unit。
    ServiceScope provisionScope() const;

    // 为展示保留的检测上下文。
    CliState cli() const { return context_.cli; }
    ServiceState service() const { return context_.service; }
    bool serviceOfficial() const { return context_.serviceOfficial; }
    bool systemdAvailable() const { return context_.systemdAvailable; }
    ServiceScope scope() const { return context_.scope; }
    ServiceOrigin origin() const { return context_.origin; }
    bool consentToStartExisting() const { return context_.consentToStartExisting; }
    const InstallationContext& context() const { return context_; }

private:
    InstallationPlan(const InstallationContext& context,
                     QVector<InstallationStage> stages,
                     bool blocked,
                     bool needsConsent);

    InstallationContext context_;
    QVector<InstallationStage> stages_;
    bool blocked_{false};
    bool needsConsent_{false};
};

/// 对给定环境快照做确定性安装决策（等价于 ``InstallationPlan::make``）。
InstallationPlan makeInstallationPlan(const InstallationContext& context);

/// 把枚举转为稳定、人类可读的字符串（用于日志/展示/测试）。
QString cliStateToString(CliState state);
QString serviceStateToString(ServiceState state);
QString installationStageToString(InstallationStage stage);

}  // namespace dsh::service
