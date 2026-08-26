// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::backend::Status 服务元数据默认值与 applyServiceMetadata 纯映射的单元测试。
//
// 只依赖 Qt Core + core 库，不启动进程、不读状态文件、不调用 systemctl /
// journalctl，因此可在任意环境稳定运行（无 systemd 的 CI 也通过）。

#include <QTest>
#include <QString>

#include "../src/backend/Backend.h"
#include "../src/service/ServiceInfo.h"

using dsh::backend::Mode;
using dsh::backend::Status;
using dsh::backend::applyServiceMetadata;
using dsh::backend::backendHealthObservationStable;
using dsh::backend::requiresStartConfirmation;
using dsh::backend::profileRepairHint;
using dsh::backend::systemdInvocationJournalMatch;
using dsh::service::LifecycleState;
using dsh::service::ServiceInfo;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;

namespace {

/// 手工构造一个只读服务快照，便于精确断言 applyServiceMetadata 的派生值。
ServiceInfo makeServiceInfo(ServiceScope scope, LifecycleState state,
                            const QString& user = QString(),
                            const QString& loadState = QStringLiteral("loaded"),
                            const QString& subState = QString(),
                            const QString& dshHome = QString()) {
    ServiceInfo info;
    info.unitName = QStringLiteral("dsh-web.service");
    info.scope = scope;
    info.state = state;
    info.user = user;
    info.loadState = loadState;
    info.subState = subState;
    info.dshHome = dshHome;
    info.dshHomeSet = !dshHome.isEmpty();
    return info;
}

template <typename E>
static int asInt(E value) {
    return static_cast<int>(value);
}

}  // namespace

class TestBackendStatus : public QObject {
    Q_OBJECT
private slots:
    void statusDefaults();
    void applyActiveSystemService();
    void applyFailedServiceSetsReason();
    void applyLoadStateErrorSetsReason();
    void applyUserScopeWithoutUserUsesCurrentUser();
    void applyUserScopeWithUserKeepsUser();
    void applyDshHomePassthrough();
    void applyClearsFailureReasonWhenHealthy();
    void applyDoesNotTouchLifecycleFields();
    void requiresStartConfirmationSystemdStoppedOrFailed();
    void requiresStartConfirmationOtherModesOrStates();
    void profileRepairHintRecognizesMissingModule();
    void profileRepairHintRecognizesFatalMcpStartup();
    void profileRepairHintIgnoresUnrelatedFailure();
    void backendHealthObservationDebouncesFailures();
    void systemdInvocationJournalMatchRejectsStaleOrInvalidIds();
};

void TestBackendStatus::statusDefaults() {
    Status s;
    QVERIFY(!s.running);
    QCOMPARE(asInt(s.mode), asInt(Mode::Systemd));
    QVERIFY(s.url.isEmpty());
    QVERIFY(s.detail.isEmpty());
    QCOMPARE(s.activeTasks, 0);

    // 新增的服务元数据默认值：未知/兜底语义。
    QCOMPARE(asInt(s.scope), asInt(ServiceScope::System));
    QCOMPARE(asInt(s.origin), asInt(ServiceOrigin::ExistingOfficial));
    QCOMPARE(asInt(s.state), asInt(LifecycleState::Unknown));
    QVERIFY(!s.manageable);
    QVERIFY(s.owner.isEmpty());
    QVERIFY(s.dshHome.isEmpty());
    QVERIFY(s.failureReason.isEmpty());
    QVERIFY(s.journalSummary.isEmpty());
}

void TestBackendStatus::applyActiveSystemService() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Active, QStringLiteral("zhouwr"),
        QStringLiteral("loaded"), QStringLiteral("running"));
    Status s;
    applyServiceMetadata(s, info, QStringLiteral("zhouwr"));

    QCOMPARE(asInt(s.scope), asInt(ServiceScope::System));
    QCOMPARE(asInt(s.state), asInt(LifecycleState::Active));
    QCOMPARE(s.owner, QStringLiteral("zhouwr"));
    QVERIFY(s.failureReason.isEmpty());

    // applyServiceMetadata 不负责、也不应改动这些字段：
    QVERIFY(!s.manageable);
    QCOMPARE(asInt(s.origin), asInt(ServiceOrigin::ExistingOfficial));
    QVERIFY(s.journalSummary.isEmpty());
    QVERIFY(s.dshHome.isEmpty());
}

