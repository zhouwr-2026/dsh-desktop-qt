// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 日志查看器：从托盘菜单"查看日志"打开，显示
// ``~/.local/share/dsh-desktop/dsh-desktop.log`` 末尾的内容，
// 方便用户在不打开终端的情况下诊断问题。

#pragma once

#include <QDialog>
#include <QString>

namespace dsh::app {

class LogViewer : public QDialog {
    Q_OBJECT
public:
    /// \param log_path 默认显示的文件路径，留空则使用 XDG 数据目录默认日志。
    explicit LogViewer(const QString& log_path = QString(),
                       QWidget* parent = nullptr);
};

}  // namespace dsh::app