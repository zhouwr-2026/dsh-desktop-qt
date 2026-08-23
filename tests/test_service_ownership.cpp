// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::ServiceOwnership 所有权记录与一致性校验的单元测试。
//
// 只测试纯序列化、内存记录、原子持久化与一致性比对，绝不执行任何
// systemctl / 生命周期操作。

#include <QTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "../src/service/ServiceOwnership.h"

using dsh::service::ConsistencyResult;
using dsh::service::OwnedServiceRecord;
using dsh::service::ServiceOwnership;
using dsh::service::ServiceScope;
using dsh::service::execStartFingerprintsEqual;
using dsh::service::makeExecStartFingerprint;
using dsh::service::parseRecords;
using dsh::service::recordFromJson;
using dsh::service::recordToJson;
using dsh::service::scopeFromString;
using dsh::service::scopeToString;
using dsh::service::serializeRecords;

namespace {

const QString kSystemExec = QStringLiteral(
    "{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web ; }");
const QString kUserExec = QStringLiteral(
    "{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --no-open ; }");

/// 在专用临时目录中构造一个 ServiceOwnership，指向 ``services-owned.json``。
class TempOwnership {
public:
    explicit TempOwnership() {
        QVERIFY(tempDir_.isValid());
    }
    QString dir() const { return tempDir_.path(); }
    QString filePath() const { return tempDir_.filePath("services-owned.json"); }

private:
    QTemporaryDir tempDir_;
};

}  // namespace

class TestServiceOwnership : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void scopeStringRoundTrip();
    void fingerprintIsStableSha256Hex();
    void recordRoundTripSerialization();
    void loadSaveFileRoundTrip();
    void malformedJsonRejected();
    void fieldValidationRejected();
    void fingerprintMismatchDetected();
    void systemAndUserRecordsAreSeparate();
    void recordIsIdempotentUpsert();
    void lookupByUnitAndScope();
    void missingStateFileLoadsEmpty();
    void consistencyNotRecorded();
    void defaultPathUnderStateLocation();
};

void TestServiceOwnership::initTestCase() {
    // 使 QStandardPaths 能生成确定的应用状态位置（避免歧义报错）。
    QCoreApplication::setOrganizationName(QStringLiteral("dsh-desktop-test"));
    QCoreApplication::setApplicationName(QStringLiteral("service-ownership-test"));
}

void TestServiceOwnership::scopeStringRoundTrip() {
    QCOMPARE(scopeToString(ServiceScope::System), QStringLiteral("system"));
    QCOMPARE(scopeToString(ServiceScope::User), QStringLiteral("user"));

    ServiceScope scope{ServiceScope::System};
    QVERIFY(scopeFromString(QStringLiteral("system"), &scope));
    QCOMPARE(scope, ServiceScope::System);
    QVERIFY(scopeFromString(QStringLiteral("user"), &scope));
    QCOMPARE(scope, ServiceScope::User);

    QVERIFY(!scopeFromString(QStringLiteral("System"), &scope));
    QVERIFY(!scopeFromString(QStringLiteral(""), &scope));
    QVERIFY(!scopeFromString(QStringLiteral("root"), &scope));

    // 反向解析失败时不改动出参。
    ServiceScope before{ServiceScope::User};
    QVERIFY(!scopeFromString(QStringLiteral("bogus"), &before));
    QCOMPARE(before, ServiceScope::User);
}

void TestServiceOwnership::fingerprintIsStableSha256Hex() {
    const QString fp = makeExecStartFingerprint(kSystemExec);
    QCOMPARE(fp.size(), 64);
    // 全部为小写十六进制。
    for (const QChar ch : fp) {
        const ushort code = ch.unicode();
        const bool hex = (code >= '0' && code <= '9')
            || (code >= 'a' && code <= 'f');
        QVERIFY(hex);
    }
    // 确定性：同输入同指纹。
    QCOMPARE(fp, makeExecStartFingerprint(kSystemExec));
    // 不同输入不同指纹。
    QVERIFY(fp != makeExecStartFingerprint(kUserExec));

    // 空串也产生稳定的指纹。
    QCOMPARE(makeExecStartFingerprint(QString()),
             makeExecStartFingerprint(QString()));

    QVERIFY(execStartFingerprintsEqual(kSystemExec, kSystemExec));
    QVERIFY(!execStartFingerprintsEqual(kSystemExec, kUserExec));
}

