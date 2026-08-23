// SPDX-License-Identifier: MIT
// @author zhouwr
#include "RestartDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsh::app {

RestartDialog::RestartDialog(int activeTasks,
                             const QString& backendUrl,
                             bool supervisedMode,
                             bool backendRunning,
                             bool canManageBackend,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("重启 DSH Desktop"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    if (activeTasks > 0) {
        auto* warn = new QLabel(
            tr("⚠  检测到 <b>%1</b> 个后台任务正在执行，\n"
               "现在重启可能会中断它们。")
                .arg(activeTasks));
        warn->setStyleSheet("color: #d35400; font-weight: 600;");
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    QString message = tr("确定要重启 DSH Desktop 吗？\n\n"
                         "• 当前进程会被替换，托盘会短暂消失\n"
                         "• 新的桌面端启动后会重新连接后端");
    if (canManageBackend) {
        if (backendRunning) {
            message += tr("\n• 勾选下方选项可同时重启后台 dsh web 服务");
        } else {
            message += tr("\n• 后端服务未运行，桌面端会先尝试拉起它");
        }
    } else {
        message += tr("\n• 远程 dsh web 不会被重启");
    }
    auto* msg = new QLabel(message);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    if (canManageBackend) {
        if (backendRunning) {
            checkbox_ = new QCheckBox(
                tr("同时重启后台 dsh web 服务  (%1)").arg(backendUrl));
            if (supervisedMode) {
                checkbox_->setText(
                    tr("同时重启由桌面端拉起的 dsh web 子进程  (%1)").arg(backendUrl));
            }
        } else {
            // 后端未运行：勾选框只是说明性，调用方会无条件拉起；锁定为勾
            // 选状态以清晰传达"无论你是否取消这个勾选，都会启动"的语义。
            checkbox_ = new QCheckBox(
                tr("同时启动 DSH 后台服务  (%1)").arg(backendUrl));
            checkbox_->setChecked(true);
            checkbox_->setEnabled(false);
        }
        layout->addWidget(checkbox_);
    }

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("重启"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}  // namespace dsh::app
