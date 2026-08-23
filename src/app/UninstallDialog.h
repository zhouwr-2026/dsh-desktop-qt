// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 原生卸载确认对话框：
//   * 只读展示检测到的后台服务来源（origin）与范围（scope）；
//   * 复选框「同时卸载 DSH 后台服务」，默认不勾选；
//   * 勾选并继续时弹出高风险二次确认；
//   * 当后台并非"由桌面端创建且指纹一致"时，明确说明归属拒绝（将被保留），
//     不提供可执行移除路径。
//
// 本对话框只做决策收集（复选框 + 二次确认），不执行任何卸载动作；真正的
// 桌面端/后台移除由上层卸载器在 UninstallPlan 判定之后完成。

#pragma once

#include "../service/ServiceInfo.h"
#include "../service/UninstallPlan.h"

#include <QCheckBox>
#include <QDialog>

namespace dsh::app {

class UninstallDialog : public QDialog {
    Q_OBJECT
public:
    /// \param context 只读的卸载检测快照（由上层折叠自 ServiceDiscovery +
    ///                ServiceOwnership）。对话框只读取其 origin/scope 等用于
    ///                展示与归属判定，不修改传入对象。
    explicit UninstallDialog(const dsh::service::UninstallContext& context,
                             QWidget* parent = nullptr);

    /// 用户是否勾选了「同时卸载 DSH 后台服务」（默认未勾选）。
    bool removeBackendSelected() const;

    /// 是否已完成二次确认（仅当勾选时需要；未勾选时为 false）。
    bool secondaryConfirmed() const;

    /// 合并后的决策上下文：把复选框与二次确认结果写回拷贝的 context_
    /// （removeBackendService / secondaryConfirmed），供 UninstallPlan::make 使用。
    dsh::service::UninstallContext mergedContext() const;

    // -----------------------------------------------------------------------
    // 纯函数/可测试：不依赖模态调用即可判定与展示文案。
    // -----------------------------------------------------------------------

    /// 把「勾选」与「二次确认」两个输入合并到一份上下文拷贝（纯函数）。
    static dsh::service::UninstallContext mergeDecision(
        const dsh::service::UninstallContext& context,
        bool removeBackendService,
        bool secondaryConfirmed);

    /// 后台是否「由桌面端创建且指纹一致」（可移除判据，见 UninstallPlan）。
    static bool backendOwnedByDesktop(
        const dsh::service::UninstallContext& context);

    /// 勾选后是否需要二次确认：后台检测到、确实可移除（可为桌面端拥有或官方
    /// 其它来源）时勾选即需要；未检测到后台时不需要。
    static bool requiresSecondaryConfirmation(
        const dsh::service::UninstallContext& context);

    /// 二次确认对话框正文（按来源给出不同的影响范围说明）。
    static QString secondaryConfirmationText(
        const dsh::service::UninstallContext& context);

    /// 归属拒绝说明：后台非「由桌面端创建且指纹一致」时，解释其将在卸载时被
    /// 保留并说明原因（源头/范围）。未检测到后台时返回空。
    static QString ownershipRefusalText(
        const dsh::service::UninstallContext& context);

    /// 检测到后台时的展示摘要（来源 + 范围 + 一致性）。
    static QString detectionSummaryText(
        const dsh::service::UninstallContext& context);

private:
    void onAccepted();  // 勾选时弹出二次确认，再决定 accept/reject。

    dsh::service::UninstallContext context_;
    QCheckBox* checkbox_{nullptr};
    bool secondaryConfirmed_{false};
};

}  // namespace dsh::app