void TestServiceOwnership::recordRoundTripSerialization() {
    OwnedServiceRecord original;
    original.unitName = QStringLiteral("dsh-web.service");
    original.scope = ServiceScope::User;
    original.createdAt = QDateTime::fromString(
        QStringLiteral("2024-08-23T17:40:00.000Z"), Qt::ISODateWithMs);
    original.execStartFingerprint = makeExecStartFingerprint(kUserExec);

    const QJsonObject json = recordToJson(original);
    QCOMPARE(json.value(QStringLiteral("unit")).toString(),
             QStringLiteral("dsh-web.service"));
    QCOMPARE(json.value(QStringLiteral("scope")).toString(),
             QStringLiteral("user"));
    QCOMPARE(json.value(QStringLiteral("execStartSha256")).toString(),
             original.execStartFingerprint);

    OwnedServiceRecord parsed;
    QString error;
    QVERIFY2(recordFromJson(json, &parsed, &error),
             qPrintable(error));
    QCOMPARE(parsed, original);
}

void TestServiceOwnership::loadSaveFileRoundTrip() {
    TempOwnership temp;
    const QString path = temp.filePath();

    ServiceOwnership first(path);
    first.record(QStringLiteral("dsh-web.service"), ServiceScope::System,
                 kSystemExec);
    QString originalCreatedIso;
    {
        OwnedServiceRecord rec;
        QVERIFY(first.find(QStringLiteral("dsh-web.service"),
                           ServiceScope::System, &rec));
        QVERIFY(rec.createdAt.isValid());
        QVERIFY(!rec.execStartFingerprint.isEmpty());
        originalCreatedIso = rec.createdAt.toUTC().toString(Qt::ISODateWithMs);
    }
    QVERIFY2(first.save(), "save() 应成功");

    // 从磁盘重新加载，验证字段一致。
    ServiceOwnership second(path);
    QVERIFY2(second.load(), "load() 应成功");
    QCOMPARE(second.count(), 1);

    OwnedServiceRecord reloaded;
    QVERIFY(second.find(QStringLiteral("dsh-web.service"),
                        ServiceScope::System, &reloaded));
    QCOMPARE(reloaded.execStartFingerprint,
             makeExecStartFingerprint(kSystemExec));
    // createdAt 经过序列化/反序列化后应原样保留。
    QVERIFY(reloaded.createdAt.isValid());
    QCOMPARE(reloaded.createdAt.toUTC().toString(Qt::ISODateWithMs),
             originalCreatedIso);
}

void TestServiceOwnership::malformedJsonRejected() {
    QVector<OwnedServiceRecord> records;
    QString error;

    // 语法错误。
    QVERIFY(!parseRecords(QByteArrayLiteral("{ not valid json"), &records, &error));
    QVERIFY(records.isEmpty());
    QVERIFY(!error.isEmpty());

    // 顶层不是对象。
    records.clear();
    error.clear();
    QVERIFY(!parseRecords(QByteArrayLiteral("[1,2,3]"), &records, &error));
    QVERIFY(records.isEmpty());

    // 版本不符。
    records.clear();
    error.clear();
    QVERIFY(!parseRecords(
        QByteArrayLiteral(R"({"version":2,"services":[]})"), &records, &error));
    QVERIFY(records.isEmpty());

    // 缺失 services 数组。
    records.clear();
    error.clear();
    QVERIFY(!parseRecords(
        QByteArrayLiteral(R"({"version":1})"), &records, &error));
    QVERIFY(records.isEmpty());

    // services 不是数组。
    records.clear();
    error.clear();
    QVERIFY(!parseRecords(
        QByteArrayLiteral(R"({"version":1,"services":{}})"), &records, &error));
    QVERIFY(records.isEmpty());

    // 数组条目不是对象。
    records.clear();
    error.clear();
    QVERIFY(!parseRecords(
        QByteArrayLiteral(R"({"version":1,"services":["x"]})"), &records, &error));
    QVERIFY(records.isEmpty());

    // services 数组内嵌非法字段。
    records.clear();
    error.clear();
    const QString badEntry =
        QStringLiteral(R"({"version":1,"services":[{"unit":"dsh-web.service",)"
                        R"("scope":"system","createdAt":"2024-08-23T17:40:00.000Z",)"
                        R"("execStartSha256":"not-a-hash"}]})");
    QVERIFY(!parseRecords(badEntry.toUtf8(), &records, &error));
    QVERIFY(records.isEmpty());
}

