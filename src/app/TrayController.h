// SPDX-License-Identifier: MIT
// @author zhouwr
//
// KDE Plasma 6 托盘图标 + 右键菜单。
//
// 持有一个 ``QSystemTrayIcon``（即 KDE 的 StatusNotifierItem）、一个
// ``QMenu`` 与六个菜单项：显示桌面 / 隐藏桌面 / 检查更新 / 更新到最新版 /
// 重启桌面 / 退出。其中"更新到最新版"默认隐藏，仅在 ``check()`` 发现新
// 版本后才显示。

#pragma once

#include <QObject>

QT_BEGIN_NAMESPACE
class QSystemTrayIcon;
class QAction;
class QMenu;
class QIcon;
QT_END_NAMESPACE

namespace dsh::app {

class DshWindow;

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(QObject* parent = nullptr);
    ~TrayController() override;

    /// 绑定菜单动作到窗口显隐与回调。
    void bind(DshWindow* window,
              std::function<void()> onCheck,
              std::function<void()> onUpdate,
              std::function<void()> onRestart,
              std::function<void()> onQuit,
              std::function<void()> onAbout,
              std::function<void()> onLog,
              std::function<void()> onClear);

    /// 是否显示"更新到最新版"菜单项。
    void setUpdateAvailable(bool available);

    /// 显示 / 隐藏托盘图标。
    void show();
    void hide();
    bool isVisible() const;

    /// 应用统一入口生成的 logo。
    void applyLogo(const QIcon& icon, const QString& scheme);

    /// 给每个菜单项设置 KDE 主题的语义图标（每个不同）。
    void setMenuIcons();

    QString appliedTheme() const { return appliedTheme_; }
    QString toolTip() const;

signals:
    void triggered();  // 托盘被单击/双击（用于切换窗口显隐）

public:
    /// 只读访问菜单项列表（供自检报告使用）。
    QList<QAction*> menuActions() const;

private:
    QSystemTrayIcon* tray_{nullptr};
    QMenu* menu_{nullptr};
    QAction* showAct_{nullptr};
    QAction* hideAct_{nullptr};
    QAction* checkAct_{nullptr};
    QAction* updateAct_{nullptr};
    QAction* restartAct_{nullptr};
    QAction* logAct_{nullptr};
    QAction* clearAct_{nullptr};
    QAction* aboutAct_{nullptr};
    QAction* quitAct_{nullptr};
    QString appliedTheme_{QStringLiteral("light")};
};

}  // namespace dsh::app
