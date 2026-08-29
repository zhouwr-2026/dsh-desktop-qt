// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::DshServiceManager 单元测试。
//
// 分两块：
//   1. 纯函数：system/user 命令参数构建、提权包裹、invalid unit 名校验、
//      timeout/result 映射、官方验证与归属策略。不启动任何进程。
//   2. 异步行为：用测试替身（/bin/true、/bin/false、一个忽略参数并休眠的
//      小脚本）驱动真实的 QProcess 异步路径，验证 operationStarted /
//      operationFinished / journalTailLine 与超时、取消映射。均不依赖真实的
//      systemctl / journalctl / systemd。

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../src/service/DshServiceManager.h"
#include "../src/service/SystemctlCommandBuilder.h"

using dsh::service::DetectedService;
using dsh::service::DshServiceManager;
using dsh::service::OperationResult;
using dsh::service::ProcessExitStatus;
using dsh::service::ProcessOutcome;
using dsh::service::ResolvedCommand;
using dsh::service::ServiceError;
using dsh::service::ServiceInfo;
using dsh::service::ServiceOperation;
using dsh::service::ServiceResult;
using dsh::service::ServiceScope;
using dsh::service::SystemctlCommandBuilder;

namespace {

int asInt(ServiceResult v) { return static_cast<int>(v); }
int asInt(ServiceError v) { return static_cast<int>(v); }
int asInt(ServiceOperation v) { return static_cast<int>(v); }

/// 已通过官方验证的用户级目标（可直接变更）。
DetectedService validUserTarget() {
    DetectedService d;
    d.unitName = QStringLiteral("dsh-web.service");
    d.scope = ServiceScope::User;
    d.host = QStringLiteral("127.0.0.1");
    d.port = 3080;
    d.valid = true;
    return d;
}

/// 写一个忽略参数并执行的脚本替身（用于驱动真实进程）。
/// \p body 为脚本正文（如 ``sleep 10``）。
QString makeHelperScript(QTemporaryDir& dir, const QString& body) {
    const QString path = dir.filePath(QStringLiteral("helper.sh"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    file.write("#!/bin/sh\n");
    file.write(body.toUtf8());
    file.write("\n");
    file.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                    | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                    | QFileDevice::ExeOther);
    return path;
}

}  // namespace

class TestServiceManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // 纯函数：命令参数构建 / 提权 / unit 名校验 / 映射 / 归属策略。
    void systemArgumentsUserScope();
    void systemArgumentsSystemScope();
    void commandElevation();
    void noElevationForReadOnlyAndUserScope();
    void journalctlArgumentsUserAndSystem();
    void daemonReloadAndEnableArguments();
    void daemonReloadEnableElevation();
    void validUnitNames();
    void invalidUnitNames();
    void mapProcessResultSuccessAndFailure();
    void mapProcessResultTimeoutCancelNotStarted();
    void evaluateManageability();

    // 异步行为：用测试替身驱动真实 QProcess。
    void asyncStopSuccess();
    void asyncStopNonZeroExit();
    void asyncTimeout();
    void asyncCancellation();
    void journalTailLinesAndCancel();
    void rejectBeforePreflight();
    void discoveryThenMutate();
};

void TestServiceManager::initTestCase() {
    // 确保 QSignalSpy 能捕获自定义类型。
    qRegisterMetaType<dsh::service::ServiceOperation>("dsh::service::ServiceOperation");
    qRegisterMetaType<dsh::service::ServiceResult>("dsh::service::ServiceResult");
    qRegisterMetaType<dsh::service::ServiceError>("dsh::service::ServiceError");
    qRegisterMetaType<dsh::service::OperationResult>("dsh::service::OperationResult");
    qRegisterMetaType<dsh::service::ServiceStatus>("dsh::service::ServiceStatus");
}

// ---------------------------------------------------------------------------
// 命令参数构建
// ---------------------------------------------------------------------------

