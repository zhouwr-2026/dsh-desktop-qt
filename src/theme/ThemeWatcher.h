// SPDX-License-Identifier: MIT
// @author zhouwr
// KDE Plasma 6 色彩方案监听器。
//
// 检测路径（按优先级）：
//   1. KDE 配置文件，以及跨用户 KDE 会话导出的运行态主题标记。
//   2. ``QStyleHints::colorScheme()``。
//   3. ``org.freedesktop.portal.Settings`` D-Bus 接口。
//   4. QPalette 与上次结果兜底。
//
// 检测到变化时发出 ``schemeChanged(QString)`` 信号，值在 ``"light"`` 与
// ``"dark"`` 之间切换。

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace dsh::theme {

class ThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit ThemeWatcher(QObject* parent = nullptr);
    ~ThemeWatcher() override;

    /// 启动轮询；可重复调用。
    void start();
    void stop();
    void refresh();
    void setForcedScheme(const QString& scheme);

    /// \return 当前主题，"light" 或 "dark"。
    QString current() const { return current_; }

    /// 从 KDE 配置文件解析主题。LookAndFeelPackage 的优先级高于
    /// ColorScheme，与文件排列顺序无关。
    static QString detectKdeTheme(const QStringList& paths);

signals:
    void schemeChanged(const QString& scheme);

private slots:
    void onTick();

private:
    QString probe();
    QString readKdeConfig() const;
    QString readPortal() const;

    QTimer timer_;
    QString current_{QStringLiteral("light")};
    QString forced_;
};

}  // namespace dsh::theme
