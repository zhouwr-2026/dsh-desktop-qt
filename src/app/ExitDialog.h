// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 原生退出确认对话框：
//   * 确认 / 取消两个按钮
//   * 后端有活跃任务时高亮提示（橙色文字）
//   * 复选框：是否同时停止后台 dsh web 服务

#pragma once

#include <QCheckBox>
#include <QDialog>

namespace dsh::app {

class ExitDialog : public QDialog {
    Q_OBJECT
public:
    /// \param activeTasks    后端探测到的活跃任务数。
    /// \param backendUrl     后端 URL，用于提示用户。
    /// \param supervisedMode true 时勾选框文案改为"停止由桌面端拉起的子进程"。
    explicit ExitDialog(int activeTasks,
                        const QString& backendUrl,
                        bool supervisedMode,
                        bool canStopBackend,
                        QWidget* parent = nullptr);

    /// 用户是否勾选了"同时停止后台服务"。
    bool stopBackgroundService() const {
        return checkbox_ && checkbox_->isChecked();
    }

private:
    QCheckBox* checkbox_{nullptr};
};

}  // namespace dsh::app
