// SPDX-License-Identifier: MIT
// @author zhouwr
//
// ``QWebEnginePage`` 子类：拒绝任何离开同源宿主的导航，外部链接通过
// opener 转发到系统浏览器；同时静默处理 ``window.alert/confirm/prompt``，
// 避免 Chromium 原生 JS 对话框出现在桌面端。
//
// 自定义 HTML 模态（如 DSH Web UI 的"Session 导出已开始下载"）由
// ``SuppressExportToast`` 在 ``DownloadInterceptor::handle`` 触发时另行清
// 理，不走这里的虚函数。

#pragma once

#include <QString>
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
    /// \param logSink  可选回调：把拦截决策写到日志便于排查。
    explicit LoopbackWebPage(QWebEngineProfile* profile,
                             const QUrl& applicationUrl,
                             std::function<void(const QUrl&)> opener,
                             std::function<void(const QString&)> logSink = nullptr,
                             QObject* parent = nullptr);

    bool acceptNavigationRequest(const QUrl& url,
                                NavigationType type,
                                bool isMainFrame) override;

    /// 纯谓词：判断 URL 是否被视为"内部"。无 Qt 引擎依赖，便于单测。
    static bool isInternal(const QUrl& url,
                           const QUrl& applicationUrl = QUrl());
    static bool isSameOrigin(const QUrl& left, const QUrl& right);

protected:
    void javaScriptAlert(const QUrl& securityOrigin, const QString& msg) override;
    // 安全默认 = 拒绝：返回 false 让页面把 confirm 当用户取消，**不要** auto-accept
    // （那会替用户点破坏性按钮）。
    bool javaScriptConfirm(const QUrl& securityOrigin, const QString& msg) override;
    bool javaScriptPrompt(const QUrl& securityOrigin, const QString& msg,
                          const QString& defaultValue, QString* result) override;

private:
    QUrl applicationUrl_;
    std::function<void(const QUrl&)> opener_;
    std::function<void(const QString&)> logSink_;
};

}  // namespace dsh::web