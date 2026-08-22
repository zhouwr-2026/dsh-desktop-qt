// SPDX-License-Identifier: MIT
// @author zhouwr
// 极简的 stderr / 文件日志器，供 DSH Desktop 主进程使用。
//
// 每行日志都会带上时间戳并以 ``[dsh-desktop <时间>] <消息>`` 的格式输出，
// 便于排错时在 journal 中 grep。同时也可以镜像写入到 ``--log-file`` 指
// 定的文件，默认写到 Qt ``AppDataLocation`` 下的 ``dsh-desktop.log``，跨
// 会话保留诊断信息。

#pragma once

#include <QObject>
#include <QString>

namespace dsh::util {

class Logger : public QObject {
    Q_OBJECT
public:
    explicit Logger(QObject* parent = nullptr);
    ~Logger() override;

    /// 设置可选的日志镜像文件路径。
    void setFile(const QString& path);

    /// 写一行日志。会自动补上时间戳与换行。
    void log(const QString& message);

    /// 便捷方法：记录并返回原字符串，便于链式调用。
    QString operator()(const QString& message) {
        log(message);
        return message;
    }

private:
    QString file_path_;
};

}  // namespace dsh::util
