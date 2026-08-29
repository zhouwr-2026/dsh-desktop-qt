// SPDX-License-Identifier: MIT
// @author zhouwr
//
// systemd ``systemctl show`` 失败时的统一错误分类。
//
// 单一权威实现：把 stderr 文本与 unitName 一起映射为 ``RejectionReason`` +
// 人类可读的 detail。``runSystemctlShow``（ServiceDiscovery）与
// ``DshServiceManager::handleDiscoveryFinished`` 之前各有自己的同质实现，
// 易出现"一处改了另一处忘改"的字符串匹配漂移；本工具消除该重复。
//
// 纯函数，无状态、无运行时依赖（只依赖 Qt::QString 与 RejectionReason）。
//
// (变更理由: 结构审查 #4, classifyShowFailure / runSystemctlShow 重复)

#pragma once

#include "ServiceDiscovery.h"  // RejectionReason

#include <QString>

namespace dsh::service {

/// 把 ``systemctl show`` 的 stderr 文本分类为 ``RejectionReason``，并生成
/// 对应的 ``detail`` 字符串。
///
/// 分类规则（与历史行为一致）：
///   * 包含 ``"could not be found"`` 或 ``"No such file or directory"`` →
///     ``UnitNotFound``，detail = "<unitName> 在该范围未发现"；
///   * 包含 ``"bus"``（不区分大小写）或 ``"connect"``（不区分大小写）→
///     ``BusUnavailable``，detail = stderr 或 "systemd 总线不可用"；
///   * 其它 → ``ShowFailed``，detail = stderr 或 "systemctl show 失败"；
///   * stderr 为空时 detail 退化为 reason 字符串。
///
/// \param stderrText 来自 ``systemctl show`` 的 stderr（可包含前后空白）。
/// \param unitName   用于 ``UnitNotFound`` 的可读 detail。
/// \param reason     出参：分类结果。
/// \param detail     出参：人类可读的诊断信息。
void classifyShowFailure(const QString& stderrText, const QString& unitName,
                         RejectionReason& reason, QString& detail);

}  // namespace dsh::service