void TestServiceOwnership::fieldValidationRejected() {
    // recordFromJson 单独校验各项字段。
    QJsonObject json;
    json.insert(QStringLiteral("unit"), QStringLiteral("dsh-web.service"));
    json.insert(QStringLiteral("scope"), QStringLiteral("system"));
    json.insert(QStringLiteral("createdAt"),
                QStringLiteral("2024-08-23T17:40:00.000Z"));
    json.insert(QStringLiteral("execStartSha256"),
                QString(64, QLatin1Char('a')));

    OwnedServiceRecord record;
    QString error;

    // 完全合法。
    QVERIFY2(recordFromJson(json, &record, &error),
             qPrintable(error));

    // 缺 unit。
    QJsonObject noUnit = json;
    noUnit.remove(QStringLiteral("unit"));
    QVERIFY(!recordFromJson(noUnit, &record, &error));
    QVERIFY(!error.isEmpty());

    // unit 为空串。
    QJsonObject emptyUnit = json;
    emptyUnit.insert(QStringLiteral("unit"), QString());
    QVERIFY(!recordFromJson(emptyUnit, &record, &error));

    // scope 非法。
    QJsonObject badScope = json;
    badScope.insert(QStringLiteral("scope"), QStringLiteral("root"));
    QVERIFY(!recordFromJson(badScope, &record, &error));

    // createdAt 非法。
    QJsonObject badCreated = json;
    badCreated.insert(QStringLiteral("createdAt"), QStringLiteral("not-a-date"));
    QVERIFY(!recordFromJson(badCreated, &record, &error));

    // 指纹长度非法。
    QJsonObject badFp = json;
    badFp.insert(QStringLiteral("execStartSha256"), QStringLiteral("abc"));
    QVERIFY(!recordFromJson(badFp, &record, &error));
}

void TestServiceOwnership::fingerprintMismatchDetected() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());

    // 未记录时为 NotRecorded。
    QCOMPARE(store.checkConsistency(QStringLiteral("dsh-web.service"),
                                    ServiceScope::System, kSystemExec),
             ConsistencyResult::NotRecorded);

    store.record(QStringLiteral("dsh-web.service"), ServiceScope::System,
                 kSystemExec);

    // 同样的 ExecStart → Match。
    QCOMPARE(store.checkConsistency(QStringLiteral("dsh-web.service"),
                                    ServiceScope::System, kSystemExec),
             ConsistencyResult::Match);

    // 其它 ExecStart → Mismatch。
    QCOMPARE(store.checkConsistency(QStringLiteral("dsh-web.service"),
                                    ServiceScope::System, kUserExec),
             ConsistencyResult::Mismatch);
}

