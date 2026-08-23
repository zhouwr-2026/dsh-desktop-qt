// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::InstallationPlan 的单元测试。
//
// 该模块是"纯安装决策模型"：输入一个只读检测 + 用户授权的环境快照
// （InstallationContext），确定性地产出有序的安装动作序列（InstallationStage）。
// 测试只构造纯数据、不触发任何网络/进程/UI/systemd，因此可稳定运行。
//
// 覆盖 docs/DSH-DESKTOP-SERVICE-PLAN.zh.md 的第 2 节与第 10 节场景：
// 已有有效服务复用、inactive/failed 需授权、补齐 service、无 CLI 先装、
// foreign/unmanaged 不自启动、无 systemd 用兜底。

#include <QTest>
#include <QVector>

#include "../src/service/InstallationPlan.h"

using dsh::service::CliState;
using dsh::service::InstallationContext;
using dsh::service::InstallationPlan;
using dsh::service::InstallationStage;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::ServiceState;
using dsh::service::cliStateToString;
using dsh::service::installationStageToString;
using dsh::service::makeInstallationPlan;
using dsh::service::serviceStateToString;

namespace {

InstallationContext makeCtx(CliState cli,
                            ServiceState service,
                            bool serviceOfficial,
                            bool systemdAvailable,
                            bool consentToStartExisting = false,
                            ServiceScope scope = ServiceScope::User,
                            ServiceOrigin origin = ServiceOrigin::ExistingOfficial,
                            bool shareWithOtherUsers = false) {
    InstallationContext ctx;
    ctx.cli = cli;
    ctx.service = service;
    ctx.serviceOfficial = serviceOfficial;
    ctx.systemdAvailable = systemdAvailable;
    ctx.consentToStartExisting = consentToStartExisting;
    ctx.scope = scope;
    ctx.origin = origin;
    ctx.shareWithOtherUsers = shareWithOtherUsers;
    return ctx;
}

void verifyStages(const InstallationPlan& plan,
                  std::initializer_list<InstallationStage> expected) {
    const QVector<InstallationStage>& stages = plan.stages();
    QCOMPARE(stages.size(), static_cast<int>(expected.size()));
    int i = 0;
    for (const InstallationStage s : expected) {
        QCOMPARE(stages.at(i), s);
        ++i;
    }
}

}  // namespace

class TestInstallationPlan : public QObject {
    Q_OBJECT
private slots:
    // --- 无 CLI：安装官方包后补齐 service ---
    void missingCliNoServiceProvisions();
    void missingCliNoServiceProvisionsShareSystem();
    void missingCliWithSystemdMissingServiceOrder();
    // --- 已安装 CLI 而无 service：仅补齐 ---
    void installedCliNoServiceProvisions();
    // --- 已有有效服务：绝不覆盖 ---
    void installedCliActiveServiceReuse();
    void missingCliActiveServiceReuse();
    // --- inactive/failed：必须授权才启动 ---
    void installedCliInactiveNoConsent();
    void installedCliInactiveWithConsent();
    void installedCliFailedWithConsent();
    void installedCliFailedNoConsent();
    // --- foreign / unmanaged / 非官方：桌面端不自启动 ---
    void activeServiceNotOfficialBlocked();
    void foreignServiceBlocked();
    void unmanagedServiceBlocked();
    // --- 无 systemd：受管子进程兜底 ---
    void noSystemdMissingCliSupervised();
    void noSystemdInstalledCliSupervised();
    // --- Unavailable（总线不可用）：同样走兜底 ---
    void unavailableServiceInstalledCliSupervised();
    void unavailableServiceMissingCliSupervised();
    // --- 派生结果 ---
    void resultingOriginForProvision();
    void resultingOriginForSupervised();
    void resultingOriginForReuse();
    void provisionScopeDefaultsToUser();
    // --- 字符串化（稳定可读） ---
    void stringConversions();
};

