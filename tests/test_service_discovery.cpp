// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service 服务发现数据类型与 ``systemctl show`` 解析的单元测试。

#include <QTest>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../src/service/SystemctlShowParser.h"
#include "../src/service/ServiceDiscovery.h"

using dsh::service::DetectedService;
using dsh::service::DiscoveryResult;
using dsh::service::DiscoveredService;
using dsh::service::LifecycleState;
using dsh::service::RejectionReason;
using dsh::service::ServiceInfo;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::invokesOfficialDshWeb;
using dsh::service::kDefaultHost;
using dsh::service::kDefaultPort;
using dsh::service::parseSystemctlShow;
using dsh::service::selectCandidateIndex;
using dsh::service::validateCandidate;

template <typename E>
static int asInt(E value) {
    return static_cast<int>(value);
}

static QString validSystemShowText() {
    return QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "MainPID=1000\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; }\n"
        "User=zhouwr\n");
}

static QString validUserShowText() {
    return QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "MainPID=2000\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --no-open ; }\n"
        "User=alice\n");
}

class TestServiceDiscovery : public QObject {
    Q_OBJECT
private slots:
    void activeOfficialSystemService();
    void inactiveUserService();
    void foreignExecStartRejection();
    void nonDefaultPortAndDshHome();
    void equalsFormFlags();
    void validateAcceptsOfficialLoaded();
    void validateRejectsLoadNotLoaded();
    void validateRejectsForeignExecStart();
    void selectPrefersUserWhenBothValid();
    void selectFallsBackToSystemWhenUserInvalid();
    void selectUserWhenSystemAbsent();
    void selectNoneWhenNoValid();
    void selectedAccessor();
    void detectedCarriesUserScopeWhenBothValid();
};

void TestServiceDiscovery::activeOfficialSystemService() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "Description=DeepSeek Harness Web UI\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "MainPID=136207\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; ignore_errors=no ; start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }\n"
        "Environment=HOME=/home/zhouwr\n"
        "EnvironmentFiles=/etc/dsh-web.env (ignore_errors=no)\n"
        "WorkingDirectory=/home/zhouwr\n"
        "User=zhouwr\n");

    const ServiceInfo info =
        parseSystemctlShow(show, QStringLiteral("dsh-web.service"), ServiceScope::System);

    QCOMPARE(info.unitName, QStringLiteral("dsh-web.service"));
    QCOMPARE(asInt(info.scope), asInt(ServiceScope::System));
    QCOMPARE(asInt(info.origin), asInt(ServiceOrigin::ExistingOfficial));
    QCOMPARE(asInt(info.state), asInt(LifecycleState::Active));
    QCOMPARE(info.loadState, QStringLiteral("loaded"));
    QCOMPARE(info.activeState, QStringLiteral("active"));
    QCOMPARE(info.subState, QStringLiteral("running"));
    QCOMPARE(info.mainPid, static_cast<qint64>(136207));
    QCOMPARE(info.user, QStringLiteral("zhouwr"));
    QCOMPARE(info.workingDirectory, QStringLiteral("/home/zhouwr"));
    QVERIFY(info.invokesOfficialDshWeb);
    QCOMPARE(info.environment,
             QStringList{QStringLiteral("HOME=/home/zhouwr")});
    QCOMPARE(info.environmentFiles,
             QStringList{QStringLiteral("/etc/dsh-web.env")});

    QCOMPARE(info.port, kDefaultPort);
    QVERIFY(info.portIsDefault);
    QCOMPARE(info.host, QLatin1String(kDefaultHost));
    QVERIFY(info.hostIsDefault);

    QVERIFY(info.dshHome.isEmpty());
    QVERIFY(!info.dshHomeSet);
}

void TestServiceDiscovery::inactiveUserService() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=inactive\n"
        "SubState=dead\n"
        "MainPID=0\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --no-open ; ignore_errors=no ; start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }\n"
        "Environment=HOME=/home/alice\n"
        "WorkingDirectory=/home/alice\n"
        "User=alice\n");

    const ServiceInfo info =
        parseSystemctlShow(show, QStringLiteral("dsh-web.service"), ServiceScope::User);

    QCOMPARE(asInt(info.scope), asInt(ServiceScope::User));
    QCOMPARE(asInt(info.state), asInt(LifecycleState::Inactive));
    QCOMPARE(info.loadState, QStringLiteral("loaded"));
    QCOMPARE(info.activeState, QStringLiteral("inactive"));
    QCOMPARE(info.subState, QStringLiteral("dead"));
    QCOMPARE(info.mainPid, static_cast<qint64>(-1));
    QCOMPARE(info.user, QStringLiteral("alice"));
    QCOMPARE(info.workingDirectory, QStringLiteral("/home/alice"));
    QVERIFY(info.invokesOfficialDshWeb);

    QCOMPARE(info.port, kDefaultPort);
    QVERIFY(info.portIsDefault);
    QCOMPARE(info.host, QLatin1String(kDefaultHost));
    QVERIFY(info.hostIsDefault);
}

