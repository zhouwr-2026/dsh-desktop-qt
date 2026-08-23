// SPDX-License-Identifier: MIT
// @author zhouwr

#include "ServiceOwnership.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace dsh::service {

namespace {

constexpr int kStateFileVersion = 1;
constexpr int kSha256HexLength = 64;

/// 判别字符串是否为合法的 SHA-256 小写/大写十六进制（长度为 64）。
bool isSha256Hex(const QString& text) {
    if (text.size() != kSha256HexLength) return false;
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        const bool hex = (code >= '0' && code <= '9')
            || (code >= 'a' && code <= 'f')
            || (code >= 'A' && code <= 'F');
        if (!hex) return false;
    }
    return true;
}

}  // namespace

QString scopeToString(ServiceScope scope) {
    switch (scope) {
        case ServiceScope::User:
            return QStringLiteral("user");
        case ServiceScope::System:
            return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

bool scopeFromString(const QString& text, ServiceScope* scope) {
    if (!scope) return false;
    if (text == QLatin1String("system")) {
        *scope = ServiceScope::System;
        return true;
    }
    if (text == QLatin1String("user")) {
        *scope = ServiceScope::User;
        return true;
    }
    return false;
}

ServiceOwnership::ServiceOwnership(const QString& stateFilePath)
    : stateFilePath_(stateFilePath) {}

QString ServiceOwnership::defaultStateFilePath() {
    // 优先使用 StateLocation（应用私有状态，非用户文档），其次
    // 退化到 AppLocalDataLocation，最终追加固定的文件名。
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (base.isEmpty()) {
        base =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    return base + QStringLiteral("/services-owned.json");
}

void ServiceOwnership::record(const QString& unitName, ServiceScope scope,
                              const QString& execStart) {
    OwnedServiceRecord rec;
    rec.unitName = unitName;
    rec.scope = scope;
    rec.createdAt = QDateTime::currentDateTimeUtc();
    rec.execStartFingerprint = makeExecStartFingerprint(execStart);

    for (int i = 0; i < records_.size(); ++i) {
        if (records_.at(i).unitName == unitName && records_.at(i).scope == scope) {
            records_[i] = rec;  // 幂等 upsert：刷新指纹与创建时间。
            return;
        }
    }
    records_.append(rec);
}

bool ServiceOwnership::find(const QString& unitName, ServiceScope scope,
                            OwnedServiceRecord* out) const {
    for (const OwnedServiceRecord& rec : records_) {
        if (rec.unitName == unitName && rec.scope == scope) {
            if (out) *out = rec;
            return true;
        }
    }
    return false;
}

bool ServiceOwnership::contains(const QString& unitName, ServiceScope scope) const {
    return find(unitName, scope, nullptr);
}

bool ServiceOwnership::load() {
    QFile file(stateFilePath_);
    if (!file.exists()) {
        // 从未记录过任何自建单元：空状态，不算错误。
        records_.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QVector<OwnedServiceRecord> loaded;
    if (!parseRecords(data, &loaded)) {
        return false;  // 保持内存不变。
    }
    records_ = loaded;
    return true;
}

bool ServiceOwnership::save() const {
    const QString dir = QFileInfo(stateFilePath_).absolutePath();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        return false;
    }

    const QByteArray json = serializeRecords(records_);

    QSaveFile file(stateFilePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(json) < 0 || !file.commit()) {
        file.cancelWriting();
        return false;
    }
    return true;
}

ConsistencyResult ServiceOwnership::checkConsistency(
    const QString& unitName, ServiceScope scope, const QString& currentExecStart) const {
    OwnedServiceRecord rec;
    if (!find(unitName, scope, &rec)) {
        return ConsistencyResult::NotRecorded;
    }
    if (rec.execStartFingerprint == makeExecStartFingerprint(currentExecStart)) {
        return ConsistencyResult::Match;
    }
    return ConsistencyResult::Mismatch;
}

QString makeExecStartFingerprint(const QString& execStart) {
    const QByteArray data = execStart.toUtf8();
    const QByteArray sum =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return QString::fromLatin1(sum.toHex());  // toHex() 默认小写。
}

bool execStartFingerprintsEqual(const QString& execStartA,
                                const QString& execStartB) {
    return makeExecStartFingerprint(execStartA) == makeExecStartFingerprint(execStartB);
}

QJsonObject recordToJson(const OwnedServiceRecord& record) {
    QJsonObject object;
    object.insert(QStringLiteral("unit"), record.unitName);
    object.insert(QStringLiteral("scope"), scopeToString(record.scope));
    object.insert(QStringLiteral("createdAt"),
                  record.createdAt.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("execStartSha256"), record.execStartFingerprint);
    return object;
}

bool recordFromJson(const QJsonObject& object,
                    OwnedServiceRecord* record,
                    QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!record) return fail(QStringLiteral("null record pointer"));

    const QJsonValue unitJson = object.value(QStringLiteral("unit"));
    if (!unitJson.isString() || unitJson.toString().isEmpty()) {
        return fail(QStringLiteral("missing or empty 'unit'"));
    }

    const QJsonValue scopeJson = object.value(QStringLiteral("scope"));
    ServiceScope scope;
    if (!scopeJson.isString() ||
        !scopeFromString(scopeJson.toString(), &scope)) {
        return fail(QStringLiteral("invalid 'scope'"));
    }

    const QJsonValue createdJson = object.value(QStringLiteral("createdAt"));
    if (!createdJson.isString()) {
        return fail(QStringLiteral("invalid 'createdAt'"));
    }
    QDateTime created =
        QDateTime::fromString(createdJson.toString(), Qt::ISODateWithMs);
    if (!created.isValid()) {
        created = QDateTime::fromString(createdJson.toString(), Qt::ISODate);
    }
    if (!created.isValid()) {
        return fail(QStringLiteral("invalid 'createdAt'"));
    }

    const QJsonValue fpJson = object.value(QStringLiteral("execStartSha256"));
    const QString fingerprint = fpJson.toString();
    if (!fpJson.isString() || !isSha256Hex(fingerprint)) {
        return fail(QStringLiteral("invalid 'execStartSha256'"));
    }

    OwnedServiceRecord parsed;
    parsed.unitName = unitJson.toString();
    parsed.scope = scope;
    parsed.createdAt = created;
    parsed.execStartFingerprint = fingerprint.toLower();
    *record = parsed;
    return true;
}

QByteArray serializeRecords(const QVector<OwnedServiceRecord>& records) {
    QJsonArray array;
    for (const OwnedServiceRecord& record : records) {
        array.append(recordToJson(record));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kStateFileVersion);
    root.insert(QStringLiteral("services"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool parseRecords(const QByteArray& json,
                  QVector<OwnedServiceRecord>* records,
                  QString* error) {
    auto fail = [records, error](const QString& message) {
        if (records) records->clear();
        if (error) *error = message;
        return false;
    };
    if (!records) return fail(QStringLiteral("null records pointer"));

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("malformed JSON: %1")
                        .arg(parseError.errorString()));
    }
    if (!doc.isObject()) {
        return fail(QStringLiteral("top-level JSON is not an object"));
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != kStateFileVersion) {
        return fail(QStringLiteral("unsupported state file version"));
    }

    const QJsonValue servicesJson = root.value(QStringLiteral("services"));
    if (!servicesJson.isArray()) {
        return fail(QStringLiteral("missing or non-array 'services'"));
    }

    QVector<OwnedServiceRecord> parsed;
    const QJsonArray array = servicesJson.toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            return fail(QStringLiteral("services entry is not an object"));
        }
        OwnedServiceRecord record;
        QString recordError;
        if (!recordFromJson(value.toObject(), &record, &recordError)) {
            return fail(recordError);
        }
        parsed.append(record);
    }

    *records = parsed;
    if (error) error->clear();
    return true;
}

}  // namespace dsh::service