void TestServiceManager::systemArgumentsUserScope() {
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Start, ServiceScope::User,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("start"), QStringLiteral("dsh-web.service")}));

    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Stop, ServiceScope::User,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("stop"), QStringLiteral("dsh-web.service")}));

    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Status, ServiceScope::User,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("show"), QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::systemArgumentsSystemScope() {
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Restart, ServiceScope::System,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--no-pager"), QStringLiteral("restart"),
                          QStringLiteral("dsh-web.service")}));

    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Discovery, ServiceScope::System,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--no-pager"), QStringLiteral("show"),
                          QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::commandElevation() {
    const qint64 euidNonRoot = 1000;
    const qint64 euidRoot = 0;

    // 系统级 + 变更 + 非 root → 提权。
    QVERIFY(SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Start, ServiceScope::System, euidNonRoot));
    QVERIFY(SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Restart, ServiceScope::System, euidNonRoot));
    QVERIFY(SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Stop, ServiceScope::System, euidNonRoot));

    // 系统级 + 变更 + root → 不提权。
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Start, ServiceScope::System, euidRoot));

    const ResolvedCommand elevated = SystemctlCommandBuilder::resolveCommand(
        ServiceOperation::Start, ServiceScope::System, QStringLiteral("dsh-web.service"),
        QStringLiteral("/usr/bin/systemctl"), QStringLiteral("/usr/bin/pkexec"),
        euidNonRoot);
    QCOMPARE(elevated.program, QStringLiteral("/usr/bin/pkexec"));
    QCOMPARE(elevated.arguments,
             QStringList({QStringLiteral("--disable-internal-agent"),
                          QStringLiteral("/usr/bin/systemctl"),
                          QStringLiteral("--no-pager"), QStringLiteral("start"),
                          QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::noElevationForReadOnlyAndUserScope() {
    const qint64 euidNonRoot = 1000;

    // 系统级 + 只读（Status/Discovery）+ 非 root → 不提权。
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Status, ServiceScope::System, euidNonRoot));
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Discovery, ServiceScope::System, euidNonRoot));

    // 用户级 + 变更 + 非 root → 不提权。
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Stop, ServiceScope::User, euidNonRoot));

    // pkexec 缺省（空）时，即使系统级变更也不包裹。
    const ResolvedCommand noPkexec = SystemctlCommandBuilder::resolveCommand(
        ServiceOperation::Restart, ServiceScope::System, QStringLiteral("dsh-web.service"),
        QStringLiteral("/usr/bin/systemctl"), QString(), euidNonRoot);
    QCOMPARE(noPkexec.program, QStringLiteral("/usr/bin/systemctl"));
    QCOMPARE(noPkexec.arguments,
             QStringList({QStringLiteral("--no-pager"), QStringLiteral("restart"),
                          QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::journalctlArgumentsUserAndSystem() {
    QCOMPARE(SystemctlCommandBuilder::journalctlArguments(
                 ServiceScope::User, QStringLiteral("dsh-web.service"), 50, true),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("-n"), QStringLiteral("50"),
                          QStringLiteral("-u"), QStringLiteral("dsh-web.service"),
                          QStringLiteral("-f")}));

    QCOMPARE(SystemctlCommandBuilder::journalctlArguments(
                 ServiceScope::System, QStringLiteral("dsh-web.service"), 10, false),
             QStringList({QStringLiteral("--no-pager"), QStringLiteral("-n"),
                          QStringLiteral("10"), QStringLiteral("-u"),
                          QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::daemonReloadAndEnableArguments() {
    // daemon-reload 无 unit 参数。
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::DaemonReload, ServiceScope::User, QString()),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("daemon-reload")}));
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::DaemonReload, ServiceScope::System, QString()),
             QStringList({QStringLiteral("--no-pager"),
                          QStringLiteral("daemon-reload")}));

    // enable 带 unit 名。
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Enable, ServiceScope::User,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--user"), QStringLiteral("--no-pager"),
                          QStringLiteral("enable"),
                          QStringLiteral("dsh-web.service")}));
    QCOMPARE(SystemctlCommandBuilder::systemctlArguments(
                 ServiceOperation::Enable, ServiceScope::System,
                 QStringLiteral("dsh-web.service")),
             QStringList({QStringLiteral("--no-pager"), QStringLiteral("enable"),
                          QStringLiteral("dsh-web.service")}));
}