void TestServiceDiscovery::foreignExecStartRejection() {
    QVERIFY(!invokesOfficialDshWeb(QString()));
    QVERIFY(!invokesOfficialDshWeb(QStringLiteral("/usr/bin/node /opt/other/server.js")));
    QVERIFY(!invokesOfficialDshWeb(QStringLiteral("/usr/bin/python3 -m http.server 8000")));
    QVERIFY(!invokesOfficialDshWeb(QStringLiteral("/usr/bin/dsh tui")));
    QVERIFY(!invokesOfficialDshWeb(QStringLiteral("/usr/bin/dsh --profile tui")));
    QVERIFY(!invokesOfficialDshWeb(
        QStringLiteral("{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh tui ; }")));

    QVERIFY(invokesOfficialDshWeb(QStringLiteral("/usr/bin/dsh web")));
    QVERIFY(invokesOfficialDshWeb(
        QStringLiteral("{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --port 9000 ; }")));
    QVERIFY(invokesOfficialDshWeb(QStringLiteral("dsh --profile web")));
}

void TestServiceDiscovery::nonDefaultPortAndDshHome() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "MainPID=5555\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --no-open --host 0.0.0.0 --port 8080 ; }\n"
        "Environment=HOME=/home/zhouwr DSH_HOME=/custom/home/.dsh\n"
        "WorkingDirectory=/home/zhouwr\n"
        "User=zhouwr\n");

    const ServiceInfo info =
        parseSystemctlShow(show, QStringLiteral("dsh-web.service"), ServiceScope::System);

    QVERIFY(info.invokesOfficialDshWeb);
    QCOMPARE(info.port, 8080);
    QVERIFY(!info.portIsDefault);
    QCOMPARE(info.host, QStringLiteral("0.0.0.0"));
    QVERIFY(!info.hostIsDefault);
    QCOMPARE(info.dshHome, QStringLiteral("/custom/home/.dsh"));
    QVERIFY(info.dshHomeSet);
    QCOMPARE(info.environment,
             (QStringList{QStringLiteral("HOME=/home/zhouwr"),
                          QStringLiteral("DSH_HOME=/custom/home/.dsh")}));
}

void TestServiceDiscovery::equalsFormFlags() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --host=127.0.0.2 --port=9090 ; }\n");

    const ServiceInfo info =
        parseSystemctlShow(show, QStringLiteral("dsh-web.service"), ServiceScope::System);

    QCOMPARE(info.port, 9090);
    QVERIFY(!info.portIsDefault);
    QCOMPARE(info.host, QStringLiteral("127.0.0.2"));
    QVERIFY(!info.hostIsDefault);
}

// ---------------------------------------------------------------------------
// 下面校验与选择逻辑均为纯函数，不依赖 QProcess / systemctl。
// ---------------------------------------------------------------------------

void TestServiceDiscovery::validateAcceptsOfficialLoaded() {
    const DiscoveredService d = validateCandidate(parseSystemctlShow(
        validSystemShowText(), QStringLiteral("dsh-web.service"), ServiceScope::System));
    QVERIFY(d.valid);
    QCOMPARE(asInt(d.rejection), asInt(RejectionReason::None));
    QVERIFY(d.rejectionDetail.isEmpty());
}

void TestServiceDiscovery::validateRejectsLoadNotLoaded() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=not-found\n"
        "ActiveState=inactive\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; }\n");

    const DiscoveredService d = validateCandidate(parseSystemctlShow(
        show, QStringLiteral("dsh-web.service"), ServiceScope::System));

    QVERIFY(!d.valid);
    QCOMPARE(asInt(d.rejection), asInt(RejectionReason::LoadStateNotLoaded));
    QVERIFY(!d.rejectionDetail.isEmpty());
}

void TestServiceDiscovery::validateRejectsForeignExecStart() {
    const QString show = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "ExecStart={ path=/usr/bin/node ; argv[]=/usr/bin/node /opt/other/server.js ; }\n");

    const DiscoveredService d = validateCandidate(parseSystemctlShow(
        show, QStringLiteral("dsh-web.service"), ServiceScope::System));

    QVERIFY(!d.valid);
    QCOMPARE(asInt(d.rejection), asInt(RejectionReason::ForeignExecStart));
}

