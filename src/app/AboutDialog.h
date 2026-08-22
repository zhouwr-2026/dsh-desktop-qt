// SPDX-License-Identifier: MIT
// @author zhouwr
//
// "关于 DSH Desktop" 对话框：显示版本、构建信息、官方链接、致谢。

#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QIcon;
class QLabel;
QT_END_NAMESPACE

namespace dsh::app {

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
    void applyLogo(const QIcon& icon, const QString& scheme);
    QString appliedLogoTheme() const { return appliedLogoTheme_; }

private:
    QLabel* logoLabel_{nullptr};
    QString appliedLogoTheme_{QStringLiteral("light")};
};

}  // namespace dsh::app
