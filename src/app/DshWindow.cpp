// SPDX-License-Identifier: MIT
// @author zhouwr
#include "DshWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLabel>
#include <QMoveEvent>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWindow>

#include <functional>

namespace dsh::app {

namespace {

// 默认的外部 URL 打开策略：优先 QDesktopServices，失败则 shell-out 到
// xdg-open。前者遵循 XDG mimeapps（Firefox / Chromium / 等）。
void openExternal(const QUrl& url) {
    if (!QDesktopServices::openUrl(url)) {
        QProcess::startDetached("xdg-open", {url.toString()});
    }
}

// 设置/清除 WebEngine 的 ForceDarkMode 属性，让 Chromium 在暗色主题下
// 反色渲染浅色网页，并让 ``prefers-color-scheme`` 媒体查询返回 dark。
//
// ``QWebEngineSettings::ForceDarkMode`` 需要 Qt 6.7 才存在，而项目的
// CMake 声明的最低版本是 Qt 6.5，因此这里用版本宏做编译期保护。
//
// 回退行为（Qt 6.5 / 6.6）：该属性不存在，无法让 Chromium 强制反色；此
// 函数退化为无害空操作，DSH Web UI 自身的暗色样式仍会随主题切换，缺的
// 仅是 Chromium 侧的系统级反色渲染。
void applyForceDarkMode(QWebEngineSettings* settings, bool dark) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    settings->setAttribute(QWebEngineSettings::ForceDarkMode, dark);
#else
    Q_UNUSED(settings);
    Q_UNUSED(dark);
#endif
}

}  // namespace

DshWindow::DshWindow(const QString& url,
                     dsh::theme::ThemeWatcher* theme,
                     std::function<void(const QString&)> log,
                     QWidget* parent)
    : QMainWindow(parent), url_(url), theme_(theme), log_(std::move(log)),
      settings_(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("anywhere-labs"),
                QStringLiteral("dsh-desktop")) {
    setWindowTitle(QStringLiteral("DSH Desktop"));

    const QString profileRoot = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/webengine");
    QDir().mkpath(profileRoot);
    profile_ = new QWebEngineProfile(QStringLiteral("dsh-desktop"), this);
    profile_->setPersistentStoragePath(profileRoot + QStringLiteral("/storage"));
    profile_->setCachePath(profileRoot + QStringLiteral("/cache"));
    downloads_ = new dsh::web::DownloadInterceptor(this);
    connect(downloads_, &dsh::web::DownloadInterceptor::log, this,
            [this](const QString& m) { if (log_) log_(m); });
    connect(profile_, &QWebEngineProfile::downloadRequested,
            this, &DshWindow::onDownloadRequested);

    auto settings = profile_->settings();
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);

    // 暗色主题下让 Chromium 也渲染暗色（DSH Web UI 页面不再白底）。
    // ForceDarkMode 会让 Chromium 反色渲染浅色网页，并让
    // ``prefers-color-scheme`` 媒体查询返回 dark。
    // （见 applyForceDarkMode 的版本说明：Qt < 6.7 时为无害空操作。）
    applyForceDarkMode(settings, theme_ && theme_->current() == "dark");

    contentStack_ = new QStackedWidget(this);
    createStartupPage();

    view_ = new QWebEngineView(contentStack_);
    page_ = new dsh::web::LoopbackWebPage(profile_, QUrl(url_), openExternal, view_);
    connect(page_, &QWebEnginePage::permissionRequested, this,
            [this](QWebEnginePermission permission) {
        const bool trustedClipboardRequest =
            permission.permissionType()
                == QWebEnginePermission::PermissionType::ClipboardReadWrite
            && dsh::web::LoopbackWebPage::isSameOrigin(
                permission.origin(), QUrl(url_));
        if (trustedClipboardRequest) {
            permission.grant();
            if (log_) log_(QStringLiteral("web: 已授权同源页面写入剪贴板"));
        } else {
            permission.deny();
        }
    });
    view_->setPage(page_);
    connect(view_, &QWebEngineView::loadFinished,
            this, &DshWindow::onApplicationLoadFinished);
    contentStack_->addWidget(view_);
    contentStack_->setCurrentWidget(startupPage_);
    setCentralWidget(contentStack_);

    // 还原上次窗口几何；首次启动时给出合理默认值
    restorePersistedGeometry();

    view_->setUrl(QUrl(url_));

    if (theme_) {
        connect(theme_, &dsh::theme::ThemeWatcher::schemeChanged,
                this, &DshWindow::onThemeChanged);
    }
}

