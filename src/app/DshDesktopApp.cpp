// SPDX-License-Identifier: MIT
// @author zhouwr
#include "DshDesktopApp.h"

#include "../icon/IconLoader.h"
#include "../updater/Updater.h"
#include "ExitDialog.h"
#include "UpdateDialog.h"

#include <QAbstractSocket>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <iostream>

#include "util/Notify.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStandardPaths>
#include <QThread>

namespace dsh::app {

namespace {

// 在 offscreen / CI 模式下 ``QSystemTrayIcon::isSystemTrayAvailable()``
// 会阻塞等 D-Bus 应答，所以先嗅探 KDE StatusNotifierWatcher 服务。
//
// 仅探测显式会话地址和当前用户的 XDG runtime bus，不跨用户猜测。

/// 在多个常见路径里探测一个能连上 KDE StatusNotifierWatcher 的 D-Bus session。
/// 命中后会把 ``DBUS_SESSION_BUS_ADDRESS`` 设到该地址，所以
/// 后续 ``QDBusConnection::sessionBus()`` 也会连同一个。
QDBusConnection connectToKdeSessionBus() {
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        return QDBusConnection::sessionBus();
    }

    const QByteArray originalAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    QStringList candidates;
    if (!originalAddress.isEmpty()) candidates << QString::fromLocal8Bit(originalAddress);

    const QString runtime = QStandardPaths::writableLocation(
                                QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty()) candidates << runtime + QStringLiteral("/bus");
    candidates.removeDuplicates();

    for (int index = 0; index < candidates.size(); ++index) {
        QString address = candidates.at(index);
        if (address.startsWith('/')) address.prepend(QStringLiteral("unix:path="));
        const QString connectionName = QStringLiteral("dsh-kde-session-%1").arg(index);
        QDBusConnection connection = QDBusConnection::connectToBus(address, connectionName);
        if (connection.isConnected()) {
            QDBusInterface iface("org.kde.StatusNotifierWatcher",
                                  "/StatusNotifierWatcher",
                                  "org.kde.StatusNotifierWatcher", connection);
            if (iface.isValid()) {
                qputenv("DBUS_SESSION_BUS_ADDRESS", address.toLocal8Bit());
                return connection;
            }
        }
        QDBusConnection::disconnectFromBus(connectionName);
    }

    if (originalAddress.isEmpty()) qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    else qputenv("DBUS_SESSION_BUS_ADDRESS", originalAddress);
    return QDBusConnection::sessionBus();
}

bool probeTrayAvailable() {
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") return true;
    const QDBusConnection conn = connectToKdeSessionBus();
    if (conn.isConnected()) {
        QDBusInterface iface("org.kde.StatusNotifierWatcher",
                              "/StatusNotifierWatcher",
                              "org.kde.StatusNotifierWatcher", conn);
        if (iface.isValid()) return true;
    }
    // 兜底：Qt 自带的 X11/XEmbed 检测（在无 StatusNotifierWatcher 但
    // 仍有 XEmbed 兼容托盘的环境下可用，例如旧 GNOME / xrdp 等）
    return QSystemTrayIcon::isSystemTrayAvailable();
}

}  // namespace

DshDesktopApp::DshDesktopApp(AppArgs args, QObject* parent)
    : QObject(parent), args_(std::move(args)) {
    if (!args_.logFile.isEmpty()) {
        logger_.setFile(args_.logFile);
        logger_.log(QStringLiteral("DSH Desktop 启动，日志文件：%1").arg(args_.logFile));
    }
    backend_ = dsh::backend::Backend::createForHost(args_.url, this);
    connect(backend_.get(), &dsh::backend::Backend::log, this,
            [this](const QString& m) { logger_.log(m); });
    theme_ = new dsh::theme::ThemeWatcher(this);
    // --theme 强制：覆盖 ThemeWatcher 的探测结果
    if (!args_.forceTheme.isEmpty()) {
        logger_.log(QStringLiteral("theme: --theme %1 覆盖自动检测")
                        .arg(args_.forceTheme));
        theme_->setForcedScheme(args_.forceTheme);
    }
    connect(theme_, &dsh::theme::ThemeWatcher::schemeChanged,
            this, &DshDesktopApp::onThemeChanged);
}

