// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 自建 systemd service 单元的所有权记录与一致性校验。
//
// 本模块记录"由 DSH Desktop 补齐/启动的 systemd service 单元"的元数据
// （unit 名、范围、创建时间与 ExecStart 指纹），并把记录中的指纹与当前
// 实测的 ExecStart 做一致性比对，用来发现用户或其它工具改动 unit 后的
// 漂移（例如重写了 ExecStart 或改用别的命令）。
//
// 它只读写一个本地 JSON 状态文件（优先位于 QStandardPaths 的
// StateLocation，其次 AppLocalDataLocation），绝不调用 systemctl，
// 绝不 start / stop / enable / disable，绝不删除任何单元。它只做记录与
// 比对，不改变任何被观测单元的运行时状态。
//
// 序列化 / 反序列化均为纯函数，不接触磁盘，便于单元测试。

#pragma once

#include "ServiceInfo.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsh::service {

/// 一致性比对结果。
enum class ConsistencyResult {
    NotRecorded,  // 没有该 unit+scope 的所有权记录。
    Match,        // 当前 ExecStart 指纹与记录一致。
    Mismatch,     // 当前 ExecStart 指纹与记录不一致（单元被改动过）。
};

/// 一条自建 unit 的所有权记录。纯数据，不含任何进程 / 磁盘 IO。
struct OwnedServiceRecord {
    QString unitName;
    ServiceScope scope{ServiceScope::System};
    QDateTime createdAt;           // 创建时间，统一保存为 UTC。
    QString execStartFingerprint;  // ExecStart 的 SHA-256 小写十六进制指纹。

    bool operator==(const OwnedServiceRecord& other) const {
        return unitName == other.unitName
            && scope == other.scope
            && createdAt == other.createdAt
            && execStartFingerprint == other.execStartFingerprint;
    }
    bool operator!=(const OwnedServiceRecord& other) const { return !(*this == other); }
};

// ---------------------------------------------------------------------------
// 纯函数：scope 字符串化、ExecStart 指纹、序列化/反序列化。不接触磁盘。
// ---------------------------------------------------------------------------

/// 把 ``ServiceScope`` 序列化为稳定的小写字符串（``"system"`` / ``"user"``）。
QString scopeToString(ServiceScope scope);

/// 反向解析 ``ServiceScope``；输入无法识别时返回 false（\p scope 保持不变）。
bool scopeFromString(const QString& text, ServiceScope* scope);

/// 计算 ExecStart 的稳定指纹：UTF-8 的 SHA-256，小写十六进制，64 字符。
QString makeExecStartFingerprint(const QString& execStart);

/// 两条原始 ExecStart 的指纹是否一致（各自先做指纹再比较）。
bool execStartFingerprintsEqual(const QString& execStartA, const QString& execStartB);

/// 单条记录 -> JSON 对象（字段名为 ``unit`` / ``scope`` / ``createdAt`` /
/// ``execStartSha256``）。
QJsonObject recordToJson(const OwnedServiceRecord& record);

/// JSON 对象 -> 记录。字段缺失、类型错误或取值非法时返回 false，
/// 并把原因写入 \p error（\p error 可为空）。
bool recordFromJson(const QJsonObject& object,
                    OwnedServiceRecord* record,
                    QString* error = nullptr);

/// 记录集合 -> 完整 JSON 文档字节。顶层为包含 ``version`` 与
/// ``services`` 数组的对象。
QByteArray serializeRecords(const QVector<OwnedServiceRecord>& records);

/// JSON 文档字节 -> 记录集合。文档非法（语法错误、非对象、版本不符、
/// 字段非法）时返回 false、清空 \p records 并把原因写入 \p error。
bool parseRecords(const QByteArray& json,
                  QVector<OwnedServiceRecord>* records,
                  QString* error = nullptr);

/// DSH Desktop 自建 unit 的所有权记录 + 一致性校验。
///
/// 非线程安全；默认在 UI 线程使用。状态文件路径可显式指定（测试用临时
/// 文件），缺省时使用 defaultStateFilePath()。
class ServiceOwnership {
public:
    /// 用显式路径构造（测试传临时路径）；缺省时用 defaultStateFilePath()。
    explicit ServiceOwnership(const QString& stateFilePath = defaultStateFilePath());

    /// 默认状态文件路径：优先 ``StateLocation``，其次
    /// ``AppLocalDataLocation``，在末尾追加 ``services-owned.json``。
    static QString defaultStateFilePath();

    QString stateFilePath() const { return stateFilePath_; }

    // -----------------------------------------------------------------------
    // 记录管理（内存中）
    // -----------------------------------------------------------------------

    /// 记录一条自建 unit：按 unit+scope 幂等 upsert，指纹由 ExecStart 计算。
    /// 对已存在的 unit+scope 重新记录会刷新 createdAt 与指纹。
    void record(const QString& unitName, ServiceScope scope, const QString& execStart);

    /// 按 unit+scope 查找；命中返回 true 并填充 \p out（\p out 可为空）。
    bool find(const QString& unitName, ServiceScope scope,
              OwnedServiceRecord* out) const;

    /// 是否存在该 unit+scope 的所有权记录。
    bool contains(const QString& unitName, ServiceScope scope) const;

    /// 当前全部记录（拷贝）。
    QVector<OwnedServiceRecord> records() const { return records_; }

    /// 当前记录条数。
    int count() const { return records_.size(); }

    // -----------------------------------------------------------------------
    // 持久化
    // -----------------------------------------------------------------------

    /// 从状态文件加载。文件缺失视为"从未记录"（成功，清空内存）；文件存在
    /// 但 JSON 非法或字段不合法时返回 false 且保持内存不变。
    bool load();

    /// 原子保存到状态文件（QSaveFile）。编码任何一步失败时不改动目标文件。
    bool save() const;

    // -----------------------------------------------------------------------
    // 一致性
    // -----------------------------------------------------------------------

    /// 核对某 unit+scope 的当前 ExecStart 与记录中的指纹。未记录时返回
    /// ``NotRecorded``；指纹相同返回 ``Match``；否则 ``Mismatch``。
    ConsistencyResult checkConsistency(const QString& unitName,
                                       ServiceScope scope,
                                       const QString& currentExecStart) const;

private:
    QString stateFilePath_;
    QVector<OwnedServiceRecord> records_;
};

}  // namespace dsh::service
