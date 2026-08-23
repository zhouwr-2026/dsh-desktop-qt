// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::UninstallPlan 的单元测试。
//
// 该模块是"纯卸载决策模型"：输入一个只读检测 + 用户授权的快照
// （UninstallContext），确定性地产出最终卸载动作/决策（UninstallAction）。
// 测试只构造纯数据、不触发任何网络/进程/UI/systemd/磁盘，因此可稳定运行。
//
// 覆盖 docs/DSH-DESKTOP-SERVICE-PLAN.zh.md 第 5/9 节卸载规则：
// 默认仅卸载桌面端、勾选后需二次确认、仅移除"由桌面端拥有且指纹匹配"的
// 后台、绝不移除官方/foreign/unmanaged 或被改动的后台。

#include <QTest>

#include "../src/service/ServiceOwnership.h"
#include "../src/service/UninstallPlan.h"

using dsh::service::ConsistencyResult;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::UninstallAction;
using dsh::service::UninstallContext;
using dsh::service::UninstallPlan;
using dsh::service::consistencyResultToString;
using dsh::service::makeUninstallPlan;
using dsh::service::uninstallActionToString;

namespace {

/// 构造一个"由桌面端拥有且未被改动"的后台快照：检测到 + 来源为
/// ProvisionedByDesktop + 归属一致为 Match。
UninstallContext ownedBackendCtx() {
    UninstallContext ctx;
    ctx.backendDetected = true;
    ctx.scope = ServiceScope::User;
    ctx.origin = ServiceOrigin::ProvisionedByDesktop;
    ctx.ownershipConsistency = ConsistencyResult::Match;
    return ctx;
}

/// 便捷构造：默认值 + 覆盖若干字段。
UninstallContext makeCtx(bool removeBackendService,
                         bool secondaryConfirmed,
                         ServiceOrigin origin = ServiceOrigin::ExistingOfficial,
                         ConsistencyResult consistency = ConsistencyResult::NotRecorded,
                         bool backendDetected = true,
                         bool serviceRemovalAvailable = false,
                         bool targetScopePathKnown = false,
                         bool desktopInstalled = true,
                         ServiceScope scope = ServiceScope::User) {
    UninstallContext ctx;
    ctx.desktopInstalled = desktopInstalled;
    ctx.backendDetected = backendDetected;
    ctx.scope = scope;
    ctx.origin = origin;
    ctx.ownershipConsistency = consistency;
    ctx.removeBackendService = removeBackendService;
    ctx.secondaryConfirmed = secondaryConfirmed;
    ctx.serviceRemovalAvailable = serviceRemovalAvailable;
    ctx.targetScopePathKnown = targetScopePathKnown;
    return ctx;
}

}  // namespace

class TestUninstallPlan : public QObject {
    Q_OBJECT
private slots:
    // --- 默认：复选框未勾选 -> 仅卸载桌面端 ---
    void removeDesktopOnlyIsDefault();
    void retainBackendBecauseUnchecked_ownedBackend();
    // --- 勾选 + 二次确认 + 拥有且匹配 + 目标已知 + 可移除 -> 移除后台 ---
    void removeDesktopAndOwnedBackend();
    void removeDesktopAndOwnedBackend_systemScope();
    // --- 勾选但未二次确认 -> 阻塞 ---
    void blockedMissingConfirmation();
    // --- 勾选 + 确认，但后台绝不移除（非桌面端拥有的各个来源） ---
    void retainUnowned_existingOfficial();
    void retainUnowned_supervisedFallback();
    void retainUnowned_externalRemote();
    void retainUnowned_ownedButMismatch();
    void retainUnowned_ownedButNotRecorded();
    void retainUnowned_backendNotDetected();
    void retainUnowned_removalNotAvailable();
    void retainUnowned_targetPathUnknown();
    // --- 桌面端移除单独建模 ---
    void desktopRemovalReflectsInstallState();
    // --- 访问器：后台移除/保留/阻塞 ---
    void accessorsAcrossCases();
    // --- 字符串化（稳定可读） ---
    void stringConversions();
};

