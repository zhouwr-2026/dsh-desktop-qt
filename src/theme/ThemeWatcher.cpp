// SPDX-License-Identifier: MIT
// @author zhouwr
#include "ThemeWatcher.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

#include <unistd.h>
#include <algorithm>

namespace dsh::theme {

namespace {
// 轮询周期 4 秒：足够快地响应手动切换，又不会浪费 CPU。
constexpr int kPollMs = 4000;

// KDE 主题字符串里出现 "light"（但不含 dark）视作亮色。
bool isLightString(const QString& s) {
    const QString l = s.toLower();
    return l.contains("light") || (l.contains("breeze") && !l.contains("dark"));
}

bool isDarkString(const QString& s) {
    return s.toLower().contains("dark");
}
}  // namespace

ThemeWatcher::ThemeWatcher(QObject* parent) : QObject(parent) {
    timer_.setInterval(kPollMs);
    timer_.setSingleShot(false);
    connect(&timer_, &QTimer::timeout, this, &ThemeWatcher::onTick);
}

ThemeWatcher::~ThemeWatcher() = default;

void ThemeWatcher::start() {
    refresh();
    timer_.start();
}

void ThemeWatcher::stop() {
    timer_.stop();
}

void ThemeWatcher::refresh() {
    onTick();
}

void ThemeWatcher::setForcedScheme(const QString& scheme) {
    forced_ = scheme;
    if (!forced_.isEmpty() && current_ != forced_) {
        current_ = forced_;
        emit schemeChanged(current_);
    }
}

void ThemeWatcher::onTick() {
    const QString next = probe();
    if (next != current_) {
        current_ = next;
        emit schemeChanged(current_);
    }
}

QString ThemeWatcher::probe() {
    if (!forced_.isEmpty()) return forced_;
    // 1. KDE 配置是本项目目标环境中唯一稳定的真值来源。xrdp 下桌面
    // 会话可能属于 root，而应用属于普通用户，此时读取 root 侧导出的
    // /run/dsh-desktop/theme 标记。
    const QString kde = readKdeConfig();
    if (kde == "dark" || kde == "light") return kde;
    // 2. Qt 风格提示。
    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const Qt::ColorScheme scheme = app->styleHints()->colorScheme();
        switch (scheme) {
            case Qt::ColorScheme::Dark: return QStringLiteral("dark");
            case Qt::ColorScheme::Light: return QStringLiteral("light");
            default: break;
        }
    }
    // 3. portal 设置。
    const QString portal = readPortal();
    if (portal == "dark" || portal == "light") return portal;
    // 4. QPalette 亮度兜底。
    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const QColor win = app->palette().color(QPalette::Window);
        const double lum = 0.2126 * win.redF() + 0.7152 * win.greenF() + 0.0722 * win.blueF();
        if (lum < 0.5) return QStringLiteral("dark");
        if (lum > 0.5) return QStringLiteral("light");
    }
    return current_;
}

QString ThemeWatcher::detectKdeTheme(const QStringList& paths) {
    const auto findValue = [&paths](const QStringList& keys) {
        for (const QString& path : paths) {
            if (!QFile::exists(path)) continue;
            for (QSettings::Format format : {QSettings::NativeFormat,
                                              QSettings::IniFormat}) {
                QSettings settings(path, format);
                for (const QString& key : keys) {
                    const QString value = settings.value(key).toString().trimmed();
                    if (!value.isEmpty()) return value;
                }
            }
        }
        return QString{};
    };

    // KDE 6 的全局外观包是最高优先级；不能因为先遇到另一个文件中的
    // ColorScheme 就提前返回旧值。
    const QString lookAndFeel = findValue({
        QStringLiteral("KDE/LookAndFeelPackage"),
        QStringLiteral("LookAndFeelPackage"),
        QStringLiteral("General/LookAndFeelPackage"),
    });
    if (isDarkString(lookAndFeel)) return QStringLiteral("dark");
    if (isLightString(lookAndFeel)) return QStringLiteral("light");

    const QString colorScheme = findValue({
        QStringLiteral("General/ColorScheme"),
        QStringLiteral("ColorScheme"),
        QStringLiteral("KDE/ColorScheme"),
    });
    if (isDarkString(colorScheme)) return QStringLiteral("dark");
    if (isLightString(colorScheme)) return QStringLiteral("light");
    return {};
}

QString ThemeWatcher::readKdeConfig() const {
    // ``DSH_DESKTOP_THEME_FILE`` 是**部署方控制**的覆盖项（默认路径
    // ``/run/dsh-desktop/theme`` 由 ``dsh-theme-export.service`` 写入）。
    // 本函数只读取并严格匹配 ``dark`` / ``light`` 字面量，绝不传播文件内容
    // 到其它通道（参见安全审查 L-3）。
    const QString markerPath = qEnvironmentVariable(
        "DSH_DESKTOP_THEME_FILE", QStringLiteral("/run/dsh-desktop/theme"));
    QFile marker(markerPath);
    if (marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString value = QString::fromUtf8(marker.readAll()).trimmed();
        if (value == QStringLiteral("dark") || value == QStringLiteral("light")) {
            return value;
        }
    }

    QStringList paths = {
        QDir::homePath() + QStringLiteral("/.config/kdeglobals"),
        QDir::homePath() + QStringLiteral("/.config/plasmarc"),
    };
    if (getuid() != 0) {
        paths << QStringLiteral("/root/.config/kdeglobals")
              << QStringLiteral("/root/.config/plasmarc");
    }
    return detectKdeTheme(paths);
}

QString ThemeWatcher::readPortal() const {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {};
    QDBusInterface iface("org.freedesktop.portal.Desktop",
                         "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Settings",
                         bus);
    if (!iface.isValid()) return {};
    QDBusReply<QVariant> reply = iface.call("Read",
                                            "org.freedesktop.appearance",
                                            "color-scheme");
    if (!reply.isValid()) return {};
    // FreeDesktop 规范：1 = dark, 2 = light。
    if (reply.value().toUInt() == 1) return QStringLiteral("dark");
    if (reply.value().toUInt() == 2) return QStringLiteral("light");
    return {};
}

}  // namespace dsh::theme
