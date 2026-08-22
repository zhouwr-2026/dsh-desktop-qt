// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 更新对话框：显示当前 / 最新版本号，提供"更新到最新版"按钮（通过
// ``pkexec npm install -g`` 执行），实时展示 pkexec 的 stdout/stderr。

#pragma once

#include <QDialog>

#include "../updater/Updater.h"

QT_BEGIN_NAMESPACE
class QPushButton;
class QTextEdit;
QT_END_NAMESPACE

namespace dsh::app {

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(dsh::updater::Status status, QWidget* parent = nullptr);

private slots:
    void onUpdate();
    void onLog(const QString& line);
    void onUpdateFinished(bool ok);

private:
    dsh::updater::Status status_;
    QTextEdit* log_{nullptr};
    QPushButton* updateButton_{nullptr};
};

}  // namespace dsh::app