void TestBackendStatus::applyFailedServiceSetsReason() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Failed, QStringLiteral("zhouwr"),
        QStringLiteral("loaded"), QStringLiteral("failed"));
    Status s;
    applyServiceMetadata(s, info);

    QCOMPARE(asInt(s.state), asInt(LifecycleState::Failed));
    QVERIFY(s.failureReason.contains(QStringLiteral("ActiveState=failed")));
    QVERIFY(s.failureReason.contains(QStringLiteral("SubState=failed")));
}

void TestBackendStatus::applyLoadStateErrorSetsReason() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Unknown, QString(),
        QStringLiteral("not-found"));
    Status s;
    applyServiceMetadata(s, info);

    QVERIFY(s.failureReason.contains(QStringLiteral("LoadState=not-found")));
}

void TestBackendStatus::applyUserScopeWithoutUserUsesCurrentUser() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::User, LifecycleState::Active, QString(),
        QStringLiteral("loaded"), QStringLiteral("running"));
    Status s;
    applyServiceMetadata(s, info, QStringLiteral("alice"));

    // 用户级服务且 User 字段为空：用调用方提供的当前用户名补 owner。
    QCOMPARE(s.owner, QStringLiteral("alice"));
}

void TestBackendStatus::applyUserScopeWithUserKeepsUser() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::User, LifecycleState::Active, QStringLiteral("bob"),
        QStringLiteral("loaded"), QStringLiteral("running"));
    Status s;
    applyServiceMetadata(s, info, QStringLiteral("alice"));

    // User 字段已有值（用户级单元的另一位所有者）：保留，不被当前用户覆盖。
    QCOMPARE(s.owner, QStringLiteral("bob"));
}

void TestBackendStatus::applyDshHomePassthrough() {
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Active, QString(),
        QStringLiteral("loaded"), QStringLiteral("running"),
        QStringLiteral("/custom/home/.dsh"));
    Status s;
    applyServiceMetadata(s, info);

    QCOMPARE(s.dshHome, QStringLiteral("/custom/home/.dsh"));
}

void TestBackendStatus::applyClearsFailureReasonWhenHealthy() {
    Status s;
    s.failureReason = QStringLiteral("stale failure reason");
    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Active, QString(),
        QStringLiteral("loaded"), QStringLiteral("running"));
    applyServiceMetadata(s, info);

    // 健康的快照不留下过期失败原因。
    QVERIFY(s.failureReason.isEmpty());
}

void TestBackendStatus::applyDoesNotTouchLifecycleFields() {
    // 建立带非默认值的运行/诊断字段，验证 applyServiceMetadata 只派生元数据，
    // 不触碰由各 status() 负责的字段（这是纯映射的边界）。
    Status s;
    s.running = true;
    s.mode = Mode::Supervised;
    s.url = QStringLiteral("http://127.0.0.1:3080");
    s.detail = QStringLiteral("custom detail");
    s.activeTasks = 7;
    s.origin = ServiceOrigin::SupervisedFallback;
    s.manageable = true;
    s.journalSummary = QStringLiteral("custom journal");

    const ServiceInfo info = makeServiceInfo(
        ServiceScope::System, LifecycleState::Active, QStringLiteral("zhouwr"),
        QStringLiteral("loaded"), QStringLiteral("running"));
    applyServiceMetadata(s, info);

    // 运行/诊断字段保持不变。
    QVERIFY(s.running);
    QCOMPARE(asInt(s.mode), asInt(Mode::Supervised));
    QCOMPARE(s.url, QStringLiteral("http://127.0.0.1:3080"));
    QCOMPARE(s.detail, QStringLiteral("custom detail"));
    QCOMPARE(s.activeTasks, 7);
    QCOMPARE(asInt(s.origin), asInt(ServiceOrigin::SupervisedFallback));
    QVERIFY(s.manageable);
    QCOMPARE(s.journalSummary, QStringLiteral("custom journal"));

    // 元数据字段由快照派生。
    QCOMPARE(asInt(s.scope), asInt(ServiceScope::System));
    QCOMPARE(asInt(s.state), asInt(LifecycleState::Active));
    QCOMPARE(s.owner, QStringLiteral("zhouwr"));
}

