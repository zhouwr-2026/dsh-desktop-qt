// SPDX-License-Identifier: MIT
// @author zhouwr
//
// ``QWebEnginePage`` 子类：拒绝任何离开 loopback 宿主的 http(s) 导航。
// 外部链接通过调用方传入的 opener（一般是
// ``QDesktopServices::openUrl`` -> ``xdg-open``）转发到系统浏览器。

#pragma once

#include <QWebEnginePage>
#include <QUrl>

#include <functional>

namespace dsh::web {

class LoopbackWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    /// \param profile  Profile（需与 view 绑定）。
    /// \param applicationUrl  Web 应用的根 URL，其同源导航视为内部导航。
    /// \param opener   接收外部 URL 的可调用对象。
    explicit LoopbackWebPage(QWebEngineProfile* profile,
                             const QUrl& applicationUrl,
                             std::function<void(const QUrl&)> opener,
                             QObject* parent = nullptr);

    bool acceptNavigationRequest(const QUrl& url,
                                NavigationType type,
                                bool isMainFrame) override;

    /// 纯谓词：判断 URL 是否被视为"内部"。无 Qt 引擎依赖，便于单测。
    static bool isInternal(const QUrl& url,
                           const QUrl& applicationUrl = QUrl());
    static bool isSameOrigin(const QUrl& left, const QUrl& right);

private:
    QUrl applicationUrl_;
    std::function<void(const QUrl&)> opener_;
};

}  // namespace dsh::web
