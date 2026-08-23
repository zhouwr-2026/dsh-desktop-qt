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
using dsh::updater::DesktopReleaseAsset;
using dsh::updater::DesktopReleaseInfo;
using dsh::updater::DesktopVersionResult;
using dsh::updater::Status;
using dsh::updater::UpdateComponent;
using dsh::updater::UpdatePlan;
using dsh::updater::VersionCheckStatus;
using dsh::updater::backendComponent;
using dsh::updater::componentDetail;
using dsh::updater::componentLabel;
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

/// 构造一个携带附件与发布信息的桌面检查结果。
DesktopVersionResult makeDesktopWithRelease(VersionCheckStatus status, bool updateAvailable,
                                            const QString& tag,
                                            const QVector<DesktopReleaseAsset>& assets) {
    DesktopVersionResult r = makeDesktop(status, updateAvailable, tag);
    r.release.assets = assets;
    r.release.name = tag;
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
    void backendInvalidUpdateAvailableNothingKnown();
    void backendInvalidUpdateAvailableEmptyCurrent();
    void backendInvalidUpdateAvailableEmptyTarget();
    void backendInvalidUpdateAvailableInvalidSemver();
    // --- 桌面映射（纯函数） ---
    void desktopAvailable();
    void desktopCurrent();
    void desktopUnavailableNoRelease();
    void desktopInvalidOffline();
    void desktopInvalidResponse();
    void desktopInvalidOkMissingTag();
    void desktopInvalidOkInvalidTag();
    // --- 合并计划 ---
    void combineComponentsOrderBackendFirst();
    void trayActionVisibleWhenBackendAvailable();
    void trayActionVisibleWhenDesktopAvailable();
    void trayActionVisibleWhenBothAvailable();
    void trayActionHiddenWhenNoneAvailable();
    void defaultSelectedBackendFirstBothAvailable();
    void defaultSelectedOnlyAvailable();
    // --- 桌面发布信息携带（用于后续下载） ---
    void desktopComponentCarriesRelease();
    void componentUpdateEqualityIncludesRelease();
    // --- 面向对话框的纯辅助函数 ---
    void componentLabelValues();
    void componentDetailAvailable();
    void componentDetailCurrent();
    void componentDetailUnavailable();
    void componentDetailInvalid();
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

void TestUpdatePlan::backendInvalidUpdateAvailableNothingKnown() {
    // updateAvailable 声称可更新，但当前与目标版本都为空——自相矛盾。
    const ComponentUpdate u = backendComponent(
        makeBackend(QString(), QString(), true));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::backendInvalidUpdateAvailableEmptyCurrent() {
    // updateAvailable 声称可更新，但当前版本为空（未安装）——自相矛盾。
    const ComponentUpdate u = backendComponent(
        makeBackend(QString(), QStringLiteral("1.1.0"), true));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::backendInvalidUpdateAvailableEmptyTarget() {
    // updateAvailable 声称可更新，但目标版本为空（离线）——自相矛盾。
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.0.0"), QString(), true));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::backendInvalidUpdateAvailableInvalidSemver() {
    // updateAvailable 声称可更新，但当前版本非法 SemVer——自相矛盾。
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("garbage"), QStringLiteral("1.1.0"), true));
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

void TestUpdatePlan::desktopInvalidOkMissingTag() {
    // Ok 但缺 tag：响应不完整，即使声称可更新也不可信。
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::Ok, true, QString()));
    QCOMPARE(u.state, ComponentState::Invalid);
}

void TestUpdatePlan::desktopInvalidOkInvalidTag() {
    // Ok 但 tag 非法 SemVer：不应视为可更新。
    const ComponentUpdate u = desktopComponent(
        makeDesktop(VersionCheckStatus::Ok, true, QStringLiteral("not-a-version")));
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

void TestUpdatePlan::desktopComponentCarriesRelease() {
    // 桌面检查结果携带发布信息（含附件）时，映射后的组件应原样带出，供下载使用。
    DesktopReleaseAsset asset;
    asset.name = QStringLiteral("dsh-desktop-0.2.0.AppImage");
    asset.url = QStringLiteral("https://gitee.com/eruditeLoong/asset");
    asset.size = 12345678;
    const DesktopVersionResult result = makeDesktopWithRelease(
        VersionCheckStatus::Ok, true, QStringLiteral("v0.2.0"), {asset});

    const ComponentUpdate u = desktopComponent(result);
    QCOMPARE(u.component, UpdateComponent::Desktop);
    QCOMPARE(u.state, ComponentState::Available);
    QCOMPARE(u.release.tagName, QStringLiteral("v0.2.0"));
    QCOMPARE(u.release.assets.size(), 1);
    QCOMPARE(u.release.assets[0].name, QStringLiteral("dsh-desktop-0.2.0.AppImage"));
    QCOMPARE(u.release.assets[0].size, qint64(12345678));
}

void TestUpdatePlan::componentUpdateEqualityIncludesRelease() {
    // ``ComponentUpdate`` 的相等性必须包含发布信息，避免静默丢失下载来源。
    DesktopReleaseAsset asset;
    asset.name = QStringLiteral("x.AppImage");
    asset.url = QStringLiteral("https://gitee.com/x/y");
    asset.size = 1;
    const ComponentUpdate a = desktopComponent(makeDesktopWithRelease(
        VersionCheckStatus::Ok, true, QStringLiteral("v1.0.0"), {asset}));
    const ComponentUpdate b = desktopComponent(makeDesktopWithRelease(
        VersionCheckStatus::Ok, true, QStringLiteral("v1.0.0"), {{asset.name, asset.url, 2}}));
    QVERIFY(a == a);
    QVERIFY(a != b);
}

void TestUpdatePlan::componentLabelValues() {
    QCOMPARE(componentLabel(UpdateComponent::Backend), QStringLiteral("DSH 后台服务"));
    QCOMPARE(componentLabel(UpdateComponent::Desktop), QStringLiteral("DSH Desktop"));
}

void TestUpdatePlan::componentDetailAvailable() {
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.0.0"), QStringLiteral("1.1.0"), true));
    const QString detail = componentDetail(u);
    QVERIFY(detail.contains(QStringLiteral("1.0.0")));
    QVERIFY(detail.contains(QStringLiteral("1.1.0")));
    QVERIFY(detail.contains(QStringLiteral("npm")));
}

void TestUpdatePlan::componentDetailCurrent() {
    const ComponentUpdate u = backendComponent(
        makeBackend(QStringLiteral("1.1.0"), QStringLiteral("1.1.0"), false));
    QVERIFY(componentDetail(u).contains(QStringLiteral("已是最新版本")));
}

void TestUpdatePlan::componentDetailUnavailable() {
    const ComponentUpdate u = backendComponent(
        makeBackend(QString(), QStringLiteral("1.1.0"), false));
    QVERIFY(componentDetail(u).contains(QStringLiteral("更新不可用")));
}

void TestUpdatePlan::componentDetailInvalid() {
    const ComponentUpdate u = backendComponent(makeBackend(QString(), QString(), false));
    QVERIFY(componentDetail(u).contains(QStringLiteral("无法检查更新")));
}

QTEST_GUILESS_MAIN(TestUpdatePlan)
#include "test_update_plan.moc"