void TestBackendStatus::requiresStartConfirmationSystemdStoppedOrFailed() {
    // systemd 模式下，官方后端 inactive/failed 时应要求用户确认后再启动。
    Status s;
    s.mode = Mode::Systemd;
    s.state = LifecycleState::Inactive;
    QVERIFY(requiresStartConfirmation(s));

    s.state = LifecycleState::Failed;
    QVERIFY(requiresStartConfirmation(s));
}

void TestBackendStatus::requiresStartConfirmationOtherModesOrStates() {
    // 默认 Status 不确认。
    Status d;
    QVERIFY(!requiresStartConfirmation(d));

    // systemd 但处于 Active / Activating / Unknown / Unmanaged：不确认（保持自动启动）。
    Status s;
    s.mode = Mode::Systemd;
    s.state = LifecycleState::Active;
    QVERIFY(!requiresStartConfirmation(s));
    s.state = LifecycleState::Activating;
    QVERIFY(!requiresStartConfirmation(s));
    s.state = LifecycleState::Unknown;
    QVERIFY(!requiresStartConfirmation(s));
    s.state = LifecycleState::Unmanaged;
    QVERIFY(!requiresStartConfirmation(s));

    // Supervised 模式（含 inactive）：保留原自动启动行为，不确认。
    s.mode = Mode::Supervised;
    s.state = LifecycleState::Inactive;
    QVERIFY(!requiresStartConfirmation(s));
    s.state = LifecycleState::Failed;
    QVERIFY(!requiresStartConfirmation(s));

    // External 模式（远程）：不确认。
    s.mode = Mode::External;
    s.state = LifecycleState::Inactive;
    QVERIFY(!requiresStartConfirmation(s));
    s.state = LifecycleState::Failed;
    QVERIFY(!requiresStartConfirmation(s));
}

void TestBackendStatus::profileRepairHintRecognizesMissingModule() {
    const QString hint = profileRepairHint(QStringLiteral(
        "Error [ERR_MODULE_NOT_FOUND]: Cannot find module '/profile/plugin/lib/index.js'"));
    QVERIFY(hint.contains(QStringLiteral("dsh-profile-check")));
    QVERIFY(hint.contains(QStringLiteral("dsh plugin --profile web install")));
    QVERIFY(hint.contains(QStringLiteral("反复重启不能修复")));
}

void TestBackendStatus::profileRepairHintRecognizesFatalMcpStartup() {
    const QString hint = profileRepairHint(QStringLiteral(
        "mcp-client(wikijs): initial connection or tool synchronization failed"));
    QVERIFY(hint.contains(QStringLiteral("failOnStartupError: false")));
    QVERIFY(hint.contains(QStringLiteral("启用重连")));
}

void TestBackendStatus::profileRepairHintIgnoresUnrelatedFailure() {
    QVERIFY(profileRepairHint(QStringLiteral("address already in use")).isEmpty());
}

void TestBackendStatus::backendHealthObservationDebouncesFailures() {
    int failures = 0;
    QVERIFY(!backendHealthObservationStable(false, failures, 3));
    QCOMPARE(failures, 1);
    QVERIFY(!backendHealthObservationStable(false, failures, 3));
    QCOMPARE(failures, 2);
    QVERIFY(backendHealthObservationStable(false, failures, 3));
    QCOMPARE(failures, 3);

    QVERIFY(backendHealthObservationStable(true, failures, 3));
    QCOMPARE(failures, 0);
}

void TestBackendStatus::systemdInvocationJournalMatchRejectsStaleOrInvalidIds() {
    QCOMPARE(systemdInvocationJournalMatch(
                 QStringLiteral("9E946EAD817E438FB204F2DB8CD93D73")),
             QStringLiteral("_SYSTEMD_INVOCATION_ID=9e946ead817e438fb204f2db8cd93d73"));
    QVERIFY(systemdInvocationJournalMatch(QString()).isEmpty());
    QVERIFY(systemdInvocationJournalMatch(QStringLiteral("not-an-invocation-id")).isEmpty());
}

QTEST_GUILESS_MAIN(TestBackendStatus)
#include "test_backend_status.moc"
