// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 顶层控制器：把托盘、窗口、后端、主题监听器、更新器拼到同一个 Qt 事件循
// 环里。

#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

QT_BEGIN_NAMESPACE
class QApplication;
class QEventLoop;
class QLocalServer;
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

#include "../backend/Backend.h"
#include "../theme/ThemeWatcher.h"
#include "../util/Logger.h"
#include "AboutDialog.h"
#include "DshWindow.h"
#include "LogViewer.h"
#include "TrayController.h"

namespace dsh::updater {
struct Status;
}

namespace dsh::app {

/// 命令行参数结构体。
struct AppArgs {
    QString url;        // dsh web URL（默认 http://127.0.0.1:3080）
    QString logFile;    // 可选日志文件路径
    QString forceTheme; // --theme 强制：dark / light（默认自动检测）
    bool smoke{false};  // --smoke：仅探测后端
    bool selfTest{false};// --self-test：完整启动后输出报告再退出
};

class DshDesktopApp : public QObject {
    Q_OBJECT
public:
    explicit DshDesktopApp(AppArgs args, QObject* parent = nullptr);
    ~DshDesktopApp() override;

    /// 启动 Qt 主循环。返回 QApplication::exec() 的退出码。
    int run();

    /// 仅探测后端可达性（无需 Qt）。
    int smoke();

    /// 环境探测：DISPLAY / D-Bus / 托盘 watcher / 后端可达性。
    /// 不启动 GUI；只在 stderr 打印结构化诊断。
    int probe();

    /// 完整启动 + 输出结构化报告 + 退出。供 smoke.sh 使用。
    int selfTest();

private:
    void onCheckUpdates(bool silent = false);
    void finishUpdateCheck(const dsh::updater::Status& status, bool silent);
    void onPerformUpdate();
    void onRestartDesktop();   // 重启 DSH Desktop：重新拉起应用并退出
    void onStartBackend();     // 启动后台服务
    void onRestartBackend();   // 重启后台服务
    void onStopBackend();      // 停止后台服务
    void onRequestQuit();
    void onShowAbout();
    void onShowLog();
    void onClearDownloads();
    void onTrayTriggered();
    void onThemeChanged(const QString& scheme);
    void pollBackendHealth();
    void applyLogoTheme(const QString& scheme);
    void performQuit(bool stopBackground);
    void onSecondInstance();
    static QString singleInstanceSocketPath();

    /// 桌面端当前能否管理后台服务生命周期（External / ``Unmanaged`` /
    /// 不可管理时为 false）。
    bool backendManageable() const;

    /// 依据后端运行与可管理性刷新"DSH 后台服务"菜单分组状态。
    void refreshBackendMenuState(bool running);

    AppArgs args_;
    dsh::util::Logger logger_;
    std::unique_ptr<dsh::backend::Backend> backend_;
    dsh::theme::ThemeWatcher* theme_{nullptr};
    DshWindow* window_{nullptr};
    TrayController* tray_{nullptr};
    QPointer<AboutDialog> aboutDialog_;
    QApplication* qtApp_{nullptr};
    QLocalServer* singleInstanceServer_{nullptr};
    QNetworkAccessManager* healthNetwork_{nullptr};
    QPointer<QNetworkReply> healthReply_;
    bool shutdownStarted_{false};
    bool updateAvailable_{false};
    bool updateCheckInProgress_{false};
    bool backendHealthKnown_{false};
    bool lastBackendRunning_{false};
    QString appliedLogoTheme_{QStringLiteral("light")};
};

}  // namespace dsh::app
