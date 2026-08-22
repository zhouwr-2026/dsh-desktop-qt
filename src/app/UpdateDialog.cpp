// SPDX-License-Identifier: MIT
// @author zhouwr
#include "UpdateDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

namespace dsh::app {

UpdateDialog::UpdateDialog(dsh::updater::Status status, QWidget* parent)
    : QDialog(parent), status_(status) {
    setWindowTitle(tr("DSH 更新"));
    setMinimumWidth(500);

    auto* layout = new QVBoxLayout(this);

    const QString currentVersion = status_.current.isEmpty()
        ? tr("未知") : status_.current.toHtmlEscaped();
    auto* cur = new QLabel(tr("当前版本：<b>%1</b>").arg(currentVersion));
    cur->setTextFormat(Qt::RichText);
    layout->addWidget(cur);

    const QString latestVersion = status_.latest.isEmpty()
        ? tr("未知") : status_.latest.toHtmlEscaped();
    auto* lat = new QLabel(tr("最新版本：<b>%1</b>").arg(latestVersion));
    lat->setTextFormat(Qt::RichText);
    layout->addWidget(lat);

    QString stateLabel;
    if (status_.updateAvailable) {
        stateLabel = tr("<span style='color:#27ae60;font-weight:600;'>有新版本可用，点击下方按钮立即更新。</span>");
    } else if (status_.detail.startsWith(QStringLiteral("离线"))) {
        stateLabel = tr("<span style='color:#d35400;'>无法连接 npm 注册表，请稍后重试。</span>");
    } else {
        stateLabel = tr("<span style='color:#27ae60;'>已是最新版本。</span>");
    }
    auto* state = new QLabel(stateLabel);
    state->setTextFormat(Qt::RichText);
    layout->addWidget(state);

    log_ = new QTextEdit();
    log_->setReadOnly(true);
    log_->setMinimumHeight(160);
    log_->hide();
    layout->addWidget(log_);

    auto* buttons = new QDialogButtonBox();
    updateButton_ = buttons->addButton(tr("更新到最新版"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
    updateButton_->setEnabled(status_.updateAvailable);
    connect(buttons, &QDialogButtonBox::accepted, this, &UpdateDialog::onUpdate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void UpdateDialog::onUpdate() {
    updateInProgress_ = true;
    log_->show();
    updateButton_->setEnabled(false);

    // 走工作线程，避免阻塞 UI 刷新日志
    auto* thread = new QThread();
    auto* updater = new dsh::updater::Updater();
    updater->setTargetVersion(status_.latest);
    updater->moveToThread(thread);
    connect(thread, &QThread::started,
            updater, &dsh::updater::Updater::performUpdateAsync);
    connect(updater, &dsh::updater::Updater::log,
            this, &UpdateDialog::onLog);
    connect(updater, &dsh::updater::Updater::updateFinished,
            this, &UpdateDialog::onUpdateFinished);
    connect(updater, &dsh::updater::Updater::updateFinished,
            thread, &QThread::quit);
    connect(updater, &dsh::updater::Updater::updateFinished,
            updater, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void UpdateDialog::onLog(const QString& line) {
    log_->append(line);
}

void UpdateDialog::onUpdateFinished(bool ok) {
    updateInProgress_ = false;
    log_->append(ok ? tr("更新完成。") : tr("更新失败，请检查上方日志。"));
}

void UpdateDialog::reject() {
    if (updateInProgress_) {
        log_->append(tr("更新正在进行，完成或超时后才能关闭窗口。"));
        return;
    }
    QDialog::reject();
}

}  // namespace dsh::app
