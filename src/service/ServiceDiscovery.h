// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 只读的 dsh-web.service 实况发现。
//
// 本模块在 ``systemctl show`` 的输出之上新增一层"验证 + 选择"：对系统域
// 与当前用户域各执行一次只读 ``systemctl show``，解析为 ServiceInfo，
// 再校验 ``LoadState=loaded`` 且 ExecStart 调用官方 ``dsh web``，最后按
// 文档化偏好选出唯一选中项。
//
// 本模块绝不执行任何会改变系统状态的操作：不 start / stop / enable /
// disable，不写文件。systemctl 缺失或系统/用户总线不可用时只记录原因，
// 不抛异常、不崩溃。
//
// 解析逻辑见 SystemctlShowParser；本模块复用其解析结果。

#pragma once

#include "ServiceInfo.h"

#include <QString>
#include <QVector>

namespace dsh::service {

/// 某个候选服务在发现阶段被拒绝（或通过）的原因。
enum class RejectionReason {
    None,               // 校验通过，是有效候选。
    UnitNotFound,       // ``systemctl show`` 报 unit 在该范围不存在。
    LoadStateNotLoaded, // ``LoadState`` 不是 ``loaded``（陈旧/未加载）。
    ForeignExecStart,   // ``ExecStart`` 不调用官方 ``dsh web``。
    SystemctlMissing,   // 找不到 ``systemctl`` 可执行文件。
    BusUnavailable,     // systemd 系统/用户总线不可用（连接失败等）。
    ShowFailed,         // ``systemctl show`` 返回非零退出码（其它原因）。
    ProcessError,       // 进程无法启动。
    Timeout,            // 进程执行超时。
};

/// 一个候选（一次 ``systemctl show``）的发现结果。
struct DiscoveredService {
    ServiceInfo info;                    // scope / unitName 已由发现层填好。
    bool valid{false};                   // 是否通过 LoadState + 官方入口校验。
    RejectionReason rejection{RejectionReason::None};
    QString rejectionDetail;             // 人类可读的拒绝原因。
};

/// 简明识别结果：把选中候选的 unit 名、范围与解析到的实际监听 endpoint
/// 一起传递出去，供上层（如 SystemdBackend）直接使用，避免重新从文件系统
/// 推断 scope 或丢掉已解析的 host/port。
///
/// ``host``/``port`` 已在解析层应用过官方默认回退，因此只要 ``valid`` 为真
/// 就总是可以安全构造 URL。未选中任何有效候选时 ``valid=false``。
struct DetectedService {
    QString unitName;
    ServiceScope scope{ServiceScope::System};
    QString host{kDefaultHost};
    int port{kDefaultPort};
    bool valid{false};
};

/// 一次完整发现的只读结果。
struct DiscoveryResult {
    QVector<DiscoveredService> candidates;  // 固定顺序：[系统域, 用户域]。
    int selectedIndex{-1};                  // 选中候选下标；-1 表示没有有效候选。
    bool systemctlAvailable{true};          // 是否找到 systemctl。

    /// 选中候选；无选中时返回 nullptr。
    const DiscoveredService* selected() const;

    /// 返回选中候选的简明识别结果（unitName / scope / host / port）。
    /// 无选中或选中候选无效时返回 ``valid=false`` 的默认值。
    DetectedService detected() const;
};

/// 校验单个候选：``LoadState=loaded`` 且 ``invokesOfficialDshWeb``。
///
/// 纯函数，不执行进程。``info.invokesOfficialDshWeb`` 由 parseSystemctlShow
/// 填充；调用方需先解析再调用本函数。
DiscoveredService validateCandidate(const ServiceInfo& info);

/// 纯选择逻辑（无 systemctl）：从候选列表中选出唯一选中项下标。
///
/// 文档化、确定性的选择偏好：
///   1. 系统域与用户域候选都有效时，优先当前用户的用户级服务
///      （让 DSH 以普通用户运行，避免 root，见方案 §2.3）；
///   2. 只有单个有效候选时选中它；
///   3. 均无效时返回 -1（不选中）。
///
/// 返回 candidates 的下标，不在 candidates 中时返回 -1。
int selectCandidateIndex(const QVector<DiscoveredService>& candidates);

/// 执行一次只读实况发现并返回结构化结果。
///
/// 对系统域运行 ``systemctl --no-pager show <unit>``，对当前用户域运行
/// ``systemctl --user --no-pager show <unit>``；随后解析、校验并选择。
/// 不执行任何会改变系统状态的操作。
DiscoveryResult discoverDshWebService(
    const QString& unitName = QStringLiteral("dsh-web.service"));

}  // namespace dsh::service
