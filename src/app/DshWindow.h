// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 承载官方 DSH Web UI 的原生窗口。
//
//   * 一个 ``QWebEngineView`` 指向 ``http://127.0.0.1:3080``。
//   * 一个 ``QWebEngineProfile``，用于绑定 ``downloadRequested`` 处理会话
//     日志下载。
//   * 一个 ``LoopbackWebPage``，确保任何外部 http(s) 跳转都改走系统浏览
//     器，而不是停留在 webview 内。
//   * 一个 ``DownloadInterceptor``，把"会话日志"按钮转换为带进度条的原生
//     保存对话框。
//
// 关闭窗口按钮=隐藏到托盘（KDE Plasma 惯例）；真正的退出只能通过托盘"退
// 出"菜单，保持生命周期可预期。

#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QWebEngineView;
class QWebEngineProfile;
class QWebEngineDownloadRequest;
class QCloseEvent;
class QIcon;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

#include "../theme/ThemeWatcher.h"
#include "../web/DownloadInterceptor.h"
#include "../web/LoopbackWebPage.h"

namespace dsh::app {

class DshWindow : public QMainWindow {
    Q_OBJECT
public:
    /// \param url   加载的 DSH web 地址。
    /// \param theme 主题监听器（用于切换图标）。
    /// \param log   日志回调。
    explicit DshWindow(const QString& url,
                        dsh::theme::ThemeWatcher* theme,
                        std::function<void(const QString&)> log,
                        QWidget* parent = nullptr);
    ~DshWindow() override;

    /// 把窗口带到前台（先取消最小化）。
    void showAndRaise();

    /// 重新加载当前 URL——启动/重启后台服务后刷新页面。
    void reload();

    QUrl currentUrl() const;
    bool clipboardWriteEnabled() const;
    void applyLogo(const QIcon& icon, const QString& scheme);
    QString appliedLogoTheme() const { return appliedLogoTheme_; }

    /// 还原窗口几何：上次的 size/pos/state。
    void restorePersistedGeometry();
    /// 持久化窗口几何：size/pos/state。窗口每次移动/缩放/隐藏时调用。
    void savePersistedGeometry();

signals:
    void log(const QString& message);

protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onDownloadRequested(QWebEngineDownloadRequest* item);
    void onThemeChanged(const QString& scheme);
    void onApplicationLoadFinished(bool success);

private:
    /// 屏幕可见性校验：把窗口恢复后做一次检查，若完全不可见就回退到屏幕中央。
    void ensureVisibleOnScreen();
    void createStartupPage();
    void showStartupPage(const QString& status, const QString& detail = {});
    void applyStartupTheme(const QString& scheme);
    void retryApplicationLoad();

    QString url_;
    dsh::theme::ThemeWatcher* theme_{nullptr};
    std::function<void(const QString&)> log_;

    QWebEngineProfile* profile_{nullptr};
    QWebEngineView* view_{nullptr};
    dsh::web::LoopbackWebPage* page_{nullptr};
    dsh::web::DownloadInterceptor* downloads_{nullptr};
    QStackedWidget* contentStack_{nullptr};
    QWidget* startupPage_{nullptr};
    QLabel* startupLogo_{nullptr};
    QLabel* startupStatus_{nullptr};
    QLabel* startupDetail_{nullptr};
    QProgressBar* startupProgress_{nullptr};
    QPushButton* startupRetry_{nullptr};

    QSettings settings_;  // ~/.config/dsh-desktop/QSettings.ini（XDG 兼容）
    bool geometryRestored_{false};
    QString appliedLogoTheme_{QStringLiteral("light")};
    int applicationLoadRetries_{0};
};

}  // namespace dsh::app
