// SPDX-License-Identifier: MIT
// @author zhouwr
#include "AboutDialog.h"

#include "BuildVersion.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>

namespace dsh::app {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("关于 DSH Desktop"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);

    // 头部鲸鱼跟随当前主题：暗色→白鲸鱼，亮色→黑鲸鱼（与托盘/窗口一致）。
    logoLabel_ = new QLabel();
    logoLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel_);

    auto* title = new QLabel(tr("<h2>DSH Desktop</h2>"
                                "<p>DeepSeek Harness 在 KDE Plasma 6 上的原生包装器</p>"));
    title->setAlignment(Qt::AlignCenter);
    title->setTextFormat(Qt::RichText);
    layout->addWidget(title);

    auto* body = new QTextBrowser();
    body->setOpenExternalLinks(true);
    body->setHtml(QStringLiteral(
        "<p style='text-align:center;'>"
        "<b>版本：</b>%1"
        "&nbsp;·&nbsp;<b>作者：</b>zhouwr"
        "</p>"
        "<p style='text-align:center;'>"
        "官方仓库：<a href='https://github.com/deepseek-ai/deepseek-harness'>deepseek-ai/deepseek-harness</a><br/>"
        "本仓库参考：<a href='https://github.com/anywhere-labs/deepseek-harness-desktop'>anywhere-labs/deepseek-harness-desktop</a>"
        "</p>"
        "<hr/>"
        "<p><b>技术栈</b><br/>"
        "• C++17 + Qt 6（与 KDE Plasma 6 同源）<br/>"
        "• QWebEngine 渲染官方 DSH Web 全部 UI 与插件<br/>"
        "• QSystemTrayIcon + KDE StatusNotifierItem 协议<br/>"
        "• org.freedesktop.Notifications D-Bus 通知<br/>"
        "• systemd dsh-web.service 集成 + polkit 授权升级</p>"
        "<p><b>功能</b><br/>"
        "• 常驻托盘 + 6 项菜单（含动态『更新到最新版』按钮）<br/>"
        "• 主题自适应：跟随 KDE 亮/暗模式切换黑白鲸鱼图标<br/>"
        "• 会话日志下载：弹原生保存对话框 + 进度条 + 完成通知<br/>"
        "• 外部链接拦截：自动用系统默认浏览器打开<br/>"
        "• 启动失败诊断、单实例锁、XDG 标准下载路径、自启动集成</p>"
        "<p style='font-size:small;color:gray;'>许可协议：MIT</p>"
    ).arg(QString::fromLatin1(DSH_DESKTOP_VERSION)));
    layout->addWidget(body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

void AboutDialog::applyLogo(const QIcon& icon, const QString& scheme) {
    appliedLogoTheme_ = scheme;
    setWindowIcon(icon);
    logoLabel_->setPixmap(icon.pixmap(QSize(96, 96)));
}

}  // namespace dsh::app
