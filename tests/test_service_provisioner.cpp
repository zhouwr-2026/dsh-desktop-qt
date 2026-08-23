// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::ServiceProvisioner 单元测试。
//
// 只做路径决策、既有单元拒绝、原子写入、所有权记录、非法 spec 拒绝、系统级
// 无写入器拒绝，以及 reload/enable 只对"刚写入的单元"放行的验证。所有文件
// 都落在 QTemporaryDir 里，绝不触碰真实 systemd、/etc 或真实家目录。

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../src/service/ServiceProvisioner.h"

using dsh::service::DshServiceManager;
using dsh::service::ISystemUnitWriter;
using dsh::service::OperationResult;
using dsh::service::ProvisionPlan;
using dsh::service::ProvisionResult;
using dsh::service::ProvisionStatus;
using dsh::service::ServiceOperation;
using dsh::service::ServiceProvisioner;
using dsh::service::ServiceResult;
using dsh::service::ServiceScope;
using dsh::service::ServiceUnitBuilder;
using dsh::service::ServiceUnitResult;
using dsh::service::ServiceUnitSpec;

namespace {

/// 返回一份默认合法的用户级 spec。
ServiceUnitSpec validUserSpec() {
    ServiceUnitSpec spec;
    spec.dshExecutable = QStringLiteral("/usr/bin/dsh");
    spec.user = QStringLiteral("alice");
    spec.workingDirectory = QStringLiteral("/home/alice");
    spec.dshHome = QStringLiteral("/home/alice/.dsh");
    spec.host = QStringLiteral("127.0.0.1");
    spec.port = 3080;
    spec.scope = ServiceScope::User;
    return spec;
}

/// 返回一份合法的系统级 spec（User 必填）。
ServiceUnitSpec validSystemSpec() {
    ServiceUnitSpec spec = validUserSpec();
    spec.scope = ServiceScope::System;
    spec.user = QStringLiteral("alice");
    spec.dshHome = QStringLiteral("/home/alice/.dsh");
    return spec;
}

/// 测试替身：记录最后一次写入，并把内容原子写到磁盘（行为与真实受权写入器一致）。
class FakeSystemWriter : public ISystemUnitWriter {
public:
    bool fail{false};
    QString writtenPath;
    QByteArray writtenContents;
    int callCount{0};

    bool writeUnit(const QString& path, const QByteArray& contents,
                   QString* error) override {
        ++callCount;
        if (fail) {
            if (error) *error = QStringLiteral("fake writer failed");
            return false;
        }
        writtenPath = path;
        writtenContents = contents;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QStringLiteral("fake writer open failed");
            return false;
        }
        file.write(contents);
        file.close();
        return true;
    }
};

/// 返回 ServiceUnitBuilder 生成的预期单元文本。
QString expectedUnitText(const ServiceUnitSpec& spec) {
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    Q_ASSERT(r.ok);
    return r.unitText;
}

}  // namespace

class TestServiceProvisioner : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void pathSelectionUserScope();
    void pathSelectionSystemScope();
    void existingUnitRefused();
    void atomicWriteUserScope();
    void ownershipRecordedOnlyAfterProvision();
    void invalidSpecRejected();
    void noSystemWriterRejected();
    void systemWriterUsedWhenPresent();
    void enableOnlyAfterProvision();
    void enableAfterSuccessfulProvision();
};

void TestServiceProvisioner::initTestCase() {
    qRegisterMetaType<dsh::service::ProvisionStatus>(
        "dsh::service::ProvisionStatus");
    qRegisterMetaType<dsh::service::ProvisionPlan>(
        "dsh::service::ProvisionPlan");
    qRegisterMetaType<dsh::service::ProvisionResult>(
        "dsh::service::ProvisionResult");
    qRegisterMetaType<dsh::service::ServiceOperation>(
        "dsh::service::ServiceOperation");
    qRegisterMetaType<dsh::service::ServiceResult>(
        "dsh::service::ServiceResult");
    qRegisterMetaType<dsh::service::ServiceError>(
        "dsh::service::ServiceError");
    qRegisterMetaType<dsh::service::OperationResult>(
        "dsh::service::OperationResult");
    qRegisterMetaType<dsh::service::ServiceStatus>(
        "dsh::service::ServiceStatus");
}

// ---------------------------------------------------------------------------
// 路径选择
// ---------------------------------------------------------------------------

void TestServiceProvisioner::pathSelectionUserScope() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validUserSpec();

    const ProvisionPlan p =
        ServiceProvisioner::plan(spec, dir.path(), QString(), nullptr);
    QCOMPARE(p.status, ProvisionStatus::Ready);
    QCOMPARE(p.unitName, QStringLiteral("dsh-web.service"));
    QCOMPARE(int(p.scope), int(ServiceScope::User));
    QCOMPARE(p.destinationPath,
             dir.path() + QStringLiteral("/.config/systemd/user/dsh-web.service"));
    QVERIFY(!p.destinationExists);

    // 静态路径辅助默认值：指向 ~/.config/systemd/user/dsh-web.service。
    QVERIFY(ServiceProvisioner::userUnitPath().endsWith(
        QStringLiteral("/.config/systemd/user/dsh-web.service")));
}

