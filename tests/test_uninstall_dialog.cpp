// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::app::UninstallDialog 的单元测试。
//
// 只驱动对话框的默认状态与纯决策辅助（默认不勾选、文案精确、勾选后需二次
// 确认、归属拒绝说明），不弹出任何模态对话框；控件需 offscreen 平台。
// 通过 QT_QPA_PLATFORM=offscreen 运行。

#include <QTest>

#include <QCheckBox>

#include "../src/app/UninstallDialog.h"

using dsh::app::UninstallDialog;
using dsh::service::ConsistencyResult;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::UninstallContext;

namespace {

/// 构造一个「由桌面端创建且指纹一致」的后台快照。
UninstallContext ownedCtx() {
    UninstallContext ctx;
    ctx.backendDetected = true;
    ctx.scope = ServiceScope::User;
    ctx.origin = ServiceOrigin::ProvisionedByDesktop;
    ctx.ownershipConsistency = ConsistencyResult::Match;
    return ctx;
}

UninstallContext officialCtx() {
    UninstallContext ctx;
    ctx.backendDetected = true;
    ctx.scope = ServiceScope::System;
    ctx.origin = ServiceOrigin::ExistingOfficial;
    ctx.ownershipConsistency = ConsistencyResult::NotRecorded;
    return ctx;
}

UninstallContext noBackendCtx() {
    UninstallContext ctx;
    ctx.backendDetected = false;
    return ctx;
}

}  // namespace

class TestUninstallDialog : public QObject {
    Q_OBJECT
private slots:
    // 默认：复选框文本精确、默认不勾选。
    void checkboxLabelExact();
    void checkboxUncheckedByDefault();
    // 复选框仅在「由桌面端创建且指纹一致」时可点。
    void checkboxEnabledWhenOwned();
    void checkboxDisabledForOfficial();
    void checkboxDisabledWhenNoBackend();
    // 勾选后是否需要二次确认。
    void requiresSecondaryConfirmation();
    // 决策合并（removeBackendService / secondaryConfirmed）。
    void mergeDecision_checkedAndConfirmed();
    void mergeDecision_checkedButNotConfirmed();
    void mergeDecision_unchecked();
    // 归属拒绝说明。
    void ownershipRefusalText_presentForUnowned();
    void ownershipRefusalText_emptyForOwned();
    void ownershipRefusalText_emptyForNoBackend();
    // 二次确认文案反映来源/范围。
    void secondaryConfirmationText_reflectsOriginAndScope();
    // 检测摘要。
    void detectionSummary_reflectsOwnership();
};

void TestUninstallDialog::checkboxLabelExact() {
    UninstallDialog d(ownedCtx());
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    // 文本精确为「同时卸载 DSH 后台服务」（不含渲染用的 "[ ]" 方括号）。
    QCOMPARE(cb->text(), QStringLiteral("同时卸载 DSH 后台服务"));
}

void TestUninstallDialog::checkboxUncheckedByDefault() {
    UninstallDialog d(ownedCtx());
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(!cb->isChecked());
    QVERIFY(!d.removeBackendSelected());
}

void TestUninstallDialog::checkboxEnabledWhenOwned() {
    UninstallDialog d(ownedCtx());
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb->isEnabled());
}

void TestUninstallDialog::checkboxDisabledForOfficial() {
    UninstallDialog d(officialCtx());
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(!cb->isEnabled());
}

void TestUninstallDialog::checkboxDisabledWhenNoBackend() {
    UninstallDialog d(noBackendCtx());
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(!cb->isEnabled());
}

void TestUninstallDialog::requiresSecondaryConfirmation() {
    QVERIFY(UninstallDialog::requiresSecondaryConfirmation(ownedCtx()));
    QVERIFY(UninstallDialog::requiresSecondaryConfirmation(officialCtx()));
    QVERIFY(!UninstallDialog::requiresSecondaryConfirmation(noBackendCtx()));
}

void TestUninstallDialog::mergeDecision_checkedAndConfirmed() {
    const UninstallContext merged = UninstallDialog::mergeDecision(
        ownedCtx(), /*removeBackendService=*/true, /*secondaryConfirmed=*/true);
    QVERIFY(merged.removeBackendService);
    QVERIFY(merged.secondaryConfirmed);
    // 其余字段保持原样。
    QCOMPARE(merged.origin, ServiceOrigin::ProvisionedByDesktop);
}

void TestUninstallDialog::mergeDecision_checkedButNotConfirmed() {
    const UninstallContext merged = UninstallDialog::mergeDecision(
        ownedCtx(), /*removeBackendService=*/true, /*secondaryConfirmed=*/false);
    QVERIFY(merged.removeBackendService);
    QVERIFY(!merged.secondaryConfirmed);
}

void TestUninstallDialog::mergeDecision_unchecked() {
    const UninstallContext merged = UninstallDialog::mergeDecision(
        ownedCtx(), /*removeBackendService=*/false, /*secondaryConfirmed=*/false);
    QVERIFY(!merged.removeBackendService);
    QVERIFY(!merged.secondaryConfirmed);
}

void TestUninstallDialog::ownershipRefusalText_presentForUnowned() {
    const QString text = UninstallDialog::ownershipRefusalText(officialCtx());
    QVERIFY(!text.isEmpty());
    QVERIFY(text.contains(QStringLiteral("保留")));
}

void TestUninstallDialog::ownershipRefusalText_emptyForOwned() {
    QCOMPARE(UninstallDialog::ownershipRefusalText(ownedCtx()), QString());
}

void TestUninstallDialog::ownershipRefusalText_emptyForNoBackend() {
    QCOMPARE(UninstallDialog::ownershipRefusalText(noBackendCtx()), QString());
}

void TestUninstallDialog::secondaryConfirmationText_reflectsOriginAndScope() {
    const QString ownedText =
        UninstallDialog::secondaryConfirmationText(ownedCtx());
    QVERIFY(ownedText.contains(QStringLiteral("user")));
    QVERIFY(ownedText.contains(QStringLiteral("DSH Desktop")));

    const QString officialText =
        UninstallDialog::secondaryConfirmationText(officialCtx());
    QVERIFY(officialText.contains(QStringLiteral("现有官方服务")));
    QVERIFY(officialText.contains(QStringLiteral("system")));
}

void TestUninstallDialog::detectionSummary_reflectsOwnership() {
    const QString ownedSummary =
        UninstallDialog::detectionSummaryText(ownedCtx());
    QVERIFY(ownedSummary.contains(QStringLiteral("归属一致")));
    QVERIFY(ownedSummary.contains(QStringLiteral("由 DSH Desktop 创建")));
}

QTEST_MAIN(TestUninstallDialog)
#include "test_uninstall_dialog.moc"