void TestServiceOwnership::systemAndUserRecordsAreSeparate() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());

    store.record(QStringLiteral("dsh-web.service"), ServiceScope::System,
                 kSystemExec);
    store.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                 kUserExec);

    QCOMPARE(store.count(), 2);  // 同 unit 名，不同 scope → 两条记录。

    OwnedServiceRecord sys;
    OwnedServiceRecord usr;
    QVERIFY(store.find(QStringLiteral("dsh-web.service"),
                       ServiceScope::System, &sys));
    QVERIFY(store.find(QStringLiteral("dsh-web.service"),
                       ServiceScope::User, &usr));

    QCOMPARE(sys.scope, ServiceScope::System);
    QCOMPARE(usr.scope, ServiceScope::User);
    QCOMPARE(sys.execStartFingerprint,
             makeExecStartFingerprint(kSystemExec));
    QCOMPARE(usr.execStartFingerprint,
             makeExecStartFingerprint(kUserExec));

    // serialization 后再解析仍保持这两条分离记录。
    const QByteArray json = serializeRecords(store.records());
    QVector<OwnedServiceRecord> parsed;
    QVERIFY2(parseRecords(json, &parsed), "解析序列化结果应成功");
    QCOMPARE(parsed.size(), 2);
}

void TestServiceOwnership::recordIsIdempotentUpsert() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());

    store.record(QStringLiteral("dsh-web.service"), ServiceScope::System,
                 kSystemExec);
    QCOMPARE(store.count(), 1);
    OwnedServiceRecord firstRec;
    QVERIFY(store.find(QStringLiteral("dsh-web.service"),
                       ServiceScope::System, &firstRec));
    const QDateTime firstCreated = firstRec.createdAt;

    // 再记录同 unit+scope：仍是 1 条，且指纹刷新为新 ExecStart。
    store.record(QStringLiteral("dsh-web.service"), ServiceScope::System,
                 kUserExec);
    QCOMPARE(store.count(), 1);

    OwnedServiceRecord rec;
    QVERIFY(store.find(QStringLiteral("dsh-web.service"),
                       ServiceScope::System, &rec));
    QCOMPARE(rec.execStartFingerprint,
             makeExecStartFingerprint(kUserExec));
    QVERIFY(rec.createdAt >= firstCreated);

    // 不同 unit 名则是新的记录。
    store.record(QStringLiteral("other.service"), ServiceScope::System,
                 kSystemExec);
    QCOMPARE(store.count(), 2);
}

void TestServiceOwnership::lookupByUnitAndScope() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());

    store.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                 kUserExec);

    QVERIFY(store.contains(QStringLiteral("dsh-web.service"), ServiceScope::User));
    QVERIFY(!store.contains(QStringLiteral("dsh-web.service"),
                            ServiceScope::System));
    QVERIFY(!store.contains(QStringLiteral("missing.service"), ServiceScope::User));

    OwnedServiceRecord rec;
    QVERIFY(store.find(QStringLiteral("dsh-web.service"), ServiceScope::User, &rec));
    // find 对不存在的组合返回 false 且不改动 out 参数。
    OwnedServiceRecord probe;
    probe.unitName = QStringLiteral("sentinel");
    QVERIFY(!store.find(QStringLiteral("nope.service"), ServiceScope::System, &probe));
    QCOMPARE(probe.unitName, QStringLiteral("sentinel"));
}

void TestServiceOwnership::missingStateFileLoadsEmpty() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());
    // 文件不存在 → load 成功，空记录。
    QVERIFY2(store.load(), "缺失文件应视为空状态");
    QCOMPARE(store.count(), 0);
}

void TestServiceOwnership::consistencyNotRecorded() {
    TempOwnership temp;
    ServiceOwnership store(temp.filePath());
    QCOMPARE(store.checkConsistency(QStringLiteral("never.service"),
                                    ServiceScope::System, kSystemExec),
             ConsistencyResult::NotRecorded);
}

void TestServiceOwnership::defaultPathUnderStateLocation() {
    const QString path = ServiceOwnership::defaultStateFilePath();
    QVERIFY(!path.isEmpty());
    QVERIFY(path.endsWith(QStringLiteral("/services-owned.json")));

    // 默认路径应位于 StateLocation 或 AppLocalDataLocation 之下。
    const QString state =
        QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const bool underKnownRoot =
        (!state.isEmpty() && (path.startsWith(state)))
        || (!data.isEmpty() && (path.startsWith(data)));
    QVERIFY2(underKnownRoot, qPrintable(path));
}

QTEST_GUILESS_MAIN(TestServiceOwnership)
#include "test_service_ownership.moc"
