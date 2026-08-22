// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <iostream>

namespace dsh::util {

namespace {
QMutex g_log_mutex;  // 多线程下避免输出交错
}

Logger::Logger(QObject* parent) : QObject(parent) {
    file_path_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/dsh-desktop.log");
}
Logger::~Logger() = default;

void Logger::setFile(const QString& path) {
    file_path_ = path;
}

void Logger::log(const QString& message) {
    QMutexLocker lock(&g_log_mutex);
    const QString stamped = QStringLiteral("[dsh-desktop %1] %2")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                                .arg(message);
    // 始终写一份到 stderr，方便 journalctl / 终端调试
    std::cerr << stamped.toLocal8Bit().constData() << '\n';
    std::cerr.flush();
    // 镜像到文件（如果设置了）
    if (!file_path_.isEmpty()) {
        QDir().mkpath(QFileInfo(file_path_).absolutePath());
        QFile f(file_path_);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << stamped << '\n';
        }
    }
}

}  // namespace dsh::util