void TestServiceManager::daemonReloadEnableElevation() {
    const qint64 euidNonRoot = 1000;
    const qint64 euidRoot = 0;

    // 系统级 + reload/enable + 非 root -> 提权。
    QVERIFY(SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::DaemonReload, ServiceScope::System, euidNonRoot));
    QVERIFY(SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Enable, ServiceScope::System, euidNonRoot));

    // 系统级 + reload/enable + root -> 不提权。
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::DaemonReload, ServiceScope::System, euidRoot));
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Enable, ServiceScope::System, euidRoot));

    // 用户级 + reload/enable -> 不提权。
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::DaemonReload, ServiceScope::User, euidNonRoot));
    QVERIFY(!SystemctlCommandBuilder::operationNeedsElevation(
        ServiceOperation::Enable, ServiceScope::User, euidNonRoot));

    // resolveCommand 在系统级 + 非 root 时用 pkexec 包裹 enable。
    const ResolvedCommand enabled = SystemctlCommandBuilder::resolveCommand(
        ServiceOperation::Enable, ServiceScope::System, QStringLiteral("dsh-web.service"),
        QStringLiteral("/usr/bin/systemctl"), QStringLiteral("/usr/bin/pkexec"),
        euidNonRoot);
    QCOMPARE(enabled.program, QStringLiteral("/usr/bin/pkexec"));
    QCOMPARE(enabled.arguments,
             QStringList({QStringLiteral("--disable-internal-agent"),
                          QStringLiteral("/usr/bin/systemctl"),
                          QStringLiteral("--no-pager"), QStringLiteral("enable"),
                          QStringLiteral("dsh-web.service")}));
}

// ---------------------------------------------------------------------------
// unit 名校验
// ---------------------------------------------------------------------------

void TestServiceManager::validUnitNames() {
    QString error;
    QVERIFY(SystemctlCommandBuilder::isValidUnitName(QStringLiteral("dsh-web.service"), &error));
    QVERIFY(SystemctlCommandBuilder::isValidUnitName(QStringLiteral("foo.socket"), &error));
    QVERIFY(SystemctlCommandBuilder::isValidUnitName(QStringLiteral("a-b_c.target"), &error));
}

void TestServiceManager::invalidUnitNames() {
    QString error;
    const QStringList bad = {
        QString(),                                 // 空
        QStringLiteral("dsh-web"),                 // 缺后缀
        QStringLiteral("/etc/dsh-web.service"),    // 路径
        QStringLiteral("dsh-web.servic"),          // 后缀不完整
        QStringLiteral(".dsh-web.service"),        // 以点开头
        QStringLiteral("..service"),               // 隐藏
        QStringLiteral("dsh web.service"),         // 空白
        QStringLiteral("dsh-web.service\nx"),      // 换行
        QStringLiteral("dsh-web.service|evil"),    // 管道
        QStringLiteral("name$(id).service"),       // 命令替换元字符
        QStringLiteral("dsh-web.service;rm"),      // 命令分隔符
    };
    for (const QString& name : bad) {
        QVERIFY2(!SystemctlCommandBuilder::isValidUnitName(name, &error),
                 qPrintable(QStringLiteral("应拒绝: '%1'").arg(name)));
    }
}

// ---------------------------------------------------------------------------
// 结果 / 超时映射
// ---------------------------------------------------------------------------

void TestServiceManager::mapProcessResultSuccessAndFailure() {
    ProcessOutcome ok;
    ok.started = true;
    ok.exitStatus = ProcessExitStatus::Normal;
    ok.exitCode = 0;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(ok)),
             asInt(ServiceResult::Success));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(ok)), asInt(ServiceError::None));

    ProcessOutcome nonzero;
    nonzero.exitCode = 3;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(nonzero)),
             asInt(ServiceResult::Failed));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(nonzero)),
             asInt(ServiceError::NonZeroExit));

    ProcessOutcome crash;
    crash.exitStatus = ProcessExitStatus::Crashed;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(crash)),
             asInt(ServiceResult::Failed));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(crash)),
             asInt(ServiceError::NonZeroExit));
}

void TestServiceManager::mapProcessResultTimeoutCancelNotStarted() {
    ProcessOutcome timeout;
    timeout.timedOut = true;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(timeout)),
             asInt(ServiceResult::Timeout));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(timeout)),
             asInt(ServiceError::ProcessTimedOut));

    ProcessOutcome cancelled;
    cancelled.cancelled = true;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(cancelled)),
             asInt(ServiceResult::Cancelled));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(cancelled)),
             asInt(ServiceError::Cancelled));

    ProcessOutcome notStarted;
    notStarted.started = false;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(notStarted)),
             asInt(ServiceResult::Failed));
    QCOMPARE(asInt(DshServiceManager::mapProcessError(notStarted)),
             asInt(ServiceError::ProcessStartFailed));

    // 超时优先于取消。
    ProcessOutcome both;
    both.timedOut = true;
    both.cancelled = true;
    QCOMPARE(asInt(DshServiceManager::mapProcessResult(both)),
             asInt(ServiceResult::Timeout));
}