void TestServiceProvisioner::pathSelectionSystemScope() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validSystemSpec();
    FakeSystemWriter writer;

    const ProvisionPlan p =
        ServiceProvisioner::plan(spec, QString(), dir.path(), &writer);
    QCOMPARE(p.status, ProvisionStatus::Ready);
    QCOMPARE(int(p.scope), int(ServiceScope::System));
    QCOMPARE(p.destinationPath, dir.path() + QStringLiteral("/dsh-web.service"));
    QVERIFY(!p.destinationExists);

    // 默认系统路径指向 /etc/systemd/system/dsh-web.service。
    QCOMPARE(ServiceProvisioner::systemUnitPath(),
             QStringLiteral("/etc/systemd/system/dsh-web.service"));
}

// ---------------------------------------------------------------------------
// 既有单元拒绝
// ---------------------------------------------------------------------------

void TestServiceProvisioner::existingUnitRefused() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validUserSpec();
    const QString dest =
        ServiceProvisioner::userUnitPath(dir.path());
    QVERIFY(QDir().mkpath(QFileInfo(dest).absolutePath()));

    // 预置一个"已存在"的单元，内容为哨兵。
    const QString sentinel = QStringLiteral("[Unit]\nDescription=existing\n");
    {
        QFile f(dest);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(sentinel.toUtf8());
        f.close();
    }

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());

    // plan 判定为"已存在、保持不变"。
    const ProvisionPlan p =
        ServiceProvisioner::plan(spec, dir.path(), QString(), nullptr);
    QCOMPARE(p.status, ProvisionStatus::ExistingUnitUnchanged);
    QVERIFY(p.destinationExists);

    // provision 不做任何写入，返回 -1，并上报 ExistingUnitUnchanged。
    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    QCOMPARE(prov.provision(spec), qint64(-1));
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::ExistingUnitUnchanged);
    QVERIFY(!r.newlyProvisioned);
    QVERIFY(!prov.newlyProvisioned());

    // 目标文件内容保持哨兵，未被覆盖。
    QFile readBack(dest);
    QVERIFY(readBack.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(readBack.readAll());
    QCOMPARE(content, sentinel);

    // 之后 enable 因没有"新写入的 unit"而被拒绝。
    QSignalSpy opFinished(&prov, &ServiceProvisioner::operationFinished);
    QCOMPARE(prov.enable(), qint64(-1));
    QCOMPARE(opFinished.count(), 1);
    const OperationResult orr =
        opFinished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(int(orr.result), int(ServiceResult::InvalidRequest));
    QCOMPARE(int(orr.operation), int(ServiceOperation::Enable));
}

// ---------------------------------------------------------------------------
// 原子写入（用户级）
// ---------------------------------------------------------------------------

void TestServiceProvisioner::atomicWriteUserScope() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validUserSpec();

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());

    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    const qint64 id = prov.provision(spec);
    QVERIFY(id >= 0);
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::Ready);
    QVERIFY(r.newlyProvisioned);
    QVERIFY(prov.newlyProvisioned());

    // 目标文件确实写入了与 ServiceUnitBuilder 一致的内容。
    const QString dest =
        ServiceProvisioner::userUnitPath(dir.path());
    QVERIFY(QFileInfo::exists(dest));
    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    QCOMPARE(content, expectedUnitText(spec));
    QVERIFY(content.startsWith(QStringLiteral("[Unit]\n")));
}

// ---------------------------------------------------------------------------
// 所有权记录（仅在成功供给后）
// ---------------------------------------------------------------------------

void TestServiceProvisioner::ownershipRecordedOnlyAfterProvision() {
    QTemporaryDir dir;
    QTemporaryDir stateDir;
    const ServiceUnitSpec spec = validUserSpec();
    const QString statePath = stateDir.filePath(QStringLiteral("services-owned.json"));

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());
    prov.setOwnershipStatePath(statePath);

    // 供给前没有任何记录。
    QVERIFY(!prov.ownership().contains(
        QStringLiteral("dsh-web.service"), ServiceScope::User));

    QCOMPARE(prov.provision(spec), qint64(1));

    // 供给后内存记录中存在该单元（unit+scope），指纹非空。
    QVERIFY(prov.ownership().contains(
        QStringLiteral("dsh-web.service"), ServiceScope::User));
    dsh::service::OwnedServiceRecord rec;
    QVERIFY(prov.ownership().find(
        QStringLiteral("dsh-web.service"), ServiceScope::User, &rec));
    QCOMPARE(rec.execStartFingerprint.size(), 64);
    QVERIFY(rec.createdAt.isValid());

    // 重新从磁盘加载也能读到这条记录（save 已持久化）。
    dsh::service::ServiceOwnership reloaded(statePath);
    QVERIFY2(reloaded.load(), "所有权状态文件应可加载");
    QVERIFY(reloaded.contains(QStringLiteral("dsh-web.service"),
                              ServiceScope::User));
}

// ---------------------------------------------------------------------------
// 非法 spec 拒绝
// ---------------------------------------------------------------------------

