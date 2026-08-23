// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::UpdatePlan 的单元测试。
//
// 该模块是"纯函数合并模型"：把后端（``dsh::updater::Status``）与桌面
// （``DesktopVersionChecker::DesktopVersionResult``）两份检查结果合并成一个
// 统一的更新计划。测试只构造纯数据、不触发任何网络/进程/UI，因此可稳定运行。

#include <QTest>
#include <QVector>

#include "../src/updater/UpdatePlan.h"

using dsh::updater::ComponentState;
using dsh::updater::ComponentUpdate;
using dsh::updater::DesktopVersionResult;
using dsh::updater::Status;
using dsh::updater::UpdateComponent;
using dsh::updater::UpdatePlan;
using dsh::updater::VersionCheckStatus;
using dsh::updater::backendComponent;
using dsh::updater::desktopComponent;

namespace {

/// 构造一个后端检查结果（纯数据，不调用 Updater::check 以免触发网络）。
Status makeBackend(const QString& current, const QString& latest, bool updateAvailable) {
    Status s;
    s.current = current;
    s.latest = latest;
    s.updateAvailable = updateAvailable;
    return s;
}

/// 构造一个桌面检查结果（纯数据）。
DesktopVersionResult makeDesktop(VersionCheckStatus status, bool updateAvailable,
                                 const QString& tag = QString()) {
    DesktopVersionResult r;
    r.status = status;
    r.updateAvailable = updateAvailable;
    r.release.tagName = tag;
    return r;
}

void verifyStates(const UpdatePlan& plan, ComponentState backendState,
                  ComponentState desktopState) {
    const ComponentUpdate* backend = plan.component(UpdateComponent::Backend);
    const ComponentUpdate* desktop = plan.component(UpdateComponent::Desktop);
    QVERIFY(backend != nullptr);
    QVERIFY(desktop != nullptr);
    QCOMPARE(backend->state, backendState);
    QCOMPARE(desktop->state, desktopState);
}

}  // namespace

class TestUpdatePlan : public QObject {
    Q_OBJECT
private slots:
    // --- 后端映射（纯函数） ---
    void backendAvailable();
    void backendCurrent();
    void backendUnavailableNotInstalled();
    void backendInvalidOffline();
    void backendInvalidNothingKnown();
    void backendInvalidSemver();
    // --- 桌面映射（纯函数） ---
    void desktopAvailable();
    void desktopCurrent();
    void desktopUnavailableNoRelease();
    void desktopInvalidOffline();
    void desktopInvalidResponse();
    // --- 合并计划 ---
    void combineComponentsOrderBackendFirst();
    void trayActionVisibleWhenBackendAvailable();
    void trayActionVisibleWhenDesktopAvailable();
    void trayActionVisibleWhenBothAvailable();
    void trayActionHiddenWhenNoneAvailable();
    void defaultSelectedBackendFirstBothAvailable();
    void defaultSelectedOnlyAvailable();
};

void TestUpdatePlan::backendAvailable() {
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true));
    QCOMPARE(u.component, UpdateComponent::Backend);
    QCOMPARE(u.state, ComponentState::Available);
    QCOMPARE(u.current, QStringLiteral("1.0.0"));
    QCOMPARE(u.target, QStringLiteral("1.1.0"));
    QVERIFY(!u.source.isEmpty());
}

void TestUpdatePlan::backendCurrent() {
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.1.0"), QStringLiteral("1.1.0"), false));
    QCOMPARE(u.state, ComponentState::Current);
}

void TestUpdatePlan::backendUnavailableNotInstalled() {
    // 当前为空（未安装），但已知目标版本 -> 无更新对象。
    const ComponentUpdate u = backendComponent(
        makeBackend(QString(), QStringLiteral("1.1.0"), false));
    QCOMPARE(u.state, ComponentState::Unavailable);
}