// ---------------------------------------------------------------------------
// 官方验证 / 归属策略
// ---------------------------------------------------------------------------

void TestServiceManager::evaluateManageability() {
    ServiceInfo loaded;
    loaded.unitName = QStringLiteral("dsh-web.service");
    loaded.scope = ServiceScope::User;
    loaded.loadState = QStringLiteral("loaded");
    loaded.activeState = QStringLiteral("active");
    loaded.user = QStringLiteral("alice");
    loaded.invokesOfficialDshWeb = true;
    QCOMPARE(asInt(DshServiceManager::evaluateManageability(loaded, QStringLiteral("alice"))),
             asInt(ServiceResult::Success));

    // 用户级单元属于其他用户 → Unmanaged。
    QCOMPARE(asInt(DshServiceManager::evaluateManageability(loaded, QStringLiteral("bob"))),
             asInt(ServiceResult::Unmanaged));

    // LoadState 非 loaded → NotLoaded。
    ServiceInfo notLoaded = loaded;
    notLoaded.loadState = QStringLiteral("not-found");
    QCOMPARE(asInt(DshServiceManager::evaluateManageability(notLoaded, QStringLiteral("alice"))),
             asInt(ServiceResult::NotLoaded));

    // 非官方入口 → ForeignService。
    ServiceInfo foreign = loaded;
    foreign.invokesOfficialDshWeb = false;
    QCOMPARE(asInt(DshServiceManager::evaluateManageability(foreign, QStringLiteral("alice"))),
             asInt(ServiceResult::ForeignService));

    // 系统级服务：即使 User 为其他用户也可经 polkit 管理，不判 Unmanaged。
    ServiceInfo system = loaded;
    system.scope = ServiceScope::System;
    system.user = QStringLiteral("zhouwr");
    QCOMPARE(asInt(DshServiceManager::evaluateManageability(system, QStringLiteral("alice"))),
             asInt(ServiceResult::Success));
}

// ---------------------------------------------------------------------------
// 异步行为（测试替身）
// ---------------------------------------------------------------------------

void TestServiceManager::asyncStopSuccess() {
    DshServiceManager mgr;
    mgr.setSystemctlExecutable(QStringLiteral("/bin/true"));
    mgr.setDetectedService(validUserTarget());

    QSignalSpy started(&mgr, &DshServiceManager::operationStarted);
    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);

    const qint64 id = mgr.stop();
    QVERIFY(id >= 0);
    QCOMPARE(started.count(), 1);
    QVERIFY(finished.wait(3000));
    QCOMPARE(finished.count(), 1);

    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Success));
    QCOMPARE(int(r.operation), int(ServiceOperation::Stop));
    QVERIFY(!mgr.isBusy());
}

void TestServiceManager::asyncStopNonZeroExit() {
    DshServiceManager mgr;
    mgr.setSystemctlExecutable(QStringLiteral("/bin/false"));
    mgr.setDetectedService(validUserTarget());

    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);
    const qint64 id = mgr.stop();
    QVERIFY(id >= 0);
    QVERIFY(finished.wait(3000));

    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Failed));
    QVERIFY(mgr.isBusy() == false);
}

void TestServiceManager::asyncTimeout() {
    QTemporaryDir dir;
    const QString helper = makeHelperScript(dir, QStringLiteral("sleep 10"));
    QVERIFY(!helper.isEmpty());

    DshServiceManager mgr;
    mgr.setSystemctlExecutable(helper);
    mgr.setOperationTimeoutMs(250);
    mgr.setDetectedService(validUserTarget());

    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);
    const qint64 id = mgr.start();
    QVERIFY(id >= 0);
    QVERIFY(finished.wait(4000));

    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Timeout));
}

void TestServiceManager::asyncCancellation() {
    QTemporaryDir dir;
    const QString helper = makeHelperScript(dir, QStringLiteral("sleep 10"));
    QVERIFY(!helper.isEmpty());

    DshServiceManager mgr;
    mgr.setSystemctlExecutable(helper);
    mgr.setDetectedService(validUserTarget());

    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);
    const qint64 id = mgr.restart();
    QVERIFY(id >= 0);
    mgr.cancel(id);
    QVERIFY(finished.wait(4000));

    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Cancelled));
}