DshWindow::~DshWindow() = default;

void DshWindow::showAndRaise() {
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void DshWindow::reload() {
    applicationLoadRetries_ = 0;
    showStartupPage(tr("正在重新连接 DSH…"),
                    tr("正在准备您的工作区，请稍候"));
    view_->setUrl(QUrl(url_));
}

QUrl DshWindow::currentUrl() const {
    return view_ ? view_->url() : QUrl(url_);
}

bool DshWindow::clipboardWriteEnabled() const {
    return profile_ && profile_->settings()->testAttribute(
        QWebEngineSettings::JavascriptCanAccessClipboard);
}

void DshWindow::applyLogo(const QIcon& icon, const QString& scheme) {
    appliedLogoTheme_ = scheme;
    setWindowIcon(icon);
    if (windowHandle()) windowHandle()->setIcon(icon);
    if (startupLogo_) startupLogo_->setPixmap(icon.pixmap(QSize(112, 112)));
    applyStartupTheme(scheme);
}

void DshWindow::createStartupPage() {
    startupPage_ = new QWidget(contentStack_);
    startupPage_->setObjectName(QStringLiteral("dshStartupPage"));

    auto* layout = new QVBoxLayout(startupPage_);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(16);
    layout->addStretch(3);

    startupLogo_ = new QLabel(startupPage_);
    startupLogo_->setObjectName(QStringLiteral("dshStartupLogo"));
    startupLogo_->setAlignment(Qt::AlignCenter);
    layout->addWidget(startupLogo_, 0, Qt::AlignHCenter);

    auto* title = new QLabel(QStringLiteral("DSH Desktop"), startupPage_);
    title->setObjectName(QStringLiteral("dshStartupTitle"));
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);

    startupStatus_ = new QLabel(tr("正在启动 DSH…"), startupPage_);
    startupStatus_->setObjectName(QStringLiteral("dshStartupStatus"));
    startupStatus_->setAlignment(Qt::AlignCenter);
    QFont statusFont = startupStatus_->font();
    statusFont.setPointSize(statusFont.pointSize() + 1);
    startupStatus_->setFont(statusFont);
    layout->addWidget(startupStatus_);

    startupDetail_ = new QLabel(tr("正在准备您的工作区，请稍候"), startupPage_);
    startupDetail_->setObjectName(QStringLiteral("dshStartupDetail"));
    startupDetail_->setAlignment(Qt::AlignCenter);
    startupDetail_->setWordWrap(true);
    layout->addWidget(startupDetail_);

    startupProgress_ = new QProgressBar(startupPage_);
    startupProgress_->setObjectName(QStringLiteral("dshStartupProgress"));
    startupProgress_->setRange(0, 0);
    startupProgress_->setTextVisible(false);
    startupProgress_->setFixedSize(240, 4);
    layout->addWidget(startupProgress_, 0, Qt::AlignHCenter);

    startupRetry_ = new QPushButton(tr("重新连接"), startupPage_);
    startupRetry_->setObjectName(QStringLiteral("dshStartupRetry"));
    startupRetry_->setVisible(false);
    startupRetry_->setMinimumWidth(120);
    connect(startupRetry_, &QPushButton::clicked,
            this, &DshWindow::retryApplicationLoad);
    layout->addWidget(startupRetry_, 0, Qt::AlignHCenter);
    layout->addStretch(4);

    contentStack_->addWidget(startupPage_);
    applyStartupTheme(theme_ ? theme_->current() : QStringLiteral("light"));
}