void TestServiceDiscovery::selectPrefersUserWhenBothValid() {
    const DiscoveredService sys = validateCandidate(parseSystemctlShow(
        validSystemShowText(), QStringLiteral("dsh-web.service"), ServiceScope::System));
    const DiscoveredService usr = validateCandidate(parseSystemctlShow(
        validUserShowText(), QStringLiteral("dsh-web.service"), ServiceScope::User));
    QVERIFY(sys.valid);
    QVERIFY(usr.valid);

    const QVector<DiscoveredService> candidates{sys, usr};
    QCOMPARE(selectCandidateIndex(candidates), 1);  // 两者都有效时优先用户级
}

void TestServiceDiscovery::selectFallsBackToSystemWhenUserInvalid() {
    const DiscoveredService sys = validateCandidate(parseSystemctlShow(
        validSystemShowText(), QStringLiteral("dsh-web.service"), ServiceScope::System));
    const QString foreignUser = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ExecStart={ path=/usr/bin/node ; argv[]=/usr/bin/node /opt/other/server.js ; }\n");
    const DiscoveredService usr = validateCandidate(parseSystemctlShow(
        foreignUser, QStringLiteral("dsh-web.service"), ServiceScope::User));
    QVERIFY(sys.valid);
    QVERIFY(!usr.valid);

    const QVector<DiscoveredService> candidates{sys, usr};
    QCOMPARE(selectCandidateIndex(candidates), 0);  // 只有系统域有效 → 选系统域
}

void TestServiceDiscovery::selectUserWhenSystemAbsent() {
    DiscoveredService sys;
    sys.info.scope = ServiceScope::System;
    sys.valid = false;
    sys.rejection = RejectionReason::UnitNotFound;
    const DiscoveredService usr = validateCandidate(parseSystemctlShow(
        validUserShowText(), QStringLiteral("dsh-web.service"), ServiceScope::User));
    QVERIFY(usr.valid);

    const QVector<DiscoveredService> candidates{sys, usr};
    QCOMPARE(selectCandidateIndex(candidates), 1);  // 系统域不在 → 选用户域
}

void TestServiceDiscovery::selectNoneWhenNoValid() {
    DiscoveredService sys;
    sys.info.scope = ServiceScope::System;
    sys.valid = false;
    sys.rejection = RejectionReason::UnitNotFound;
    DiscoveredService usr;
    usr.info.scope = ServiceScope::User;
    usr.valid = false;
    usr.rejection = RejectionReason::ForeignExecStart;

    const QVector<DiscoveredService> candidates{sys, usr};
    QCOMPARE(selectCandidateIndex(candidates), -1);  // 均无效 → 不选中
}

void TestServiceDiscovery::selectedAccessor() {
    const DiscoveredService usr = validateCandidate(parseSystemctlShow(
        validUserShowText(), QStringLiteral("dsh-web.service"), ServiceScope::User));

    DiscoveryResult r;
    r.candidates = {usr};
    r.selectedIndex = 0;
    QVERIFY(r.selected());
    QCOMPARE(asInt(r.selected()->info.scope), asInt(ServiceScope::User));

    r.selectedIndex = -1;
    QVERIFY(r.selected() == nullptr);
}

void TestServiceDiscovery::detectedCarriesUserScopeWhenBothValid() {
    // 系统域与用户域候选都有效，且用户级单元监听非默认端口：
    // 验证选择偏好（优先用户级）通过 detected() 把 scope 与解析到的
    // host/port 一起传递出去（纯函数，不调用 systemctl）。
    const DiscoveredService sys = validateCandidate(parseSystemctlShow(
        validSystemShowText(), QStringLiteral("dsh-web.service"), ServiceScope::System));
    const QString userShow = QStringLiteral(
        "Id=dsh-web.service\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "MainPID=2000\n"
        "ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --host 127.0.0.1 --port 9090 ; }\n"
        "User=alice\n");
    const DiscoveredService usr = validateCandidate(parseSystemctlShow(
        userShow, QStringLiteral("dsh-web.service"), ServiceScope::User));
    QVERIFY(sys.valid);
    QVERIFY(usr.valid);

    DiscoveryResult r;
    r.candidates = {sys, usr};
    r.selectedIndex = selectCandidateIndex(r.candidates);
    QCOMPARE(r.selectedIndex, 1);  // 两者都有效时优先用户级

    const DetectedService detected = r.detected();
    QVERIFY(detected.valid);
    QCOMPARE(detected.unitName, QStringLiteral("dsh-web.service"));
    QCOMPARE(asInt(detected.scope), asInt(ServiceScope::User));
    QCOMPARE(detected.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(detected.port, 9090);
}

QTEST_GUILESS_MAIN(TestServiceDiscovery)
#include "test_service_discovery.moc"
