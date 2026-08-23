// SPDX-License-Identifier: MIT
// @author zhouwr
#include "UpdateDialog.h"

#include "../updater/DesktopReleaseDownloader.h"
#include "../updater/Updater.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

namespace dsh::app {

namespace {

/// 让一个已下载的安装包变为可执行（DesktopUpdateHelper::validateSource 要求
/// 源文件是可执行文件）。不改动文件内容，不影响其 SHA-256。
/// \return 成功返回 ``true``；设定失败返回 ``false``。
bool makeExecutable(const QString& path) {
    QFile::Permissions perms = QFile::permissions(path);
    perms |= QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther;
    return QFile::setPermissions(path, perms);
}

}  // namespace

UpdateDialog::UpdateDialog(
    const dsh::updater::UpdatePlan& plan,
    QWidget* parent,
    std::function<bool(const QString&, const QString&)> desktopUpdate)
    : QDialog(parent),
      plan_(plan),
      desktopUpdate_(std::move(desktopUpdate)) {
    setWindowTitle(tr("DSH 更新"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);

    stateLabel_ = new QLabel();
    stateLabel_->setTextFormat(Qt::RichText);
    if (plan_.trayActionVisible()) {
        const QVector<dsh::updater::UpdateComponent> selected = plan_.defaultSelected();
        QStringList names;
        for (const dsh::updater::UpdateComponent c : selected) {
            names.append(dsh::updater::componentLabel(c));
        }
        stateLabel_->setText(
            tr("<span style='color:#27ae60;font-weight:600;'>有新版本可用：%1。</span>")
                .arg(names.join(tr("、"))));
    } else {
        stateLabel_->setText(
            tr("<span style='color:#27ae60;'>所有组件已是最新版本。</span>"));
    }
    layout->addWidget(stateLabel_);

    // 组件选择区：后端优先放置，每行一个复选框 + 明细。
    auto addComponentRow =
        [this, layout](dsh::updater::UpdateComponent component) {
            const dsh::updater::ComponentUpdate* update =
                plan_.component(component);
            if (!update) return;
            auto* row = new QHBoxLayout();
            auto* checkbox = new QCheckBox(dsh::updater::componentLabel(component));
            auto* detail = new QLabel(dsh::updater::componentDetail(*update));
            detail->setTextFormat(Qt::RichText);
            detail->setWordWrap(true);
            if (update->state == dsh::updater::ComponentState::Available) {
                checkbox->setChecked(true);
                checkbox->setEnabled(true);
            } else {
                checkbox->setChecked(false);
                checkbox->setEnabled(false);
                QString text = detail->text();
                text = tr("<span style='color:#888;'>%1</span>").arg(text);
                detail->setText(text);
            }
            row->addWidget(checkbox);
            row->addWidget(detail, 1);
            layout->addLayout(row);
            if (component == dsh::updater::UpdateComponent::Backend) {
                backendCheck_ = checkbox;
            } else if (component == dsh::updater::UpdateComponent::Desktop) {
                desktopCheck_ = checkbox;
            }
        };
    addComponentRow(dsh::updater::UpdateComponent::Backend);
    addComponentRow(dsh::updater::UpdateComponent::Desktop);

    layout->addSpacing(6);

    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, 0);  // 不定量（忙碌）进度：操作期间不伪造百分比
    progressBar_->setVisible(false);
    layout->addWidget(progressBar_);

    log_ = new QTextEdit();
    log_->setReadOnly(true);
    log_->setMinimumHeight(140);
    log_->hide();
    layout->addWidget(log_);

    auto* buttons = new QDialogButtonBox();
    updateButton_ = buttons->addButton(tr("更新到最新版"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
    updateButton_->setEnabled(plan_.trayActionVisible());
    connect(buttons, &QDialogButtonBox::accepted, this, &UpdateDialog::onUpdate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void UpdateDialog::onUpdate() {
    if (updateInProgress_) return;
    collectSelectedComponents();
    if (pending_.isEmpty()) return;

    updateInProgress_ = true;
    updateButton_->setEnabled(false);
    log_->show();
    progressBar_->setVisible(true);
    log_->append(tr("开始执行更新（后端优先）…"));
    processNextStep();
}

void UpdateDialog::collectSelectedComponents() {
    pending_.clear();
    // 按计划存放顺序（后端优先）收集被勾选的组件。
    for (const dsh::updater::ComponentUpdate& update : plan_.components()) {
        if (update.state != dsh::updater::ComponentState::Available) continue;
        const bool checked =
            (update.component == dsh::updater::UpdateComponent::Backend)
                ? (backendCheck_ && backendCheck_->isChecked())
                : (desktopCheck_ && desktopCheck_->isChecked());
        if (checked) {
            pending_.append(update.component);
        }
    }
}

void UpdateDialog::processNextStep() {
    if (pending_.isEmpty()) {
        finishWithMessage(tr("更新完成。"));
        return;
    }
    const dsh::updater::UpdateComponent component = pending_.takeFirst();
    switch (component) {
        case dsh::updater::UpdateComponent::Backend:
            runBackendUpdate();
            break;
        case dsh::updater::UpdateComponent::Desktop:
            runDesktopUpdate();
            break;
    }
}

void UpdateDialog::runBackendUpdate() {
    const dsh::updater::ComponentUpdate* update =
        plan_.component(dsh::updater::UpdateComponent::Backend);
    if (!update || update->state != dsh::updater::ComponentState::Available) {
        failUpdate(tr("后端组件当前不可用。"));
        return;
    }

    backendRunning_ = true;
    log_->append(tr("正在更新 DSH 后台服务（目标 %1）…").arg(update->target));

    // 复用 Updater 的异步提权更新：独立线程，不阻塞 GUI。
    auto* thread = new QThread();
    auto* updater = new dsh::updater::Updater();
    updater->setTargetVersion(update->target);
    updater->moveToThread(thread);
    connect(thread, &QThread::started,
            updater, &dsh::updater::Updater::performUpdateAsync);
    connect(updater, &dsh::updater::Updater::log,
            this, &UpdateDialog::onLog);
    connect(updater, &dsh::updater::Updater::updateFinished,
            this, &UpdateDialog::onBackendUpdateFinished);
    connect(updater, &dsh::updater::Updater::updateFinished,
            thread, &QThread::quit);
    connect(updater, &dsh::updater::Updater::updateFinished,
            updater, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void UpdateDialog::onBackendUpdateFinished(bool ok) {
    backendRunning_ = false;
    if (!ok) {
        failUpdate(tr("后端更新失败，请查看上方日志。"));
        return;
    }
    log_->append(tr("后端更新完成。"));
    processNextStep();
}

void UpdateDialog::runDesktopUpdate() {
    using dsh::updater::ComponentState;
    using dsh::updater::DesktopReleaseDownloader;

    const dsh::updater::ComponentUpdate* update =
        plan_.component(dsh::updater::UpdateComponent::Desktop);
    if (!update || update->state != ComponentState::Available) {
        failUpdate(tr("桌面组件当前不可用。"));
        return;
    }
    if (!desktopUpdate_) {
        failUpdate(tr("桌面自更新回调用未提供，无法接管。"));
        return;
    }

    // 遵循检查时锁定的发布信息，选择当前平台的最佳可下载附件。
    const dsh::updater::DesktopReleaseAsset* asset =
        DesktopReleaseDownloader::selectBestAsset(update->release.assets);
    if (!asset) {
        failUpdate(tr("桌面更新包不可用：发布中没有适合当前平台的附件。"));
        return;
    }

    log_->append(tr("正在下载桌面更新包：%1").arg(asset->name));

    auto* downloader = new DesktopReleaseDownloader(this);
    desktopDownloader_ = downloader;
    connect(downloader, &DesktopReleaseDownloader::stageChanged, this,
            [this](const QString& stage) { onLog(tr("下载阶段：%1").arg(stage)); });
    connect(downloader, &DesktopReleaseDownloader::finished, this,
            [this](const dsh::updater::DesktopDownloadResult& result) {
                desktopDownloader_ = nullptr;
                if (!result.ok()) {
                    failUpdate(tr("桌面更新包下载失败：%1").arg(result.error));
                    return;
                }
                // 校验与替换需要源文件可执行，但不能改动其内容（不影响 SHA-256）。
                if (!makeExecutable(result.cachedPath)) {
                    failUpdate(tr("无法将桌面更新包设为可执行：%1")
                                   .arg(result.cachedPath));
                    return;
                }
                log_->append(tr("桌面更新包已下载并校验 SHA-256。"));

                if (desktopUpdate_ && desktopUpdate_(result.cachedPath, result.sha256)) {
                    // 宿主应用已接管：拉起自更新助手、释放单实例锁并准备退出，
                    // 过程中不停止后台服务。对话框随即关闭。
                    updateInProgress_ = false;
                    accept();
                    return;
                }
                failUpdate(tr("无法接管桌面自更新：安装助手不可用或校验失败。"));
            });
    if (!downloader->start(*asset)) {
        failUpdate(tr("无法开始桌面更新包下载：%1").arg(downloader->lastError()));
    }
}

void UpdateDialog::failUpdate(const QString& message) {
    updateInProgress_ = false;
    backendRunning_ = false;
    if (desktopDownloader_) {
        desktopDownloader_->deleteLater();
        desktopDownloader_ = nullptr;
    }
    log_->append(tr("<span style='color:#d35400;'>%1</span>").arg(message));
    progressBar_->setVisible(false);
    // 允许用户关闭对话框或重试剩余步骤。
    updateButton_->setEnabled(plan_.trayActionVisible());
}

void UpdateDialog::finishWithMessage(const QString& message) {
    updateInProgress_ = false;
    backendRunning_ = false;
    log_->append(message);
    progressBar_->setVisible(false);
    // 完成后不再允许重复执行本轮更新。
    updateButton_->setEnabled(false);
    if (backendCheck_) backendCheck_->setEnabled(false);
    if (desktopCheck_) desktopCheck_->setEnabled(false);
}

void UpdateDialog::onLog(const QString& line) {
    if (log_) log_->append(line);
}

void UpdateDialog::reject() {
    if (updateInProgress_ || backendRunning_) {
        log_->append(tr("更新正在进行，完成或超时后才能关闭窗口。"));
        return;
    }
    QDialog::reject();
}

}  // namespace dsh::app
