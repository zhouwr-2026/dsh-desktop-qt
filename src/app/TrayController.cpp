// SPDX-License-Identifier: MIT
// @author zhouwr
#include "TrayController.h"

#include "DshWindow.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

#include <functional>

namespace dsh::app {

TrayController::TrayController(QObject* parent) : QObject(parent) {
    tray_ = new QSystemTrayIcon(this);
    tray_->setToolTip(QStringLiteral("DSH Desktop"));

    menu_ = new QMenu();  // 无 parent：tray_ 才是 context menu 的持有者

    showAct_    = menu_->addAction(tr("显示桌面"));
    hideAct_    = menu_->addAction(tr("隐藏桌面"));
    menu_->addSeparator();
    checkAct_   = menu_->addAction(tr("检查更新"));
    updateAct_  = menu_->addAction(tr("更新到最新版"));
    updateAct_->setVisible(false);  // 仅在检查到更新后显示
    menu_->addSeparator();
    // "重启 DSH Desktop"：重新拉起当前可执行文件并退出，不触碰后台服务。
    restartDesktopAct_ = menu_->addAction(tr("重启 DSH Desktop"));
    menu_->addSeparator();

    // "DSH 后台服务" 分组：管理后台 dsh web 服务的生命周期。
    backendMenu_ = menu_->addMenu(tr("DSH 后台服务"));
    backendSectionAct_ = backendMenu_->menuAction();
    startServiceAct_    = backendMenu_->addAction(tr("启动后台服务"));
    restartServiceAct_  = backendMenu_->addAction(tr("重启后台服务"));
    stopServiceAct_     = backendMenu_->addAction(tr("停止后台服务"));
    menu_->addSeparator();

    logAct_     = menu_->addAction(tr("查看日志"));
    clearAct_   = menu_->addAction(tr("清空下载缓存"));
    aboutAct_   = menu_->addAction(tr("关于"));
    menu_->addSeparator();
    quitAct_    = menu_->addAction(tr("退出"));

    // 每个菜单项用 KDE Breeze 主题的语义图标（QIcon::fromTheme 自动跟随
    // 系统亮暗主题），而不是全部复用同一个鲸鱼——视觉更专业、可读性更好。
    // 托盘图标本身才是小鲸鱼 logo，与窗口/任务栏保持统一。
    setMenuIcons();

    tray_->setContextMenu(menu_);

    // KDE Plasma 6 通常把左键单击映射为 ActivationReason::Trigger。
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    emit triggered();
                }
            });
}

TrayController::~TrayController() {
    if (tray_) tray_->hide();
}

void TrayController::bind(DshWindow* window,
                          std::function<void()> onCheck,
                          std::function<void()> onUpdate,
                          std::function<void()> onRestartDesktop,
                          std::function<void()> onStartBackend,
                          std::function<void()> onRestartBackend,
                          std::function<void()> onStopBackend,
                          std::function<void()> onQuit,
                          std::function<void()> onAbout,
                          std::function<void()> onLog,
                          std::function<void()> onClear) {
    connect(showAct_,    &QAction::triggered, this, [window]() { window->showAndRaise(); });
    connect(hideAct_,    &QAction::triggered, this, [window]() { window->hide(); });
    connect(checkAct_,   &QAction::triggered, this, [onCheck]()    { onCheck(); });
    connect(updateAct_,  &QAction::triggered, this, [onUpdate]()   { onUpdate(); });
    connect(restartDesktopAct_, &QAction::triggered, this,
            [onRestartDesktop]() { onRestartDesktop(); });
    connect(startServiceAct_,    &QAction::triggered, this,
            [onStartBackend]()   { onStartBackend(); });
    connect(restartServiceAct_,  &QAction::triggered, this,
            [onRestartBackend]() { onRestartBackend(); });
    connect(stopServiceAct_,     &QAction::triggered, this,
            [onStopBackend]()    { onStopBackend(); });
    connect(logAct_,     &QAction::triggered, this, [onLog]()      { onLog(); });
    connect(clearAct_,   &QAction::triggered, this, [onClear]()    { onClear(); });
    connect(aboutAct_,   &QAction::triggered, this, [onAbout]()    { onAbout(); });
    connect(quitAct_,    &QAction::triggered, this, [onQuit]()     { onQuit(); });
}

void TrayController::setUpdateAvailable(bool available) {
    updateAct_->setVisible(available);
}

void TrayController::setBackendManageable(bool manageable, bool running) {
    // 整个分组随可管理性禁用；分组内按运行状态按需启用具体动作。
    if (backendSectionAct_) backendSectionAct_->setEnabled(manageable);
    if (startServiceAct_)   startServiceAct_->setEnabled(manageable && !running);
    if (restartServiceAct_) restartServiceAct_->setEnabled(manageable && running);
    if (stopServiceAct_)    stopServiceAct_->setEnabled(manageable && running);
}

void TrayController::show() { tray_->show(); }
void TrayController::hide() { tray_->hide(); }
bool TrayController::isVisible() const { return tray_ && tray_->isVisible(); }
QString TrayController::toolTip() const {
    return tray_ ? tray_->toolTip() : QString();
}

QList<QAction*> TrayController::menuActions() const {
    return menu_ ? menu_->actions() : QList<QAction*>{};
}

void TrayController::applyLogo(const QIcon& icon, const QString& scheme) {
    appliedTheme_ = scheme;
    if (!icon.isNull()) tray_->setIcon(icon);
}

void TrayController::setMenuIcons() {
    // KDE Breeze 主题的 freedesktop 标准图标名。
    // 每个菜单项一个语义图标；QIcon::fromTheme 依赖 QIcon::setThemeName
    // 指向 breeze / breeze-dark（由 DshDesktopApp 按主题设置）。
    auto fromTheme = [](const QString& name) { return QIcon::fromTheme(name); };
    if (showAct_)    showAct_->setIcon(fromTheme(QStringLiteral("go-home")));
    if (hideAct_)    hideAct_->setIcon(fromTheme(QStringLiteral("arrow-down")));
    if (checkAct_)   checkAct_->setIcon(fromTheme(QStringLiteral("system-software-update")));
    if (updateAct_)  updateAct_->setIcon(fromTheme(QStringLiteral("download")));
    if (restartDesktopAct_) restartDesktopAct_->setIcon(fromTheme(QStringLiteral("view-restore")));
    if (backendSectionAct_)  backendSectionAct_->setIcon(fromTheme(QStringLiteral("system-services")));
    if (startServiceAct_)    startServiceAct_->setIcon(fromTheme(QStringLiteral("media-playback-start")));
    if (restartServiceAct_)  restartServiceAct_->setIcon(fromTheme(QStringLiteral("view-refresh")));
    if (stopServiceAct_)     stopServiceAct_->setIcon(fromTheme(QStringLiteral("media-playback-stop")));
    if (logAct_)     logAct_->setIcon(fromTheme(QStringLiteral("text-x-generic")));
    if (clearAct_)   clearAct_->setIcon(fromTheme(QStringLiteral("edit-delete")));
    if (aboutAct_)   aboutAct_->setIcon(fromTheme(QStringLiteral("help-about")));
    if (quitAct_)    quitAct_->setIcon(fromTheme(QStringLiteral("application-exit")));
}

}  // namespace dsh::app
