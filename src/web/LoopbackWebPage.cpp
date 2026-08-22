// SPDX-License-Identifier: MIT
// @author zhouwr
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
                                 QObject* parent)
    : QWebEnginePage(profile, parent), applicationUrl_(applicationUrl),
      opener_(std::move(opener)) {
    connect(this, &QWebEnginePage::newWindowRequested, this,
            [this](QWebEngineNewWindowRequest& request) {
        const QUrl url = request.requestedUrl();
        if (!url.isValid()) return;
        if (isInternal(url, applicationUrl_)) {
            setUrl(url);
            return;
        }
        const QString scheme = url.scheme().toLower();
        if ((scheme == "http" || scheme == "https") && opener_) opener_(url);
    });
}

bool LoopbackWebPage::isInternal(const QUrl& url, const QUrl& applicationUrl) {
    // data:, blob:, about:, file:, javascript:, mailto:, tel: stay inside.
    const QString scheme = url.scheme().toLower();
    if (scheme == "data" || scheme == "blob" || scheme == "about" ||
        scheme == "file" || scheme == "javascript" || scheme == "mailto" ||
        scheme == "tel")
        return true;
    if (isSameOrigin(url, applicationUrl)) return true;
    const bool remoteApplication = applicationUrl.isValid()
        && !applicationUrl.host().isEmpty()
        && !isLoopbackHost(applicationUrl.host());
    if (!remoteApplication && isLoopbackHost(url.host())) return true;
    return false;
}

bool LoopbackWebPage::acceptNavigationRequest(const QUrl& url,
                                             NavigationType type,
                                             bool isMainFrame) {
    Q_UNUSED(type);
    Q_UNUSED(isMainFrame);
    if (isInternal(url, applicationUrl_)) return true;
    if (url.scheme().toLower() == "http" || url.scheme().toLower() == "https") {
        if (opener_) opener_(url);
        return false;  // block the in-viewport navigation
    }
    return false;  // refuse all other external schemes too (magnet:, etc.)
}

}  // namespace dsh::web