void TestUninstallPlan::removeDesktopOnlyIsDefault() {
    // 默认快照（复选框 false，无拥有后台）-> 仅卸载桌面端。
    UninstallContext ctx;
    ctx.origin = ServiceOrigin::ExistingOfficial;
    ctx.ownershipConsistency = ConsistencyResult::NotRecorded;
    ctx.backendDetected = true;

    const UninstallPlan plan = makeUninstallPlan(ctx);
    QCOMPARE(plan.action(), UninstallAction::RemoveDesktopOnly);
    QVERIFY(plan.desktopRemoval());
    QVERIFY(!plan.backendRemoval());
    QVERIFY(plan.backendRetained());
    QVERIFY(!plan.blocked());
    QVERIFY(!plan.removeBackendService());
}

void TestUninstallPlan::retainBackendBecauseUnchecked_ownedBackend() {
    // 存在"由桌面端拥有且未被改动"的后台，但复选框未勾选 -> 因未勾选而保留。
    UninstallContext ctx = ownedBackendCtx();
    ctx.removeBackendService = false;

    const UninstallPlan plan = makeUninstallPlan(ctx);
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnchecked);
    QVERIFY(plan.desktopRemoval());
    QVERIFY(!plan.backendRemoval());
    QVERIFY(plan.backendRetained());
    QVERIFY(!plan.blocked());
}

void TestUninstallPlan::removeDesktopAndOwnedBackend() {
    UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::RemoveDesktopAndOwnedBackend);
    QVERIFY(plan.desktopRemoval());
    QVERIFY(plan.backendRemoval());
    QVERIFY(!plan.backendRetained());
    QVERIFY(!plan.blocked());
}

void TestUninstallPlan::removeDesktopAndOwnedBackend_systemScope() {
    // 系统级拥有后台同样可被移除（scope 不影响决策，路径已知即可）。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true,
        /*desktopInstalled=*/true, ServiceScope::System));
    QCOMPARE(plan.action(), UninstallAction::RemoveDesktopAndOwnedBackend);
    QCOMPARE(plan.scope(), ServiceScope::System);
    QVERIFY(plan.backendRemoval());
}

void TestUninstallPlan::blockedMissingConfirmation() {
    // 勾选但未二次确认 -> 后台移除被阻塞，桌面端仍卸载。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/false,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::BlockedMissingConfirmation);
    QVERIFY(plan.desktopRemoval());
    QVERIFY(!plan.backendRemoval());
    QVERIFY(plan.backendRetained());
    QVERIFY(plan.blocked());
}

void TestUninstallPlan::retainUnowned_existingOfficial() {
    // 官网原有 service：勾选 + 确认也绝不移除。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ExistingOfficial, ConsistencyResult::Match));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(plan.desktopRemoval());
    QVERIFY(!plan.backendRemoval());
    QVERIFY(plan.backendRetained());
    QVERIFY(!plan.blocked());
}

void TestUninstallPlan::retainUnowned_supervisedFallback() {
    // SupervisedFallback 属于桌面端子进程兜底，不视为可移除的 systemd unit。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::SupervisedFallback, ConsistencyResult::NotRecorded));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::retainUnowned_externalRemote() {
    // External（远程后端）不管理本机生命周期，绝不删除。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::External, ConsistencyResult::NotRecorded));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::retainUnowned_ownedButMismatch() {
    // 来源是 ProvisionedByDesktop 但所有权指纹不一致（用户改过 ExecStart）：
    // 绝不删除，保留。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Mismatch,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
    QVERIFY(plan.backendRetained());
}