void TestServiceProvisioner::invalidSpecRejected() {
    QTemporaryDir dir;
    ServiceUnitSpec spec = validUserSpec();
    spec.port = 0;  // 非法端口

    const ProvisionPlan p =
        ServiceProvisioner::plan(spec, dir.path(), QString(), nullptr);
    QCOMPARE(p.status, ProvisionStatus::InvalidSpec);
    QVERIFY(!p.error.isEmpty());
    QVERIFY(p.unitText.isEmpty());

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());
    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    QCOMPARE(prov.provision(spec), qint64(-1));
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::InvalidSpec);
    QVERIFY(!r.newlyProvisioned);

    // 目标文件未被创建。
    QVERIFY(!QFileInfo::exists(
        ServiceProvisioner::userUnitPath(dir.path())));
}

// ---------------------------------------------------------------------------
// 系统级无写入器拒绝
// ---------------------------------------------------------------------------

void TestServiceProvisioner::noSystemWriterRejected() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validSystemSpec();

    // plan（无写入器）-> NoSystemWriter。
    const ProvisionPlan p =
        ServiceProvisioner::plan(spec, QString(), dir.path(), nullptr);
    QCOMPARE(p.status, ProvisionStatus::NoSystemWriter);

    // provision（未注入写入器）-> NoSystemWriter，不写盘。
    ServiceProvisioner prov;
    prov.setSystemUnitDir(dir.path());
    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    QCOMPARE(prov.provision(spec), qint64(-1));
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::NoSystemWriter);
    QVERIFY(!r.newlyProvisioned);
    QVERIFY(!QFileInfo::exists(
        ServiceProvisioner::systemUnitPath(dir.path())));
}

// ---------------------------------------------------------------------------
// 系统级受权写入器被使用
// ---------------------------------------------------------------------------

void TestServiceProvisioner::systemWriterUsedWhenPresent() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validSystemSpec();
    auto writer = std::make_shared<FakeSystemWriter>();

    ServiceProvisioner prov;
    prov.setSystemUnitDir(dir.path());
    prov.setSystemUnitWriter(writer);

    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    const qint64 id = prov.provision(spec);
    QVERIFY(id >= 0);
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::Ready);
    QVERIFY(r.newlyProvisioned);

    // 写入器收到了正确的路径与内容。
    QCOMPARE(writer->callCount, 1);
    QCOMPARE(writer->writtenPath,
             ServiceProvisioner::systemUnitPath(dir.path()));
    QCOMPARE(QString::fromUtf8(writer->writtenContents),
             expectedUnitText(spec));
    QVERIFY(QFileInfo::exists(writer->writtenPath));
}

// ---------------------------------------------------------------------------
// reload/enable 只对新写入放行
// ---------------------------------------------------------------------------

void TestServiceProvisioner::enableOnlyAfterProvision() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validUserSpec();

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());

    // 本会话还没写入过任何 unit：enable 立即被拒（InvalidRequest）。
    QSignalSpy opFinished(&prov, &ServiceProvisioner::operationFinished);
    QVERIFY(!prov.newlyProvisioned());
    QCOMPARE(prov.enable(), qint64(-1));
    QCOMPARE(prov.daemonReload(), qint64(-1));
    QCOMPARE(opFinished.count(), 2);  // enable + daemonReload 各上报一次
    // 两次上报都是 InvalidRequest。
    const int total = opFinished.count();
    for (int i = 0; i < total; ++i) {
        const OperationResult r =
            opFinished.takeFirst().at(0).value<OperationResult>();
        QCOMPARE(int(r.result), int(ServiceResult::InvalidRequest));
    }
}

void TestServiceProvisioner::enableAfterSuccessfulProvision() {
    QTemporaryDir dir;
    const ServiceUnitSpec spec = validUserSpec();

    ServiceProvisioner prov;
    prov.setUserHomePath(dir.path());
    // 用 /bin/true 作为替身 systemctl（成功返回 0）。
    prov.serviceManager().setSystemctlExecutable(QStringLiteral("/bin/true"));

    // 先成功写入一个新 unit。
    QSignalSpy finished(&prov, &ServiceProvisioner::provisionFinished);
    QVERIFY(prov.provision(spec) >= 0);
    QCOMPARE(finished.count(), 1);
    const ProvisionResult r =
        finished.takeFirst().at(0).value<ProvisionResult>();
    QCOMPARE(r.status, ProvisionStatus::Ready);
    QVERIFY(r.newlyProvisioned);

    // 现在 enable 放行，替身 systemctl 返回 0 -> Success。
    QSignalSpy opFinished(&prov, &ServiceProvisioner::operationFinished);
    const qint64 enableId = prov.enable();
    QVERIFY(enableId >= 0);
    QVERIFY(opFinished.wait(3000));
    QCOMPARE(opFinished.count(), 1);
    const OperationResult orr =
        opFinished.takeFirst().at(0).value<OperationResult>();
    QCOMPARE(int(orr.operation), int(ServiceOperation::Enable));
    QCOMPARE(int(orr.result), int(ServiceResult::Success));
}

QTEST_GUILESS_MAIN(TestServiceProvisioner)
#include "test_service_provisioner.moc"