void TestInstallationPlan::missingCliNoServiceProvisions() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Missing, false, /*systemd=*/true));
    verifyStages(plan, {InstallationStage::InstallOfficialDsh,
                        InstallationStage::ProvisionService});
    QVERIFY(!plan.blocked());
    QVERIFY(!plan.needsConsent());
    QCOMPARE(plan.primaryStage(), InstallationStage::InstallOfficialDsh);
    QCOMPARE(plan.provisionScope(), ServiceScope::User);
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ProvisionedByDesktop);
    QVERIFY(!plan.isEmpty());
}

void TestInstallationPlan::missingCliNoServiceProvisionsShareSystem() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Missing, false, /*systemd=*/true,
        /*consent=*/false, ServiceScope::User, ServiceOrigin::ExistingOfficial,
        /*shareWithOtherUsers=*/true));
    verifyStages(plan, {InstallationStage::InstallOfficialDsh,
                        InstallationStage::ProvisionService});
    // 用户明确选择共享：创建系统级 unit。
    QCOMPARE(plan.provisionScope(), ServiceScope::System);
}

void TestInstallationPlan::missingCliWithSystemdMissingServiceOrder() {
    // 与上一致但再确认顺序固定：先安装 CLI，再补齐 service。
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Missing, false, /*systemd=*/true));
    QCOMPARE(plan.stages().at(0), InstallationStage::InstallOfficialDsh);
    QCOMPARE(plan.stages().at(1), InstallationStage::ProvisionService);
}

void TestInstallationPlan::installedCliNoServiceProvisions() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Missing, false, /*systemd=*/true));
    verifyStages(plan, {InstallationStage::ProvisionService});
    QVERIFY(!plan.blocked());
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ProvisionedByDesktop);
    QCOMPARE(plan.primaryStage(), InstallationStage::ProvisionService);
}

void TestInstallationPlan::installedCliActiveServiceReuse() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Active, /*official=*/true,
        /*systemd=*/true));
    verifyStages(plan, {InstallationStage::ReuseExistingService});
    QVERIFY(!plan.blocked());
    QVERIFY(!plan.needsConsent());
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ExistingOfficial);
}

void TestInstallationPlan::missingCliActiveServiceReuse() {
    // 即使 CLI 检测为缺失，已有有效官方服务也绝不覆盖、不重复安装 → 复用；
    // 服务状态优先于 CLI 检测（两者在物理上不会同时存在，但模型保持确定性）。
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Active, /*official=*/true,
        /*systemd=*/true));
    verifyStages(plan, {InstallationStage::ReuseExistingService});
    QCOMPARE(plan.primaryStage(), InstallationStage::ReuseExistingService);
}

void TestInstallationPlan::installedCliInactiveNoConsent() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Inactive, /*official=*/true,
        /*systemd=*/true, /*consent=*/false));
    // 未授权：保持停止，仅读，不做任何启动动作。
    verifyStages(plan, {InstallationStage::DoNothing});
    QVERIFY(!plan.blocked());
    QVERIFY(plan.needsConsent());
    QCOMPARE(plan.primaryStage(), InstallationStage::DoNothing);
}

void TestInstallationPlan::installedCliInactiveWithConsent() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Inactive, /*official=*/true,
        /*systemd=*/true, /*consent=*/true));
    verifyStages(plan, {InstallationStage::StartExistingServiceWithConsent});
    QVERIFY(plan.needsConsent());
    QVERIFY(!plan.blocked());
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ExistingOfficial);
}

void TestInstallationPlan::installedCliFailedWithConsent() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Failed, /*official=*/true,
        /*systemd=*/true, /*consent=*/true));
    verifyStages(plan, {InstallationStage::StartExistingServiceWithConsent});
    QVERIFY(plan.needsConsent());
    QVERIFY(!plan.blocked());
}

void TestInstallationPlan::installedCliFailedNoConsent() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Failed, /*official=*/true,
        /*systemd=*/true, /*consent=*/false));
    verifyStages(plan, {InstallationStage::DoNothing});
    QVERIFY(plan.needsConsent());
    QVERIFY(!plan.blocked());
}