void TestUninstallPlan::retainUnowned_ownedButNotRecorded() {
    // 无所有权记录：不可确认归属，绝不删除。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::NotRecorded,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::retainUnowned_backendNotDetected() {
    // 未检测到后台 service：无物可移除，保留。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/false, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::retainUnowned_removalNotAvailable() {
    // 拥有且匹配，但服务文件/数据不可移除（无权限/无提权手段）-> 保留。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/false,
        /*targetScopePathKnown=*/true));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::retainUnowned_targetPathUnknown() {
    // 拥有且匹配、可移除，但目标 scope/path 未知 -> 保留。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/true, /*secondaryConfirmed=*/true,
        ServiceOrigin::ProvisionedByDesktop, ConsistencyResult::Match,
        /*backendDetected=*/true, /*serviceRemovalAvailable=*/true,
        /*targetScopePathKnown=*/false));
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());
}

void TestUninstallPlan::desktopRemovalReflectsInstallState() {
    // 桌面端卸载单独建模：桌面端未安装时 desktopRemoval 为 false，与后台无关。
    const UninstallPlan plan = makeUninstallPlan(makeCtx(
        /*removeBackendService=*/false, /*secondaryConfirmed=*/false,
        ServiceOrigin::ExistingOfficial, ConsistencyResult::NotRecorded,
        /*backendDetected=*/false,
        /*serviceRemovalAvailable=*/false, /*targetScopePathKnown=*/false,
        /*desktopInstalled=*/false));
    QCOMPARE(plan.action(), UninstallAction::RemoveDesktopOnly);
    QVERIFY(!plan.desktopRemoval());
    QVERIFY(plan.backendRetained());
}

void TestUninstallPlan::accessorsAcrossCases() {
    // 移除后台：仅 RemoveDesktopAndOwnedBackend。
    {
        const UninstallPlan p = makeUninstallPlan(makeCtx(
            true, true, ServiceOrigin::ProvisionedByDesktop,
            ConsistencyResult::Match, true, true, true));
        QVERIFY(p.backendRemoval());
        QVERIFY(!p.backendRetained());
        QVERIFY(!p.blocked());
    }
    // 保留后台：复选框未勾选（拥有后台）。
    {
        const UninstallPlan p = makeUninstallPlan(ownedBackendCtx());
        QVERIFY(p.backendRetained());
        QVERIFY(!p.backendRemoval());
        QVERIFY(!p.blocked());
    }
    // 阻塞：勾选但未确认。
    {
        const UninstallPlan p = makeUninstallPlan(makeCtx(
            true, false, ServiceOrigin::ProvisionedByDesktop,
            ConsistencyResult::Match, true, true, true));
        QVERIFY(p.blocked());
        QVERIFY(p.backendRetained());
        QVERIFY(!p.backendRemoval());
    }
}

void TestUninstallPlan::stringConversions() {
    // 稳定、可读、可用于日志/展示。
    QCOMPARE(uninstallActionToString(UninstallAction::RemoveDesktopOnly),
             QStringLiteral("RemoveDesktopOnly"));
    QCOMPARE(uninstallActionToString(UninstallAction::RemoveDesktopAndOwnedBackend),
             QStringLiteral("RemoveDesktopAndOwnedBackend"));
    QCOMPARE(uninstallActionToString(UninstallAction::RetainBackendBecauseUnchecked),
             QStringLiteral("RetainBackendBecauseUnchecked"));
    QCOMPARE(uninstallActionToString(UninstallAction::RetainBackendBecauseUnownedOrForeign),
             QStringLiteral("RetainBackendBecauseUnownedOrForeign"));
    QCOMPARE(uninstallActionToString(UninstallAction::BlockedMissingConfirmation),
             QStringLiteral("BlockedMissingConfirmation"));
    QCOMPARE(consistencyResultToString(ConsistencyResult::Match),
             QStringLiteral("Match"));
    QCOMPARE(consistencyResultToString(ConsistencyResult::Mismatch),
             QStringLiteral("Mismatch"));
    QCOMPARE(consistencyResultToString(ConsistencyResult::NotRecorded),
             QStringLiteral("NotRecorded"));
}

QTEST_GUILESS_MAIN(TestUninstallPlan)
#include "test_uninstall_plan.moc"
