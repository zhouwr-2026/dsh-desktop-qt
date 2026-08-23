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
//   * 重启 DSH Desktop（重新拉起当前可执行文件并退出，不影响后台服务）
//   * DSH 后台服务 分组：启动后台服务 / 重启后台服务 / 停止后台服务
//   * 查看日志 / 清空下载缓存 / 关于
//   * 退出
//
// "重启 DSH Desktop" 与 "DSH 后台服务" 分组之间用分隔符隔开，明确区分
// "重启桌面应用" 与 "管理后台 dsh web 服务" 两种不同的生命周期操作。

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
    /// \param onRestartDesktop 重启 DSH Desktop（重新拉起应用并退出）
    /// \param onStartBackend   启动后台服务
    /// \param onRestartBackend 重启后台服务
    /// \param onStopBackend    停止后台服务
    /// \param onQuit           退出
    /// \param onAbout          关于
    /// \param onLog            查看日志
    /// \param onClear          清空下载缓存
    void bind(DshWindow* window,
              std::function<void()> onCheck,
              std::function<void()> onUpdate,
              std::function<void()> onRestartDesktop,
              std::function<void()> onStartBackend,
              std::function<void()> onRestartBackend,
              std::function<void()> onStopBackend,
              std::function<void()> onQuit,
              std::function<void()> onAbout,
              std::function<void()> onLog,
              std::function<void()> onClear);

    /// 是否显示"更新到最新版"菜单项。
    void setUpdateAvailable(bool available);

    /// 更新"DSH 后台服务"分组的可管理状态。
    ///
    /// \param manageable 桌面端能否管理后台服务生命周期（External / 标记为
    ///                   ``Unmanaged`` 或不可管理时为 false，禁用整个分组）。
    /// \param running    后台服务当前是否在运行；用于按需启用"启动/停止/重启"。
    void setBackendManageable(bool manageable, bool running);

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
    QAction* restartDesktopAct_{nullptr};
    QMenu* backendMenu_{nullptr};          // "DSH 后台服务" 分组（子菜单）
    QAction* backendSectionAct_{nullptr};  // 分组在 ``menu_`` 中的入口项
    QAction* startServiceAct_{nullptr};
    QAction* restartServiceAct_{nullptr};
    QAction* stopServiceAct_{nullptr};
    QAction* logAct_{nullptr};
    QAction* clearAct_{nullptr};
    QAction* aboutAct_{nullptr};
    QAction* quitAct_{nullptr};
    QString appliedTheme_{QStringLiteral("light")};
};

}  // namespace dsh::app