void TestInstallationPlan::activeServiceNotOfficialBlocked() {
    // 存在 unit 且 Active，但未通过官方/归属验证 → 视为非官方，禁止启动/覆盖。
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Active, /*official=*/false,
        /*systemd=*/true));
    verifyStages(plan, {InstallationStage::DoNothing});
    QVERIFY(plan.blocked());
    QVERIFY(!plan.needsConsent());
}

void TestInstallationPlan::foreignServiceBlocked() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Foreign, /*official=*/false,
        /*systemd=*/true));
    verifyStages(plan, {InstallationStage::DoNothing});
    QVERIFY(plan.blocked());
    QVERIFY(!plan.needsConsent());
}

void TestInstallationPlan::unmanagedServiceBlocked() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Unmanaged, /*official=*/false,
        /*systemd=*/true));
    verifyStages(plan, {InstallationStage::DoNothing});
    QVERIFY(plan.blocked());
    QVERIFY(!plan.needsConsent());
}

void TestInstallationPlan::noSystemdMissingCliSupervised() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Missing, false, /*systemd=*/false));
    verifyStages(plan, {InstallationStage::InstallOfficialDsh,
                        InstallationStage::UseSupervisedFallback});
    QVERIFY(!plan.blocked());
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::SupervisedFallback);
}

void TestInstallationPlan::noSystemdInstalledCliSupervised() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Missing, false, /*systemd=*/false));
    verifyStages(plan, {InstallationStage::UseSupervisedFallback});
    QVERIFY(!plan.blocked());
}

void TestInstallationPlan::unavailableServiceInstalledCliSupervised() {
    // systemd 可用但总线不可用导致 service=Unavailable：同样走兜底。
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Unavailable, false, /*systemd=*/true));
    verifyStages(plan, {InstallationStage::UseSupervisedFallback});
    QVERIFY(!plan.blocked());
}

void TestInstallationPlan::unavailableServiceMissingCliSupervised() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Missing, ServiceState::Unavailable, false, /*systemd=*/true));
    verifyStages(plan, {InstallationStage::InstallOfficialDsh,
                        InstallationStage::UseSupervisedFallback});
    QVERIFY(!plan.blocked());
}

void TestInstallationPlan::resultingOriginForProvision() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Missing, false, /*systemd=*/true));
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ProvisionedByDesktop);
}

void TestInstallationPlan::resultingOriginForSupervised() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Missing, false, /*systemd=*/false));
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::SupervisedFallback);
}

void TestInstallationPlan::resultingOriginForReuse() {
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Active, /*official=*/true,
        /*systemd=*/true));
    QCOMPARE(plan.resultingOrigin(), ServiceOrigin::ExistingOfficial);
}

void TestInstallationPlan::provisionScopeDefaultsToUser() {
    // 未选择"共享"时默认创建当前用户级 unit。
    const InstallationPlan plan = makeInstallationPlan(makeCtx(
        CliState::Installed, ServiceState::Missing, false, /*systemd=*/true));
    QCOMPARE(plan.provisionScope(), ServiceScope::User);
}

void TestInstallationPlan::stringConversions() {
    // 稳定、可读、可用于日志/展示。
    QCOMPARE(cliStateToString(CliState::Missing), QStringLiteral("Missing"));
    QCOMPARE(cliStateToString(CliState::Installed), QStringLiteral("Installed"));
    QCOMPARE(serviceStateToString(ServiceState::Inactive), QStringLiteral("Inactive"));
    QCOMPARE(serviceStateToString(ServiceState::Unmanaged), QStringLiteral("Unmanaged"));
    QCOMPARE(serviceStateToString(ServiceState::Unavailable), QStringLiteral("Unavailable"));
    QCOMPARE(installationStageToString(InstallationStage::StartExistingServiceWithConsent),
             QStringLiteral("StartExistingServiceWithConsent"));
    QCOMPARE(installationStageToString(InstallationStage::UseSupervisedFallback),
             QStringLiteral("UseSupervisedFallback"));
    QCOMPARE(installationStageToString(InstallationStage::DoNothing),
             QStringLiteral("DoNothing"));
}

QTEST_GUILESS_MAIN(TestInstallationPlan)
#include "test_installation_plan.moc"
