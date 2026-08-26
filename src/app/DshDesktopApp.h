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
#include "../updater/UpdatePlan.h"
#include "../util/Logger.h"
#include "AboutDialog.h"
#include "DshWindow.h"
#include "LogViewer.h"
#include "TrayController.h"

namespace dsh::app {

/// 命令行参数结构体。
struct AppArgs {
    QString url;        // dsh web URL（默认 http://127.0.0.1:3080）
    QString logFile;    // 可选日志文件路径
    QString forceTheme; // --theme 强制：dark / light（默认自动检测）
    bool smoke{false};  // --smoke：仅探测后端
    bool selfTest{false};// --self-test：完整启动后输出报告再退出
};

/// 退出/重启流程里"对后台服务想做什么"的统一意图。
///
/// 由 ExitDialog / RestartDialog 的勾选状态 + 后端实际运行状态联合决定
/// 最终执行的动作；本结构只承载用户意图与必要上下文。
struct ShutdownIntent {
    bool manageBackend{false};   // 用户是否勾选了"同时处理后台服务"
    dsh::backend::Status status; // 触发对话框时的后端快照
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
    void finishUpdateCheck(const dsh::updater::UpdatePlan& plan, bool silent);
    void onPerformUpdate();

    /// 托盘"重启"菜单点击：先弹 RestartDialog，按勾选执行相应动作后
    /// 重新拉起桌面端并退出。
    void onRestartWithDialog();

    /// 托盘"退出"菜单点击：先弹 ExitDialog，按勾选执行相应动作后退出。
    void onRequestQuit();

    void onShowAbout();
    void onShowLog();
    void onClearDownloads();
    void onTrayTriggered();
    void onThemeChanged(const QString& scheme);
    void pollBackendHealth();
    void applyLogoTheme(const QString& scheme);

    /// 按 ``ShutdownIntent`` 决定对后台服务做什么，再执行桌面端退出。
    /// ``backendAction`` 在调用前完成；动作即使失败也不会阻塞桌面端退出。
    void performQuit(const ShutdownIntent& intent);

    /// 按 ``ShutdownIntent`` 决定对后台服务做什么，再重新拉起桌面端并
    /// 退出当前实例。动作即使失败也不会阻塞桌面端重启。
    void performRestart(const ShutdownIntent& intent);

    /// 拉起后台服务；不可用时按用户授权执行 ``npm i -g @deepseek-ai/dsh``
    /// 自动安装再重试。返回是否最终启动成功（含已运行情况）。
    bool ensureBackendStarted(const dsh::backend::Status& status);

    /// 退出/重启对话框的通用流程：互斥锁 + 显示对话框 + 落地前重拉 status。
    /// 由 onRestartWithDialog / onRequestQuit 共用，避免重复样板。
    void runShutdownDialog(
        std::function<std::unique_ptr<QDialog>(const dsh::backend::Status&)> makeDialog,
        std::function<bool(QDialog*)> readCheckbox,
        std::function<void(const ShutdownIntent&)> apply);

    void onSecondInstance();
    static QString singleInstanceSocketPath();

    /// 桌面自更新接管：拉起已安装的 ``dsh-desktop-updater``（带当前 PID /
    /// 下载包路径 / 目标二进制 / SHA-256 与安全安装前缀），释放单实例锁并
    /// 退出且不停止后台服务。助手不可用（缺失 / 不可执行）时返回 ``false``，
    /// 且**不替换任何文件**。
    bool launchDesktopSelfUpdate(const QString& sourcePath, const QString& sha256);

    /// 桌面端当前能否管理后台服务生命周期（External / ``Unmanaged`` /
    /// 不可管理时为 false）。
    bool backendManageable() const;

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
    /// 连续健康检查失败次数；达到阈值后才发布停服状态，避免一次 TCP
    /// 抖动就误报“后端停止”。桌面端不在健康轮询中静默重启服务。
    int consecutiveHealthFailures_{0};
    static constexpr int kHealthFailureThreshold = 3;
    QString appliedLogoTheme_{QStringLiteral("light")};
};

}  // namespace dsh::app