DshDesktopApp::~DshDesktopApp() {
    if (tray_) tray_->hide();
    if (window_) window_->close();
}

int DshDesktopApp::smoke() {
    auto s = backend_->status();
    // 写到 stderr：smoke 脚本经常通过管道把 stdout 当数据通道，避开 stdout 缓冲
    std::fprintf(stderr,
                 "smoke: backend 运行中=%s 模式=%d url=%s 详情=%s\n",
                 s.running ? "true" : "false",
                 static_cast<int>(s.mode),
                 s.url.toStdString().c_str(),
                 s.detail.toStdString().c_str());
    std::fflush(stderr);
    return s.running ? 0 : 1;
}

int DshDesktopApp::probe() {
    // 收集完整环境信息，方便安装脚本判断用户机器是否具备运行条件。
    struct Check {
        const char* name;
        bool ok;
        QString detail;
    };
    QList<Check> checks;

    const QByteArray display = qgetenv("DISPLAY");
    const QByteArray wldisplay = qgetenv("WAYLAND_DISPLAY");
    checks.append({"display", !display.isEmpty() || !wldisplay.isEmpty(),
                    QStringLiteral("DISPLAY=%1 WAYLAND_DISPLAY=%2")
                        .arg(QString::fromLocal8Bit(display),
                             QString::fromLocal8Bit(wldisplay))});

    QDBusConnection bus = connectToKdeSessionBus();
    checks.append({"dbus-session", bus.isConnected(),
                    bus.isConnected() ? QStringLiteral("已连接") : QStringLiteral("无 D-Bus 会话总线")});

    QDBusInterface watcher("org.kde.StatusNotifierWatcher",
                            "/StatusNotifierWatcher",
                            "org.kde.StatusNotifierWatcher", bus);
    checks.append({"status-notifier-watcher", watcher.isValid(),
                    watcher.isValid() ? QStringLiteral("KDE Plasma 托盘 watcher 在线")
                                       : QStringLiteral("未找到 org.kde.StatusNotifierWatcher")});

    QDBusInterface notify("org.freedesktop.Notifications",
                          "/org/freedesktop/Notifications",
                          "org.freedesktop.Notifications", bus);
    checks.append({"freedesktop-notifications", notify.isValid(),
                    notify.isValid() ? QStringLiteral("通知服务在线") : QStringLiteral("无通知服务")});

    auto status = backend_->status();
    checks.append({"dsh-web", status.running,
                    QStringLiteral("url=%1 mode=%2 detail=%3")
                        .arg(status.url)
                        .arg(static_cast<int>(status.mode))
                        .arg(status.detail)});

    bool all_ok = true;
    std::fprintf(stderr, "DSH 环境探测:\n");
    for (const auto& c : checks) {
        std::fprintf(stderr, "  %-30s %s   %s\n",
                     c.name, c.ok ? "OK " : "!! ",
                     c.detail.toStdString().c_str());
        if (!c.ok) all_ok = false;
    }
    std::fflush(stderr);
    return all_ok ? 0 : 2;
}

int DshDesktopApp::selfTest() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    return run();
}

