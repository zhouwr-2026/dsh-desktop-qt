// SPDX-License-Identifier: MIT
// @author zhouwr
// KDE 通知中心实现。

#include "Notify.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QStringList>
#include <QVariantMap>

namespace dsh::util {

namespace {

// org.freedesktop.Notifications 是 freedesktop 规范定义的标准服务名，
// 必须保留英文常量以便 D-Bus 路由。
constexpr const char* kService = "org.freedesktop.Notifications";
constexpr const char* kPath = "/org/freedesktop/Notifications";
constexpr const char* kInterface = "org.freedesktop.Notifications";

QDBusInterface* interface() {
    static QDBusInterface* iface = nullptr;
    if (iface) return iface;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return nullptr;
    iface = new QDBusInterface(kService, kPath, kInterface, bus, nullptr);
    if (!iface->isValid()) {
        delete iface;
        iface = nullptr;
    }
    return iface;
}

QString appId() { return QStringLiteral("dsh.desktop"); }
QString appName() { return QStringLiteral("DSH Desktop"); }

}  // namespace

int notify(const QString& summary,
           const QString& body,
           const QString& urgency,
           const QString& icon,
           int timeoutMs) {
    QDBusInterface* iface = interface();
    if (!iface) return 0;

    // FreeDesktop 规范的 urgency 数值：0 = low, 1 = normal, 2 = critical。
    int u = 1;
    if (urgency == "low") u = 0;
    else if (urgency == "critical") u = 2;

    QVariantMap hints;
    // desktop-entry 提示让 Plasma 把通知归入"DSH Desktop"应用，避免被
    // 通用 "通知" 类别吞掉。
    hints.insert("desktop-entry", appId());
    hints.insert("urgency", QVariant::fromValue(static_cast<quint8>(u)));

    QDBusReply<uint> reply = iface->call(
        "Notify",
        appName(),                       // 应用名
        uint{0},                         // replaces_id
        icon,                            // 图标
        summary,                         // 标题
        body,                            // 正文
        QStringList{},                   // 动作按钮（无）
        hints,                           // hints
        qMax(1000, timeoutMs)            // 自动消失时间（毫秒，至少 1 秒）
    );
    if (!reply.isValid()) return 0;
    return static_cast<int>(reply.value());
}

void closeNotification(int id) {
    if (id <= 0) return;
    QDBusInterface* iface = interface();
    if (!iface) return;
    iface->call("CloseNotification", uint{static_cast<unsigned>(id)});
}

}  // namespace dsh::util