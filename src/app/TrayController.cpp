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
    restartAct_ = menu_->addAction(tr("重启桌面"));
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
                          std::function<void()> onRestart,
                          std::function<void()> onQuit,
                          std::function<void()> onAbout,
                          std::function<void()> onLog,
                          std::function<void()> onClear) {
    connect(showAct_,    &QAction::triggered, this, [window]() { window->showAndRaise(); });
    connect(hideAct_,    &QAction::triggered, this, [window]() { window->hide(); });
    connect(checkAct_,   &QAction::triggered, this, [onCheck]()    { onCheck(); });
    connect(updateAct_,  &QAction::triggered, this, [onUpdate]()   { onUpdate(); });
    connect(restartAct_, &QAction::triggered, this, [onRestart]()  { onRestart(); });
    connect(logAct_,     &QAction::triggered, this, [onLog]()      { onLog(); });
    connect(clearAct_,   &QAction::triggered, this, [onClear]()    { onClear(); });
    connect(aboutAct_,   &QAction::triggered, this, [onAbout]()    { onAbout(); });
    connect(quitAct_,    &QAction::triggered, this, [onQuit]()     { onQuit(); });
}

void TrayController::setUpdateAvailable(bool available) {
    updateAct_->setVisible(available);
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
    if (restartAct_) restartAct_->setIcon(fromTheme(QStringLiteral("view-restore")));
    if (logAct_)     logAct_->setIcon(fromTheme(QStringLiteral("text-x-generic")));
    if (clearAct_)   clearAct_->setIcon(fromTheme(QStringLiteral("edit-delete")));
    if (aboutAct_)   aboutAct_->setIcon(fromTheme(QStringLiteral("help-about")));
    if (quitAct_)    quitAct_->setIcon(fromTheme(QStringLiteral("application-exit")));
}

}  // namespace dsh::app
