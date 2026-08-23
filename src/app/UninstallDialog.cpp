// SPDX-License-Identifier: MIT
// @author zhouwr
#include "UninstallDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsh::app {

namespace {

/// 复选框展示文本（约定为精确文案，测试引用）。
constexpr auto kRemoveBackendLabel = "同时卸载 DSH 后台服务";

QString serviceOriginDisplay(dsh::service::ServiceOrigin origin) {
    switch (origin) {
        case dsh::service::ServiceOrigin::ExistingOfficial:
            return QStringLiteral("现有官方服务");
        case dsh::service::ServiceOrigin::ProvisionedByDesktop:
            return QStringLiteral("由 DSH Desktop 创建");
        case dsh::service::ServiceOrigin::SupervisedFallback:
            return QStringLiteral("受管子进程兜底");
        case dsh::service::ServiceOrigin::External:
            return QStringLiteral("远程/外部");
    }
    return QStringLiteral("未知");
}

}  // namespace

UninstallDialog::UninstallDialog(const dsh::service::UninstallContext& context,
                                 QWidget* parent)
    : QDialog(parent), context_(context) {
    setWindowTitle(tr("卸载 DSH Desktop"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("确定要卸载 DSH Desktop 吗？\n\n"
           "• 桌面端二进制、桌面条目、图标与自启动将被移除\n"
           "• 用户配置、日志、WebEngine 数据与下载文件默认保留"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // 只读展示后台服务检测结果（来源 + 范围）。
    if (context_.backendDetected) {
        const QString summary = detectionSummaryText(context_);
        auto* backend = new QLabel(
            tr("<b>检测到的后台服务：</b>%1").arg(summary));
        backend->setWordWrap(true);
        layout->addWidget(backend);

        // 非「由桌面端创建且指纹一致」的后台：明确说明将被保留。
        if (!backendOwnedByDesktop(context_)) {
            const QString refusal = ownershipRefusalText(context_);
            if (!refusal.isEmpty()) {
                auto* note = new QLabel(QStringLiteral("⚠  ") + refusal);
                note->setWordWrap(true);
                note->setStyleSheet(
                    QStringLiteral("color: #b66a00; font-weight: 600;"));
                layout->addWidget(note);
            }
        }
    } else {
        auto* note = new QLabel(
            tr("未检测到由 DSH Desktop 创建的后台服务。"));
        note->setWordWrap(true);
        note->setStyleSheet(
            QStringLiteral("color: #888; font-style: italic;"));
        layout->addWidget(note);
    }

    // 复选框：默认不勾选，仅当后台「由桌面端创建且指纹一致」时可移除。
    checkbox_ = new QCheckBox(tr(kRemoveBackendLabel));
    checkbox_->setChecked(false);
    checkbox_->setEnabled(backendOwnedByDesktop(context_));
    layout->addWidget(checkbox_);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("卸载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &UninstallDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool UninstallDialog::removeBackendSelected() const {
    return checkbox_ && checkbox_->isChecked();
}

bool UninstallDialog::secondaryConfirmed() const {
    return secondaryConfirmed_;
}

bool UninstallDialog::backendOwnedByDesktop(
    const dsh::service::UninstallContext& context) {
    return context.backendDetected
        && context.origin == dsh::service::ServiceOrigin::ProvisionedByDesktop
        && context.ownershipConsistency == dsh::service::ConsistencyResult::Match;
}

bool UninstallDialog::requiresSecondaryConfirmation(
    const dsh::service::UninstallContext& context) {
    // 只有确实检测到后台、且复选框勾选了一项可执行的移除决策时才需要确认；
    // 未检测到后台时勾选无意义，且复选框已被禁用，不会走到确认。
    return context.backendDetected;
}

QString UninstallDialog::secondaryConfirmationText(
    const dsh::service::UninstallContext& context) {
    const QString scope =
        dsh::service::scopeToString(context.scope);
    if (context.origin == dsh::service::ServiceOrigin::ProvisionedByDesktop) {
        return tr(
            "将由 DSH Desktop 创建的后台服务一并卸载：\n\n"
            "• 范围：%1\n"
            "• 服务停止后将不可再被桌面端拉起重启\n\n"
            "确定要继续吗？")
            .arg(scope);
    }
    return tr(
        "这是系统中原有的 DSH 后台服务（来源：%1，范围：%2）。\n"
        "停止并移除可能影响其它终端、脚本或远程访问。\n\n确定要继续吗？")
        .arg(serviceOriginDisplay(context.origin), scope);
}

QString UninstallDialog::ownershipRefusalText(
    const dsh::service::UninstallContext& context) {
    if (!context.backendDetected) return {};
    if (backendOwnedByDesktop(context)) return {};  // 可移除，无需拒绝说明。
    return tr(
        "该后台服务并非由 DSH Desktop 创建或已被改动（来源：%1，范围：%2，"
        "归属一致性：%3）。卸载时将被保留；如需移除请手动处理。")
        .arg(serviceOriginDisplay(context.origin),
             dsh::service::scopeToString(context.scope),
             dsh::service::consistencyResultToString(context.ownershipConsistency));
}

QString UninstallDialog::detectionSummaryText(
    const dsh::service::UninstallContext& context) {
    const QString origin = serviceOriginDisplay(context.origin);
    const QString scope = dsh::service::scopeToString(context.scope);
    if (backendOwnedByDesktop(context)) {
        return tr("%1（范围：%2，归属一致）。"
                  "勾选下方选项可同时停止并移除它。")
            .arg(origin, scope);
    }
    return tr("%1（范围：%2）").arg(origin, scope);
}

dsh::service::UninstallContext UninstallDialog::mergeDecision(
    const dsh::service::UninstallContext& context,
    bool removeBackendService,
    bool secondaryConfirmed) {
    dsh::service::UninstallContext merged = context;
    merged.removeBackendService = removeBackendService;
    // 只有勾选 + 完成二次确认才视为"已授权移除后台"。
    merged.secondaryConfirmed = removeBackendService && secondaryConfirmed;
    return merged;
}

dsh::service::UninstallContext UninstallDialog::mergedContext() const {
    return mergeDecision(context_, removeBackendSelected(), secondaryConfirmed_);
}

void UninstallDialog::onAccepted() {
    secondaryConfirmed_ = false;
    if (checkbox_->isChecked() && requiresSecondaryConfirmation(context_)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("确认同时卸载后台服务"));
        box.setText(secondaryConfirmationText(context_));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        box.setEscapeButton(QMessageBox::No);
        if (box.exec() == QMessageBox::Yes) {
            secondaryConfirmed_ = true;
        } else {
            // 用户拒绝二次确认：视为未授权移除后台，取消整个卸载。
            reject();
            return;
        }
    }
    accept();
}

}  // namespace dsh::app