void DshWindow::showStartupPage(const QString& status, const QString& detail) {
    if (!contentStack_ || !startupPage_) return;
    startupStatus_->setText(status);
    startupDetail_->setText(detail);
    startupProgress_->setVisible(true);
    startupRetry_->setVisible(false);
    contentStack_->setCurrentWidget(startupPage_);
}

void DshWindow::applyStartupTheme(const QString& scheme) {
    if (!startupPage_) return;
    const bool dark = scheme == QStringLiteral("dark");
    const QString background = dark ? QStringLiteral("#18181b")
                                    : QStringLiteral("#f7f7f8");
    const QString foreground = dark ? QStringLiteral("#f4f4f5")
                                    : QStringLiteral("#18181b");
    const QString secondary = dark ? QStringLiteral("#a1a1aa")
                                   : QStringLiteral("#71717a");
    const QString track = dark ? QStringLiteral("#3f3f46")
                               : QStringLiteral("#d4d4d8");
    startupPage_->setStyleSheet(QStringLiteral(
        "QWidget#dshStartupPage { background: %1; }"
        "QLabel#dshStartupTitle, QLabel#dshStartupStatus { color: %2; }"
        "QLabel#dshStartupDetail { color: %3; }"
        "QProgressBar#dshStartupProgress { background: %4; border: none; border-radius: 2px; }"
        "QProgressBar#dshStartupProgress::chunk { background: #4f8cff; border-radius: 2px; }"
        "QPushButton#dshStartupRetry { padding: 7px 18px; }")
        .arg(background, foreground, secondary, track));
}

void DshWindow::retryApplicationLoad() {
    applicationLoadRetries_ = 0;
    showStartupPage(tr("正在重新连接 DSH…"),
                    tr("正在准备您的工作区，请稍候"));
    view_->setUrl(QUrl(url_));
}

void DshWindow::closeEvent(QCloseEvent* event) {
    // 隐藏到托盘的语义：只有托盘"退出"菜单才能真正结束进程
    event->ignore();
    savePersistedGeometry();  // 记住关闭时的位置
    hide();
}

void DshWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    if (geometryRestored_) savePersistedGeometry();
}

void DshWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (geometryRestored_) savePersistedGeometry();
}

void DshWindow::hideEvent(QHideEvent* event) {
    QMainWindow::hideEvent(event);
    savePersistedGeometry();
}

void DshWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!geometryRestored_) {
        // 首次显示时再次尝试还原（窗口已可见再做一次屏幕校验）
        ensureVisibleOnScreen();
        geometryRestored_ = true;
    }
}

void DshWindow::restorePersistedGeometry() {
    const QByteArray geom = settings_.value(QStringLiteral("window/geometry")).toByteArray();
    // offscreen 平台没有真实屏幕；给一个稳妥的默认
    QScreen* scr = QGuiApplication::primaryScreen();
    if (!scr) {
        resize(1280, 820);
        geometryRestored_ = true;
        return;
    }
    const QRect screen = scr->availableGeometry();

    // 启发式：检测保存的几何信息是否"过大"（超过主屏 90%）。这种情况
    // 通常是用户曾经最大化到了 xrdp/嵌套显示器的虚拟屏（几千 x 几千），下次
    // 启动会跑出物理屏外导致看不到窗口。一旦发现就丢弃旧几何，走默认。
    bool geometry_reasonable = !geom.isEmpty();
    if (geometry_reasonable) {
        const bool restored = QMainWindow::restoreGeometry(geom);
        if (!restored) {
            geometry_reasonable = false;
        } else {
            const QSize fs = frameGeometry().size();
            if (fs.width() > screen.width() * 0.9
                || fs.height() > screen.height() * 0.9) {
                geometry_reasonable = false;
            }
        }
    }

    if (!geometry_reasonable) {
        // 强制默认 1280x820 + 居中（之前用户拖大到超出物理屏的不要还原）
        const QSize def(1280, 820);
        // 把窗口放到主屏中央且不超出主屏边界
        const int x = screen.x() + (screen.width() - def.width()) / 2;
        const int y = screen.y() + (screen.height() - def.height()) / 2;
        setGeometry(x, y, def.width(), def.height());
        geometryRestored_ = true;
        return;
    }

    // 几何信息合理：再做一次"还在当前屏幕吗"检查
    const QRect fr = frameGeometry();
    if (!screen.intersects(fr)) {
        move(screen.center() - rect().center());
    }
    geometryRestored_ = true;
}

