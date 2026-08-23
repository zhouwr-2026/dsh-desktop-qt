// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 统一更新对话框：把后端（npm）与桌面（Gitee）两个组件的检查结果合并显示，
// 勾选要更新的组件并串行执行（后端优先）。更新动作携带检查时锁定的目标版本，
// 不做二次联网探测，避免目标版本在确认后漂移。

#pragma once

#include <QDialog>

#include <functional>

#include "../updater/UpdatePlan.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
QT_END_NAMESPACE

namespace dsh::updater {
class DesktopReleaseDownloader;
}  // namespace dsh::updater

namespace dsh::app {

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    /// \param plan          合并后的更新计划（来自一次更新检查）。
    /// \param parent        父窗口。
    /// \param desktopUpdate 桌面自更新接管回调：给出已下载包路径与 SHA-256，
    ///                      由宿主应用拉起 self-update 助手、释放单实例锁并退出
    ///                      且不停止后台服务。返回 ``true`` 表示已成功接管
    ///                      （应用即将退出）；返回 ``false`` 表示无法接管，
    ///                      对话框应显示失败而不替换任何东西。
    explicit UpdateDialog(
        const dsh::updater::UpdatePlan& plan,
        QWidget* parent = nullptr,
        std::function<bool(const QString& sourcePath, const QString& sha256)>
            desktopUpdate = {});
    void reject() override;

private slots:
    void onUpdate();
    void onBackendUpdateFinished(bool ok);
    void onLog(const QString& line);

private:
    /// 单选一个组件（按其状态是否可勾选）并构建勾选顺序。
    void collectSelectedComponents();
    /// 执行下一个步骤；全部完成时收尾。
    void processNextStep();
    /// 后端组件更新（在独立线程中复用 ``Updater`` 的异步更新）。
    void runBackendUpdate();
    /// 桌面组件更新：下载选中资产，成功后交给 ``desktopUpdate`` 接管。
    void runDesktopUpdate();

    /// 显示一条失败信息并结束更新流程（不再继续后续组件）。
    void failUpdate(const QString& message);
    /// 结束更新流程：恢复按钮并保持在对话框内，等待用户关闭。
    void finishWithMessage(const QString& message);

    dsh::updater::UpdatePlan plan_;
    std::function<bool(const QString&, const QString&)> desktopUpdate_;
    QVector<dsh::updater::UpdateComponent> pending_;  // 待执行的组件顺序（后端优先）

    QCheckBox* backendCheck_{nullptr};
    QCheckBox* desktopCheck_{nullptr};
    QLabel* stateLabel_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QTextEdit* log_{nullptr};
    QPushButton* updateButton_{nullptr};

    bool updateInProgress_{false};
    bool backendRunning_{false};
    /// 当前桌面更新包下载器（child of ``this``，下载完成或对话框销毁时释放）。
    dsh::updater::DesktopReleaseDownloader* desktopDownloader_{nullptr};
};

}  // namespace dsh::app
