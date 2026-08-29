// SPDX-License-Identifier: MIT
// @author zhouwr
//
// LoopbackWebPage — 对内嵌 DSH Web UI 强化 Chromium 弹窗默认行为。
//
// 对所有内嵌页面 JS confirm / prompt 一律自动拒绝，alert 静默吞掉（不弹
// 任何原生模态）；这是 ``isSameOrigin`` + 仅允许 ``http(s)://loopback`` 内嵌
// 页面策略的安全默认，避免破坏性操作（如删除会话）被静默放行。
// ``acceptNavigationRequest`` 同样拒绝非 loopback 的导航请求。
// (变更理由: 安全审查 L-6)
#include "LoopbackWebPage.h"

#include <QWebEngineNewWindowRequest>

namespace dsh::web {

namespace {

int effectivePort(const QUrl& url) {
    if (url.port() >= 0) return url.port();
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) return 80;
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) return 443;
    return -1;
}

bool isLoopbackHost(const QString& host) {
    const QString normalized = host.toLower();
    return normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("::1");
}

}  // namespace

bool LoopbackWebPage::isSameOrigin(const QUrl& left, const QUrl& right) {
    return left.isValid() && right.isValid()
        && left.scheme().compare(right.scheme(), Qt::CaseInsensitive) == 0
        && left.host().compare(right.host(), Qt::CaseInsensitive) == 0
        && effectivePort(left) == effectivePort(right);
}

LoopbackWebPage::LoopbackWebPage(QWebEngineProfile* profile,
                                 const QUrl& applicationUrl,
                                 std::function<void(const QUrl&)> opener,
                                 std::function<void(const QString&)> logSink,
                                 QObject* parent)
    : QWebEnginePage(profile, parent), applicationUrl_(applicationUrl),
      opener_(std::move(opener)), logSink_(std::move(logSink)) {
    connect(this, &QWebEnginePage::newWindowRequested, this,
            [this](QWebEngineNewWindowRequest& request) {
        const QUrl url = request.requestedUrl();
        if (!url.isValid()) return;
        if (isInternal(url, applicationUrl_)) {
            setUrl(url);
            return;
        }
        const QString scheme = url.scheme().toLower();
        const bool external =
            scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto") || scheme == QStringLiteral("tel");
        if (external && opener_) opener_(url);
    });
}

void LoopbackWebPage::javaScriptAlert(const QUrl& securityOrigin,
                                      const QString& msg) {
    if (logSink_) {
        logSink_(QStringLiteral("web: 静默吞掉 JS alert，origin=%1 msg=%2")
                     .arg(securityOrigin.toString(), msg));
    }
    // 不弹任何对话框；空实现即视为"用户已关闭"。
}

bool LoopbackWebPage::javaScriptConfirm(const QUrl& securityOrigin,
                                        const QString& msg) {
    if (logSink_) {
        logSink_(QStringLiteral("web: 静默拒绝 JS confirm（C++ 当作用户点了取消），"
                                 "origin=%1 msg=%2")
                     .arg(securityOrigin.toString(), msg));
    }
    // **安全默认 = 拒绝**（用户没有真正看到/确认原生对话框）。返回 false
    // 让页面把 confirm 当作"用户取消"，避免静默放行删除/导航等破坏性动
    // 作——自动接受曾导致嵌入式 webview 焦点被换走 / DOM 节点被替换，进
    // 而把输入法的组合上下文打断（用户反馈的 IME 中英文切换失效）。如果
    // DSH Web UI 后续确实依赖 confirm 自动推进，请用更具体的文案判断后
    // 再决定接受/拒绝，不要再走无条件 auto-accept。
    return false;
}

bool LoopbackWebPage::javaScriptPrompt(const QUrl& securityOrigin,
                                       const QString& msg,
                                       const QString& defaultValue,
                                       QString* result) {
    if (logSink_) {
        logSink_(QStringLiteral("web: 静默拒绝 JS prompt，origin=%1 msg=%2")
                     .arg(securityOrigin.toString(), msg));
    }
    Q_UNUSED(defaultValue);
    if (result) *result = QString();
    return false;
}

bool LoopbackWebPage::isInternal(const QUrl& url, const QUrl& applicationUrl) {
    if (!url.isValid()) return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("about")) {
        return url == QUrl(QStringLiteral("about:blank"));
    }
    if (scheme == QStringLiteral("blob")) {
        const QString encoded = url.toString(QUrl::FullyEncoded);
        const QUrl originUrl(encoded.mid(QStringLiteral("blob:").size()));
        if (applicationUrl.isValid() && !applicationUrl.isEmpty()) {
            return isSameOrigin(originUrl, applicationUrl);
        }
        return (originUrl.scheme() == QStringLiteral("http")
                || originUrl.scheme() == QStringLiteral("https"))
            && isLoopbackHost(originUrl.host());
    }
    if (applicationUrl.isValid() && !applicationUrl.isEmpty()) {
        return isSameOrigin(url, applicationUrl);
    }
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        && isLoopbackHost(url.host());
}

bool LoopbackWebPage::acceptNavigationRequest(const QUrl& url,
                                             NavigationType type,
                                             bool isMainFrame) {
    Q_UNUSED(type);
    Q_UNUSED(isMainFrame);
    if (isInternal(url, applicationUrl_)) return true;
    // 内部导航被拒。http/https/mailto/tel 走系统浏览器；其他 scheme
    // （magnet: / javascript: / data: 等）一律拒绝。
    const QString scheme = url.scheme().toLower();
    const bool external =
        scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("mailto") || scheme == QStringLiteral("tel");
    if (external && opener_) opener_(url);
    return false;
}

}  // namespace dsh::web
