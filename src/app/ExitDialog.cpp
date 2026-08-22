// SPDX-License-Identifier: MIT
// @author zhouwr
#include "ExitDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsh::app {

ExitDialog::ExitDialog(int activeTasks,
                       const QString& backendUrl,
                       bool supervisedMode,
                       bool canStopBackend,
                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("退出 DSH Desktop"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    if (activeTasks > 0) {
        auto* warn = new QLabel(
            tr("⚠  检测到 <b>%1</b> 个后台任务正在执行，\n"
               "现在退出可能会中断它们。")
                .arg(activeTasks));
        warn->setStyleSheet("color: #d35400; font-weight: 600;");
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    QString message = tr("确定要退出 DSH Desktop 吗？\n\n"
                         "• 托盘菜单会消失\n"
                         "• 主窗口将被关闭");
    if (canStopBackend) {
        message += tr("\n• 勾选下方选项可同时停止后台 dsh web 服务");
    } else {
        message += tr("\n• 远程 dsh web 不会被停止");
    }
    auto* msg = new QLabel(message);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    if (canStopBackend) {
        checkbox_ = new QCheckBox(
            tr("同时停止后台 dsh web 服务  (%1)").arg(backendUrl));
        if (supervisedMode) {
            checkbox_->setText(
                tr("同时停止由桌面端拉起的 dsh web 子进程  (%1)").arg(backendUrl));
        }
        layout->addWidget(checkbox_);
    }

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("退出"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}  // namespace dsh::app