void TestServiceManager::journalTailLinesAndCancel() {
    QTemporaryDir dir;
    // 输出一行后沉睡；用于验证行拆分与取消。
    const QString helper = makeHelperScript(dir, QStringLiteral("echo hello; sleep 10"));
    QVERIFY(!helper.isEmpty());

    DshServiceManager mgr;
    mgr.setJournalctlExecutable(helper);
    mgr.setDetectedService(validUserTarget());

    QSignalSpy lines(&mgr, &DshServiceManager::journalTailLine);
    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);

    const qint64 id = mgr.startJournalTail(5);
    QVERIFY(id >= 0);
    QVERIFY(lines.wait(2000));
    QVERIFY(!lines.isEmpty());
    QCOMPARE(lines.first().at(0).toString().trimmed(), QStringLiteral("hello"));

    QVERIFY(mgr.stopJournalTail());
    QVERIFY(finished.wait(4000));
    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Cancelled));
}

void TestServiceManager::rejectBeforePreflight() {
    // 未设定目标 → 变更请求立即（同步）上报 InvalidRequest。
    DshServiceManager mgr;
    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);
    QCOMPARE(mgr.stop(), qint64(-1));
    QCOMPARE(finished.count(), 1);
    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::InvalidRequest));

    // 已设定目标但未通过官方验证 → NotValidated。
    DshServiceManager mgr2;
    QVERIFY(mgr2.setUnit(QStringLiteral("dsh-web.service"), ServiceScope::User));
    QSignalSpy f2(&mgr2, &DshServiceManager::operationFinished);
    QCOMPARE(mgr2.start(), qint64(-1));
    QCOMPARE(f2.count(), 1);
    QCOMPARE(asInt(f2.takeFirst().at(0).value<OperationResult>().result),
             asInt(ServiceResult::NotValidated));
}

void TestServiceManager::discoveryThenMutate() {
    QTemporaryDir dir;
    // 假 systemctl：用户域 show 返回通过官方校验的快照，系统域返回未加载，
    // 以便验证「发现 —— 校验 —— 选中用户级 —— 之后可变更」的整条链路。
    const QString body = QStringLiteral(
        "case \" $* \" in\n"
        "  *\" --user \"*) echo \"Id=dsh-web.service\"; echo \"LoadState=loaded\"; "
        "echo \"ActiveState=active\"; echo \"SubState=running\"; echo \"MainPID=1234\"; "
        "echo \"ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; }\"; "
        "echo \"User=alice\";;\n"
        "  *) echo \"Id=dsh-web.service\"; echo \"LoadState=not-found\"; "
        "echo \"ActiveState=inactive\";;\n"
        "esac\n"
        "exit 0\n");
    const QString helper = makeHelperScript(dir, body);
    QVERIFY(!helper.isEmpty());

    DshServiceManager mgr;
    mgr.setSystemctlExecutable(helper);
    mgr.setCurrentUser(QStringLiteral("alice"));

    QSignalSpy finished(&mgr, &DshServiceManager::operationFinished);
    const qint64 id = mgr.requestDiscovery();
    QVERIFY(id >= 0);
    QVERIFY(finished.wait(4000));

    const OperationResult r =
        finished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(asInt(r.result), asInt(ServiceResult::Success));
    QCOMPARE(int(r.operation), int(ServiceOperation::Discovery));

    // 用户级有效、系统级未加载 → 选中用户级（下标 1）。
    QCOMPARE(mgr.lastDiscovery().selectedIndex, 1);
    QVERIFY(mgr.isValidated());
    QCOMPARE(int(mgr.scope()), int(ServiceScope::User));
    QCOMPARE(mgr.unitName(), QStringLiteral("dsh-web.service"));

    // 发现后目标已通过验证，可以直接变更（假 systemctl 对 stop 也退出 0）。
    QSignalSpy f2(&mgr, &DshServiceManager::operationFinished);
    QVERIFY(mgr.stop() >= 0);
    QVERIFY(f2.wait(3000));
    QCOMPARE(asInt(f2.takeFirst().at(0).value<OperationResult>().result),
             asInt(ServiceResult::Success));
}

QTEST_GUILESS_MAIN(TestServiceManager)
#include "test_service_manager.moc"