void TestUpdatePlan::backendInvalidOffline() {
    // 已知当前版本，但拿不到目标版本（离线）。
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.0.0"), QString(), false));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::backendInvalidNothingKnown() {
    // 完全没有可用信息。
    const ComponentUpdate u = backendComponent(makeBackend(QString(), QString(), false));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::backendInvalidSemver() {
    // 当前版本非法 SemVer。
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("garbage"), QStringLiteral("1.1.0"), false));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::desktopAvailable() {
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    QCOMPARE(u.component, UpdateComponent::Desktop);
    QCOMPARE(u.state, ComponentState::Available);
    // 桌面端当前版本 Result 不携带，当前为空串；target 为 release tag。
    QCOMPARE(u.target, QStringLiteral("v1.2.3"));
    QVERIFY(!u.source.isEmpty());
}

void TestUpdatePlan::desktopCurrent() {
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::Ok, false, QStringLiteral("v1.2.3")));
    QCOMPARE(u.state, ComponentState::Current);
}

void TestUpdatePlan::desktopUnavailableNoRelease() {
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::NoRelease, false));
    QCOMPARE(u.state, ComponentState::Unavailable);
}

void TestUpdatePlan::desktopInvalidOffline() {
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::Offline, false));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::desktopInvalidResponse() {
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::InvalidResponse, false));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::combineComponentsOrderBackendFirst() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true),
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    QCOMPARE(plan.components().size(), 2);
    QCOMPARE(plan.components().at(0).component, UpdateComponent::Backend);
    QCOMPARE(plan.components().at(1).component, UpdateComponent::Desktop);
}

void TestUpdatePlan::trayActionVisibleWhenBackendAvailable() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true),
        makeDesktop(VersionCheckStatus::Ok, false, QStringLiteral("v1.2.3")));
    QVERIFY(plan.trayActionVisible());
    QVERIFY(plan.hasAvailableUpdate());
    QCOMPARE(plan.defaultSelected().size(), 1);
    QCOMPARE(plan.defaultSelected().at(0), UpdateComponent::Backend);
}

void TestUpdatePlan::trayActionVisibleWhenDesktopAvailable() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.1.0"), QStringLiteral("1.1.0"), false),
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    QVERIFY(plan.trayActionVisible());
    QCOMPARE(plan.defaultSelected().size(), 1);
    QCOMPARE(plan.defaultSelected().at(0), UpdateComponent::Desktop);
}

void TestUpdatePlan::trayActionVisibleWhenBothAvailable() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true),
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    QVERIFY(plan.trayActionVisible());
    // 后端优先：Backend 排在 Desktop 之前。
    const QVector<UpdateComponent> avail = plan.orderedAvailable();
    QCOMPARE(avail.size(), 2);
    QCOMPARE(avail.at(0), UpdateComponent::Backend);
    QCOMPARE(avail.at(1), UpdateComponent::Desktop);
}

void TestUpdatePlan::trayActionHiddenWhenNoneAvailable() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.1.0"), QStringLiteral("1.1.0"), false),
        makeDesktop(VersionCheckStatus::Ok, false, QStringLiteral("v1.2.3")));
    QVERIFY(!plan.trayActionVisible());
    QVERIFY(plan.defaultSelected().isEmpty());
    QVERIFY(plan.orderedAvailable().isEmpty());
}

void TestUpdatePlan::defaultSelectedBackendFirstBothAvailable() {
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true),
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    const QVector<UpdateComponent> selected = plan.defaultSelected();
    QCOMPARE(selected.size(), 2);
    QCOMPARE(selected.at(0), UpdateComponent::Backend);
    QCOMPARE(selected.at(1), UpdateComponent::Desktop);
}

void TestUpdatePlan::defaultSelectedOnlyAvailable() {
    // 只有桌面可更新：只选桌面，且不会错误把后端（已最新）也选中。
    const UpdatePlan plan = UpdatePlan::combine(
        makeBackend(QStringLiteral("1.1.0"), QStringLiteral("1.1.0"), false),
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("v1.2.3")));
    const QVector<UpdateComponent> selected = plan.defaultSelected();
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.at(0), UpdateComponent::Desktop);

    verifyStates(plan, ComponentState::Current, ComponentState::Available);
}

QTEST_GUILESS_MAIN(TestUpdatePlan)
#include "test_update_plan.moc"
