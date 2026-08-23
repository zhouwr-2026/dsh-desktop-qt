// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 原生重启确认对话框。
//
// 职责与 ExitDialog 对称但文案/按钮/勾选语义不同：
//   * 标题："重启 DSH Desktop"
//   * 主按钮："重启"，副按钮："取消"
//   * 后端有活跃任务时高亮提示（橙色文字）
//   * 复选框：是否同时处理后台 dsh web 服务
//
// 复选框语义由调用方根据后端状态解读：
//   - 后端运行中 + 勾选   → 调用方重启后端
//   - 后端运行中 + 不勾选 → 调用方保持后端运行
//   - 后端未运行（任何勾选）→ 调用方始终拉起后端（必要时自动安装）
//
// 后端由外部管理（``canManageBackend=false``，例如远程后端）时不渲染勾选。
//
// 与 ExitDialog 各自独立（用户偏好），不复用基类，避免后者退化为
// if-else 垃圾桶。

#pragma once

#include <QCheckBox>
#include <QDialog>

namespace dsh::app {

class RestartDialog : public QDialog {
    Q_OBJECT
public:
    /// \param activeTasks      后端探测到的活跃任务数（>0 时显示橙色警告）。
    /// \param backendUrl       后端 URL，用于提示用户。
    /// \param supervisedMode   true 时勾选框文案改为"重启由桌面端拉起的
    ///                         dsh web 子进程"。
    /// \param backendRunning   后端当前是否在运行；影响勾选框的展示与含义。
    /// \param canManageBackend false 时不渲染勾选（远程后端 / 不可管理）。
    explicit RestartDialog(int activeTasks,
                           const QString& backendUrl,
                           bool supervisedMode,
                           bool backendRunning,
                           bool canManageBackend,
                           QWidget* parent = nullptr);

    /// 用户是否勾选了"同时处理后台服务"。
    ///
    /// 注：调用方还需结合 ``backendRunning`` 决定动作（详见类注释）。
    bool restartBackgroundService() const {
        return checkbox_ && checkbox_->isChecked();
    }

private:
    QCheckBox* checkbox_{nullptr};
};

}  // namespace dsh::app