void DshWindow::savePersistedGeometry() {
    const QByteArray geom = QMainWindow::saveGeometry();
    settings_.setValue(QStringLiteral("window/geometry"), geom);
    settings_.sync();
}

void DshWindow::ensureVisibleOnScreen() {
    QScreen* scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const QRect screen = scr->availableGeometry();
    const QRect fr = frameGeometry();
    if (!screen.intersects(fr)) {
        move(screen.center() - rect().center());
    }
}

void DshWindow::onDownloadRequested(QWebEngineDownloadRequest* item) {
    if (!item) return;
    const QUrl url = item->url();
    if (downloads_ && dsh::web::DownloadInterceptor::shouldIntercept(url)) {
        downloads_->handle(item);
        return;
    }
    item->accept();
}

void DshWindow::onApplicationLoadFinished(bool success) {
    const bool applicationOrigin = dsh::web::LoopbackWebPage::isSameOrigin(
        view_->url(), QUrl(url_));
    if (success && applicationOrigin) {
        applicationLoadRetries_ = 0;
        contentStack_->setCurrentWidget(view_);
        return;
    }
    if (!applicationOrigin) return;

    constexpr int kMaximumRetries = 8;
    if (applicationLoadRetries_ >= kMaximumRetries) {
        if (log_) log_(QStringLiteral("web: 页面加载失败，已达到自动重试上限"));
        startupStatus_->setText(tr("DSH 暂时无法启动"));
        startupDetail_->setText(tr("后台服务尚未就绪，请稍后重新连接"));
        startupProgress_->setVisible(false);
        startupRetry_->setVisible(true);
        contentStack_->setCurrentWidget(startupPage_);
        return;
    }
    const int retryNumber = ++applicationLoadRetries_;
    const int delayMs = qMin(500 * (1 << qMin(retryNumber - 1, 3)), 4000);
    showStartupPage(tr("正在启动 DSH…"),
                    tr("正在准备您的工作区，请稍候（%1/%2）")
                        .arg(retryNumber).arg(kMaximumRetries));
    if (log_) {
        log_(QStringLiteral("web: 页面加载失败，%1 ms 后自动重试（%2/%3）")
                 .arg(delayMs).arg(retryNumber).arg(kMaximumRetries));
    }
    QTimer::singleShot(delayMs, this, [this]() {
        if (!view_) return;
        if (dsh::web::LoopbackWebPage::isSameOrigin(view_->url(), QUrl(url_))) {
            view_->reload();
        }
    });
}

void DshWindow::onThemeChanged(const QString& scheme) {
    // 暗色模式切换：更新 Chromium 渲染后重载页面，让 DSH Web UI 跟随。
    //
    // QWebEngineSettings::ForceDarkMode 需要 Qt 6.7 才有。在 Qt 6.5/6.6 上
    // 没有该属性（见 applyForceDarkMode 的说明），此处退化为不做什么，避免
    // 使用不存在的枚举导致编译失败。
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    if (profile_ && view_) {
        const bool dark = (scheme == "dark");
        auto settings = profile_->settings();
        const bool curDark = settings->testAttribute(
            QWebEngineSettings::ForceDarkMode);
        if (curDark != dark) {
            settings->setAttribute(QWebEngineSettings::ForceDarkMode, dark);
            // 页面会收到新的 prefers-color-scheme，重载应用之。
            view_->reload();
        }
    }
#else
    Q_UNUSED(scheme);
#endif
}

}  // namespace dsh::app
