// SPDX-License-Identifier: MIT
// @author zhouwr
// KDE 通知中心集成。
//
// 通过 ``org.freedesktop.Notifications`` D-Bus 接口发送系统通知，这是
// KDE Plasma 6（以及 GNOME / XFCE 等）共同遵循的规范。在无 D-Bus 的环境
// （CI、ssh 未转发会话总线等）所有调用都返回 0 而不崩溃。

#pragma once

#include <QString>

namespace dsh::util {

/// 显示一条桌面通知。
///
/// \param summary  短标题（粗体）。
/// \param body     正文。
/// \param urgency  "low" / "normal" / "critical"。
/// \param icon     图标名（如 "dialog-information"）。
/// \param timeoutMs 自动消失时间，0 表示交给服务端决定。
/// \return 通知 id（>0），失败返回 0。
int notify(const QString& summary,
           const QString& body,
           const QString& urgency = QStringLiteral("normal"),
           const QString& icon = QStringLiteral("dialog-information"),
           int timeoutMs = 5000);

/// 按 id 关闭一条已经显示的通知（id<=0 时为空操作）。
void closeNotification(int id);

}  // namespace dsh::util