int DshDesktopApp::run() {
    QApplication* app = qApp;
    if (!app) {
        static int argc = 1;
        static char appName[] = "dsh-desktop";
        static char* argv[] = {appName, nullptr};
        app = new QApplication(argc, argv);
    }
    qtApp_ = app;

    QApplication::setApplicationDisplayName(QStringLiteral("DSH Desktop"));
    QApplication::setQuitOnLastWindowClosed(false);  // 我们活在托盘里
    // 在创建任何原生窗口前先同步 KDE 真值，避免默认 light 图标闪现，
    // 也确保首个 _NET_WM_ICON 就是正确颜色。
    theme_->refresh();
    applyLogoTheme(theme_->current());
    // 单实例锁：先尝试连接现有 socket；连接成功 → 给原实例发唤醒信号后退出。
    // 连接失败 → 自己是第一个实例，清除残留 socket 后建立新的监听。
    if (!args_.selfTest && !args_.smoke) {
        const QString sockPath = singleInstanceSocketPath();
        // 先探测：现有 socket 是不是有人在听？
        QLocalSocket probe;
        probe.connectToServer(sockPath);
        if (probe.waitForConnected(500)) {
            probe.write("show\n");
            probe.flush();
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            logger_.log("已有 DSH Desktop 实例在运行，本次启动退出。");
            std::fprintf(stderr,
                         "DSH Desktop 已在运行；本次启动退出。\n"
                         "（点击托盘图标即可显示窗口）\n");
            std::fflush(stderr);
            return 0;
        }
        probe.abort();
        // 连接失败后先尝试监听；只有地址确认为残留时，才删除并重试一次。
        singleInstanceServer_ = new QLocalServer(this);
        singleInstanceServer_->setSocketOptions(QLocalServer::UserAccessOption);
        if (!singleInstanceServer_->listen(sockPath)
            && singleInstanceServer_->serverError() == QAbstractSocket::AddressInUseError) {
            QLocalServer::removeServer(sockPath);
        }
        if (singleInstanceServer_->isListening() || singleInstanceServer_->listen(sockPath)) {
            connect(singleInstanceServer_, &QLocalServer::newConnection,
                    this, &DshDesktopApp::onSecondInstance);
            logger_.log(QStringLiteral("单实例锁已建立：%1").arg(sockPath));
        } else {
            logger_.log(QStringLiteral("单实例锁失败：%1；继续启动（多实例并存）")
                            .arg(singleInstanceServer_->errorString()));
        }
    }

    if (!probeTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("DSH Desktop"),
                              QStringLiteral("系统托盘不可用，请检查 KDE Plasma 是否在运行。"));
        return 3;
    }

    if (!backend_->isRunning()) {
        const auto status = backend_->status();
        if (status.mode == dsh::backend::Mode::External) {
            logger_.log(QStringLiteral("启动：远程 dsh web 不可达：%1")
                            .arg(status.url));
            if (!args_.selfTest) {
                QMessageBox::warning(
                    nullptr, QStringLiteral("DSH Desktop"),
                    QStringLiteral("无法连接远程 dsh web：\n%1\n\n"
                                   "请检查地址、网络和远程服务状态。")
                        .arg(status.url));
            }
        } else {
            // systemd 模式下，若官方后端处于 inactive/failed，不再静默自动拉起：
            // 先问用户是否启动；Supervised / 其它状态仍保留原有自动启动行为。
            bool shouldStart = true;
            if (dsh::backend::requiresStartConfirmation(status) && !args_.selfTest) {
                const QString why =
                    status.state == dsh::service::LifecycleState::Failed
                        ? tr("该 DSH 后端服务当前处于失败状态（failed），未正常运行。")
                        : tr("该 DSH 后端服务当前已停止（inactive）。");
                const QMessageBox::StandardButton button = QMessageBox::question(
                    nullptr, tr("启动 DSH 后台服务"),
                    tr("%1\n\n是否立即启动它？\n%2")
                        .arg(why,
                             status.detail.isEmpty() ? status.url : status.detail),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                shouldStart = (button == QMessageBox::Yes);
                if (!shouldStart) {
                    logger_.log(
                        QStringLiteral("启动：用户未授权启动已停止/失败的 DSH 后端，"
                                       "保持停止状态，继续打开桌面端。"));
                }
            }
            if (shouldStart) {
                logger_.log("启动：dsh web 未运行；尝试启动");
                if (!backend_->start()) {
                // 启动失败时给用户明确的修复指引，而不是"无法启动"了事
                    const QStringList fixes = {
                        QStringLiteral("• 检查 `dsh` 是否安装：`which dsh`"),
                        QStringLiteral("• systemd 模式下：`systemctl status dsh-web.service`"),
                        QStringLiteral("• 子进程模式下：尝试手动执行 `dsh web` 查看错误"),
                        QStringLiteral("• 网络端口冲突：`ss -ltn | grep 3080`"),
                    };
                    QMessageBox box;
                    box.setIcon(QMessageBox::Warning);
                    box.setWindowTitle(QStringLiteral("DSH Desktop"));
                    box.setText(QStringLiteral("无法启动 dsh web 服务，桌面端仍会启动。\n\n"
                                               "可执行以下步骤排查：\n%1").arg(fixes.join("\n")));
                    box.setStandardButtons(QMessageBox::Ok);
                    box.setDetailedText(status.detail);
                    box.exec();
                }
            }
        }
    }

    window_ = new DshWindow(backend_->url(), theme_,
                            [this](const QString& m) { logger_.log(m); });
    tray_ = new TrayController(this);
    tray_->bind(window_,
                [this]() { onCheckUpdates(); },
                [this]() { onPerformUpdate(); },
                [this]() { onRestartDesktop(); },
                [this]() { onStartBackend(); },
                [this]() { onRestartBackend(); },
                [this]() { onStopBackend(); },
                [this]() { onRequestQuit(); },
                [this]() { onShowAbout(); },
                [this]() { onShowLog(); },
                [this]() { onClearDownloads(); });
    connect(tray_, &TrayController::triggered,
            this, &DshDesktopApp::onTrayTriggered);

    applyLogoTheme(theme_->current());
    theme_->start();
    tray_->show();
    window_->show();

    // 启动后 60 秒自动后台检查更新；发现新版本时弹 KDE 通知 + 显示菜单按钮
    if (!args_.selfTest && !args_.smoke) {
        QTimer::singleShot(60'000, this, [this]() { onCheckUpdates(true); });
    }

    // 后端健康检查使用异步 Qt 网络请求，避免 curl/systemctl 阻塞 GUI 线程。
    if (!args_.selfTest && !args_.smoke) {
        refreshBackendMenuState(backend_->isRunning());
        healthNetwork_ = new QNetworkAccessManager(this);
        QTimer* healthTimer = new QTimer(this);
        healthTimer->setInterval(30'000);
        connect(healthTimer, &QTimer::timeout,
                this, &DshDesktopApp::pollBackendHealth);
        healthTimer->start();
        pollBackendHealth();
    }

    // 自检模式：输出报告后立即 _exit，避免 QtWebEngine 在 offscreen
    // 模式下退出时偶发的 SIGSEGV 影响冒烟脚本的退出码解析。
    if (args_.selfTest) {
        QTimer::singleShot(800, this, [this]() {
            QJsonObject report;
            report.insert(QStringLiteral("tray_visible"), tray_ && tray_->isVisible());
            report.insert(QStringLiteral("window_visible"), window_ && window_->isVisible());
            report.insert(QStringLiteral("window_title"),
                          window_ ? window_->windowTitle() : QString());
            report.insert(QStringLiteral("window_url"),
                          window_ ? window_->currentUrl().toString() : QString());
            report.insert(QStringLiteral("theme"), theme_ ? theme_->current() : QString());
            report.insert(QStringLiteral("tray_theme"),
                          tray_ ? tray_->appliedTheme() : QString());
            report.insert(QStringLiteral("tray_tooltip"),
                          tray_ ? tray_->toolTip() : QString());
            report.insert(QStringLiteral("logo_theme"), appliedLogoTheme_);
            report.insert(QStringLiteral("window_logo_theme"),
                          window_ ? window_->appliedLogoTheme() : QString());
            report.insert(QStringLiteral("clipboard_write_enabled"),
                          window_ && window_->clipboardWriteEnabled());
            QJsonArray menuItems;
            if (tray_) {
                for (QAction* a : tray_->menuActions()) {
                    menuItems.append(a->text());
                }
            }
            report.insert(QStringLiteral("menu_items"), menuItems);
            report.insert(QStringLiteral("update_action_visible"), updateAvailable_);
            report.insert(QStringLiteral("backend"), backend_->status().detail);
            std::cout << "DSH_DESKTOP_SELF_TEST_BEGIN" << std::endl
                      << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString()
                      << std::endl;
            std::cout << "DSH_DESKTOP_SELF_TEST_END" << std::endl;
            std::cout.flush();
            std::fflush(stdout);
            std::_Exit(0);  // 直接退出，绕开 QtWebEngine 清理时的崩溃
        });
    }

    return app->exec();
}

void DshDesktopApp::onCheckUpdates(bool silent) {
    if (updateCheckInProgress_) {
        if (!silent) {
            dsh::util::notify(tr("DSH 更新"), tr("更新检查正在进行，请稍候。"));
        }
        return;
    }
    updateCheckInProgress_ = true;
    logger_.log(QStringLiteral("托盘：检查更新中（silent=%1）…").arg(silent ? "yes" : "no"));
    QPointer<DshDesktopApp> self(this);
    QThread* thread = QThread::create([self, silent]() {
        const auto status = dsh::updater::Updater::check(8);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, status, silent]() {
            if (self) self->finishUpdateCheck(status, silent);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DshDesktopApp::finishUpdateCheck(const dsh::updater::Status& status, bool silent) {
    updateCheckInProgress_ = false;
    if (status.updateAvailable) {
        updateAvailable_ = true;
        tray_->setUpdateAvailable(true);
        if (silent) {
            // 后台模式：只弹 KDE 通知，不打断用户
            dsh::util::notify(
                QStringLiteral("DSH 有新版本可用"),
                QStringLiteral("当前 %1 → 最新 %2，点击托盘菜单的『更新到最新版』升级。")
                    .arg(status.current, status.latest),
                QStringLiteral("normal"),
                QStringLiteral("dialog-information"),
                8000);
        } else {
            UpdateDialog dlg(status, window_);
            dlg.exec();
        }
    } else {
        updateAvailable_ = false;
        tray_->setUpdateAvailable(false);
        if (!silent) {
            UpdateDialog dlg(status, window_);
            dlg.exec();
        }
    }
}

void DshDesktopApp::onPerformUpdate() {
    onCheckUpdates(false);
}

bool DshDesktopApp::backendManageable() const {
    if (!backend_) return false;
    const auto status = backend_->status();
    if (status.mode == dsh::backend::Mode::External) return false;
    if (status.state == dsh::service::LifecycleState::Unmanaged) return false;
    return status.manageable;
}

void DshDesktopApp::refreshBackendMenuState(bool running) {
    if (!tray_ || !backend_) return;
    // 菜单分组是否可管理：当前实现中仅 External（远程）后端不可管理；
    // Unmanaged 状态由 backendManageable() 在各动作处理器里严格复核。
    const bool manageable = backendManageable();
    tray_->setBackendManageable(manageable, running);
}

void DshDesktopApp::onRestartDesktop() {
    logger_.log("托盘：重启 DSH Desktop");
    if (!qtApp_) return;
    // 用当前可执行文件 + 原始参数重新拉起；先释放单实例锁，避免新实例把
    // 自己当成"二次启动"而立刻退出。
    const QString program = QCoreApplication::applicationFilePath();
    QStringList args = qtApp_->arguments();
    if (!args.isEmpty()) args.removeFirst();
    if (singleInstanceServer_) {
        singleInstanceServer_->close();
        singleInstanceServer_->deleteLater();
        singleInstanceServer_ = nullptr;
        QLocalServer::removeServer(singleInstanceSocketPath());
    }
    if (QProcess::startDetached(program, args)) {
        logger_.log("DSH Desktop 已重新拉起（startDetached），准备退出（不停止后台服务）。");
        performQuit(false);  // 仅退出桌面端，后台服务保持运行
    } else {
        logger_.log("DSH Desktop 重启失败：QProcess::startDetached 返回 false");
        QMessageBox::warning(window_, QStringLiteral("DSH Desktop"),
                             QStringLiteral("无法重启 DSH Desktop。"));
    }
}

void DshDesktopApp::onStartBackend() {
    logger_.log("托盘：启动后台服务");
    if (!backend_) return;
    const auto status = backend_->status();
    if (!backendManageable()) {
        logger_.log(QStringLiteral("托盘：后台服务不可管理，拒绝启动（mode=%1 state=%2 manageable=%3）")
                        .arg(static_cast<int>(status.mode))
                        .arg(static_cast<int>(status.state))
                        .arg(status.manageable));
        QMessageBox::information(window_, tr("DSH 后台服务"),
                                 tr("该后端由外部管理，桌面端无法启动。"));
        return;
    }
    if (status.running) {
        logger_.log("托盘：后台服务已在运行，无需启动");
        QMessageBox::information(window_, tr("DSH 后台服务"),
                                 tr("后台服务已在运行。"));
        return;
    }
    if (backend_->start()) {
        logger_.log("托盘：后台服务已启动");
        dsh::util::notify(tr("DSH 后台服务"), tr("后台服务已启动。"));
        refreshBackendMenuState(true);
        QTimer::singleShot(800, this, [this]() { if (window_) window_->reload(); });
    } else {
        logger_.log("托盘：后台服务启动失败");
        QMessageBox::warning(window_, tr("DSH 后台服务"),
                             tr("无法启动后台服务。\n%1").arg(status.detail));
        refreshBackendMenuState(false);
    }
}

void DshDesktopApp::onRestartBackend() {
    logger_.log("托盘：重启后台服务");
    if (!backend_) return;
    const auto status = backend_->status();
    if (!backendManageable()) {
        logger_.log(QStringLiteral("托盘：后台服务不可管理，拒绝重启（mode=%1 state=%2 manageable=%3）")
                        .arg(static_cast<int>(status.mode))
                        .arg(static_cast<int>(status.state))
                        .arg(status.manageable));
        QMessageBox::information(window_, tr("DSH 后台服务"),
                                 tr("该后端由外部管理，桌面端无法重启。"));
        return;
    }
    if (!status.running) {
        logger_.log("托盘：后台服务未运行，重启等价于启动");
        onStartBackend();
        return;
    }
    if (backend_->restart()) {
        logger_.log("托盘：后台服务已重启");
        dsh::util::notify(tr("DSH 后台服务"), tr("后台服务已重启。"));
        refreshBackendMenuState(true);
        QTimer::singleShot(800, this, [this]() { if (window_) window_->reload(); });
    } else {
        logger_.log("托盘：后台服务重启失败");
        QMessageBox::warning(window_, tr("DSH 后台服务"),
                             tr("无法重启后台服务。\n%1").arg(status.detail));
        refreshBackendMenuState(status.running);
    }
}

void DshDesktopApp::onStopBackend() {
    logger_.log("托盘：停止后台服务");
    if (!backend_) return;
    const auto status = backend_->status();
    if (!backendManageable()) {
        logger_.log(QStringLiteral("托盘：后台服务不可管理，拒绝停止（mode=%1 state=%2 manageable=%3）")
                        .arg(static_cast<int>(status.mode))
                        .arg(static_cast<int>(status.state))
                        .arg(status.manageable));
        QMessageBox::information(window_, tr("DSH 后台服务"),
                                 tr("该后端由外部管理，桌面端无法停止。"));
        return;
    }
    if (!status.running) {
        logger_.log("托盘：后台服务未运行，无需停止");
        QMessageBox::information(window_, tr("DSH 后台服务"),
                                 tr("后台服务当前未运行。"));
        return;
    }
    // 停止前必须原生确认：停止已有官方服务可能影响其它终端/脚本/远程访问。
    QMessageBox::StandardButton button = QMessageBox::question(
        window_, tr("停止后台服务"),
        tr("确定要停止 DSH 后台服务吗？\n\n%1").arg(status.detail),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button != QMessageBox::Yes) {
        logger_.log("托盘：用户取消停止后台服务");
        return;
    }
    if (backend_->stop()) {
        logger_.log("托盘：后台服务已停止");
        dsh::util::notify(tr("DSH 后台服务"), tr("后台服务已停止。"));
        refreshBackendMenuState(false);
    } else {
        logger_.log("托盘：后台服务停止失败");
        QMessageBox::warning(window_, tr("DSH 后台服务"),
                             tr("无法停止后台服务。\n%1").arg(status.detail));
        refreshBackendMenuState(true);
    }
}

void DshDesktopApp::onRequestQuit() {
    if (shutdownStarted_) return;
    shutdownStarted_ = true;
    auto status = backend_->status();
    ExitDialog dlg(status.activeTasks, status.url,
                   status.mode == dsh::backend::Mode::Supervised,
                   status.mode != dsh::backend::Mode::External, window_);
    if (dlg.exec() != QDialog::Accepted) {
        shutdownStarted_ = false;
        return;
    }
    performQuit(dlg.stopBackgroundService());
}

void DshDesktopApp::onTrayTriggered() {
    if (!window_) return;
    if (window_->isVisible()) {
        window_->hide();
    } else {
        window_->showAndRaise();
    }
}

void DshDesktopApp::onThemeChanged(const QString& scheme) {
    logger_.log(QStringLiteral("theme: 切换为 %1，统一刷新所有 Logo 表面")
                    .arg(scheme));
    applyLogoTheme(scheme);
}

void DshDesktopApp::pollBackendHealth() {
    if (!healthNetwork_ || healthReply_) return;
    QNetworkRequest request{QUrl(backend_->url())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = healthNetwork_->get(request);
    healthReply_ = reply;
    QTimer::singleShot(2000, reply, [reply]() {
        if (!reply->isFinished()) {
            reply->setProperty("dshTimedOut", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (healthReply_ != reply) {
            reply->deleteLater();
            return;
        }
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool running = reply->error() == QNetworkReply::NoError
            && httpStatus >= 200 && httpStatus < 500;
        const QString detail = running
            ? tr("HTTP %1").arg(httpStatus)
            : (reply->property("dshTimedOut").toBool()
                   ? tr("连接超时") : reply->errorString());
        // 让"DSH 后台服务"分组按可达性与可管理性刷新启用状态。
        refreshBackendMenuState(running);
        if (backendHealthKnown_ && running != lastBackendRunning_) {
            if (running) {
                dsh::util::notify(tr("DSH Web 已恢复"),
                                  tr("后端服务重新可用：%1").arg(backend_->url()),
                                  tr("normal"), tr("dialog-information"), 5000);
                logger_.log("health: dsh-web 已恢复运行");
            } else {
                dsh::util::notify(
                    tr("DSH Web 已停止"),
                    tr("后端服务不可达。点托盘『DSH 后台服务 → 启动后台服务』可尝试拉起。\n%1")
                        .arg(detail),
                    tr("critical"), tr("dialog-error"), 0);
                logger_.log(QStringLiteral("health: dsh-web 已停止：%1").arg(detail));
            }
        }
        backendHealthKnown_ = true;
        lastBackendRunning_ = running;
        healthReply_.clear();
        reply->deleteLater();
    });
}

// 统一 logo 主题入口：托盘 / 窗口 / 任务栏 / 菜单 全部走这里。
// 保证四个表面始终用同一套黑白鲸鱼（+ KDE 语义菜单图标），同步切换。
void DshDesktopApp::applyLogoTheme(const QString& scheme) {
    appliedLogoTheme_ = scheme == QStringLiteral("dark")
        ? QStringLiteral("dark") : QStringLiteral("light");
    // 1. Qt 图标主题跟随亮暗：亮色 → breeze，暗色 → breeze-dark（KDE 标准）
    QIcon::setThemeName(appliedLogoTheme_ == "dark" ? QStringLiteral("breeze-dark")
                                                     : QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    const QIcon logo = dsh::icon::iconForScheme(appliedLogoTheme_);
    // 2. 托盘图标（小鲸鱼）
    if (tray_) tray_->applyLogo(logo, appliedLogoTheme_);
    // 3. 托盘菜单语义图标（QIcon::fromTheme 跟随 breeze/breeze-dark）
    if (tray_) tray_->setMenuIcons();
    // 4. 窗口标题栏图标（小鲸鱼）
    if (window_) window_->applyLogo(logo, appliedLogoTheme_);
    // 5. 任务栏（应用级 _NET_WM_ICON 兜底；KDE 任务栏实际读窗口的图标）
    if (qtApp_) qtApp_->setWindowIcon(logo);
    // 6. About 若正打开，也立即同步；新打开时同样通过本入口初始化。
    if (aboutDialog_) aboutDialog_->applyLogo(logo, appliedLogoTheme_);
}

void DshDesktopApp::performQuit(bool stopBackground) {
    logger_.log(QStringLiteral("退出：stop_background=%1").arg(stopBackground));
    if (stopBackground && backend_) {
        backend_->stop();
    }
    if (tray_) tray_->hide();
    if (window_) window_->close();
    if (qtApp_) qtApp_->quit();
}

QString DshDesktopApp::singleInstanceSocketPath() {
    QString runtime = QStandardPaths::writableLocation(
                          QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        runtime = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/runtime");
        QDir().mkpath(runtime);
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
    }
    return runtime + QStringLiteral("/dsh-desktop.sock");
}

void DshDesktopApp::onSecondInstance() {
    // 收到二次启动的唤醒请求，弹窗口 + 提示音
    logger_.log("收到第二次实例的唤醒信号");
    if (window_) window_->showAndRaise();
}

void DshDesktopApp::onShowAbout() {
    if (!window_) return;
    AboutDialog dlg(window_);
    aboutDialog_ = &dlg;
    applyLogoTheme(theme_ ? theme_->current() : QStringLiteral("light"));
    dlg.exec();
    aboutDialog_.clear();
}

void DshDesktopApp::onShowLog() {
    if (!window_) return;
    LogViewer dlg(args_.logFile, window_);
    dlg.exec();
}

void DshDesktopApp::onClearDownloads() {
    // 清理 ~/.local/share/dsh-desktop/downloads/ 下的所有文件
    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation) + "/downloads";
    QDir d(dir);
    if (!d.exists()) {
        QMessageBox::information(window_, tr("清空下载缓存"),
                                 tr("下载目录不存在，无需清理。\n%1").arg(dir));
        return;
    }
    QStringList files = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
    if (files.isEmpty()) {
        QMessageBox::information(window_, tr("清空下载缓存"),
                                 tr("下载目录已为空。\n%1").arg(dir));
        return;
    }
    QMessageBox box;
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("确认清空下载缓存"));
    box.setText(tr("即将删除 %1 个会话日志文件（约 %2 KB），无法恢复。\n\n"
                    "目录：%3").arg(files.size()).arg(0).arg(dir));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;
    int removed = 0;
    qint64 freed = 0;
    for (const auto& name : files) {
        QFileInfo fi(d.absoluteFilePath(name));
        qint64 sz = fi.size();
        if (d.remove(name)) {
            ++removed;
            freed += sz;
        }
    }
    logger_.log(QStringLiteral("已清理 %1 个下载文件，释放 %2 KB").arg(removed).arg(freed / 1024));
    dsh::util::notify(tr("下载缓存已清空"),
                      tr("已删除 %1 个文件，释放 %2 KB").arg(removed).arg(freed / 1024),
                      tr("normal"), tr("dialog-information"), 4000);
}

}  // namespace dsh::app
