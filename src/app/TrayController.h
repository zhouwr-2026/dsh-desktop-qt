// SPDX-License-Identifier: MIT
// @author zhouwr
//
// KDE Plasma 6 托盘图标 + 右键菜单。
//
// 持有一个 ``QSystemTrayIcon``（即 KDE 的 StatusNotifierItem）、一个
// ``QMenu`` 与这些菜单项：
//
//   * 显示桌面 / 隐藏桌面
//   * 检查更新 / 更新到最新版（默认隐藏，仅在 ``check()`` 发现新版本后显示）
//   * 查看日志 / 清空下载缓存 / 关于
//   * 重启（先弹原生 RestartDialog，按勾选决定是否重启/启动/离开后台服务）
//   * 退出（先弹原生 ExitDialog，按勾选决定是否停止/启动/离开后台服务）
//
// 后台服务的启动 / 停止 / 重启 **不再** 暴露在托盘菜单上：用户通过「重启」
// 或「退出」对话框里的"是否启动/重启/停止后台服务"复选框来表达意图，避免
// 在顶级菜单上重复一组生命周期动作。
//
// "重启" 与 "退出" 之间用分隔符隔开，明确区分桌面端与后端的两类生命周期
// 操作（后者已并入对话框）。

#pragma once

#include <QObject>

#include <functional>

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
    ///
    /// \param onCheck          检查更新
    /// \param onUpdate         更新到最新版
    /// \param onRestartDesktop 托盘"重启"点击（实为 DshDesktopApp 端的
    ///                         ``onRestartWithDialog``：先弹 RestartDialog，
    ///                         再按勾选决定是否重启/启动/离开后台服务）
    /// \param onQuit           退出（先弹 ExitDialog，再按勾选决定是否停止
    ///                         /启动/离开后台服务）
    /// \param onAbout          关于
    /// \param onLog            查看日志
    /// \param onClear          清空下载缓存
    void bind(DshWindow* window,
              std::function<void()> onCheck,
              std::function<void()> onUpdate,
              std::function<void()> onRestartDesktop,
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
