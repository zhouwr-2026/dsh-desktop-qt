// SPDX-License-Identifier: MIT
// @author zhouwr
//
// ``dsh-desktop-updater`` —— DSH Desktop 自更新助手独立可执行文件。
//
// 由正在更新的旧实例（或安装器）在退出前拉起，职责单一：
//   1. 启动时恢复上次更新崩溃遗留的孤儿 ``.dsh-update-*.bak/tmp`` 文件；
//   2. 等待旧 PID 退出（有界超时，避免无限挂起）；
//   3. 校验来源文件与目标路径（目标须为已安装桌面二进制或位于可信安装前缀内，
//      且文件主与当前用户一致，见 ``validateInstallDestination``）；
//   4. 校验来源 SHA-256；
//   5. 保留目标权限并原子替换；
//   6. 任何一步失败都以非零退出码 + 清晰的 stderr 说明原因。
//
// 刻意不调用 shell、systemctl 或 QProcess —— 自更新助手必须在主程序可能已
// 损坏的情况下依然可靠，因此这是一个最小依赖（仅 Qt Core）的原生二进。

#include "DesktopUpdateHelper.h"

#include "BuildVersion.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>

#include <cerrno>
#include <csignal>
#include <cstdio>

using dsh::updater::atomicReplace;
using dsh::updater::computeSha256;
using dsh::updater::isValidSha256Hex;
using dsh::updater::recoverOrphanedDshUpdateFiles;
using dsh::updater::validateInstallDestination;
using dsh::updater::validateSource;
using dsh::updater::verifySha256;

namespace {

/// 等待旧进程退出的有界超时（毫秒）。
constexpr int kWaitTimeoutMs = 120000;
/// 轮询间隔（毫秒）。
constexpr int kPollIntervalMs = 200;

/// 判断进程是否仍然存在。
/// ``kill(pid, 0)`` 返回 0 表示存在且有权限；返回 -1 且 errno=EPERM 表示存在但无权限。
bool processAlive(qint64 pid) {
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

bool waitForExit(qint64 pid, QString* error) {
    int elapsedMs = 0;
    while (processAlive(pid) && elapsedMs < kWaitTimeoutMs) {
        QThread::msleep(static_cast<unsigned long>(kPollIntervalMs));
        elapsedMs += kPollIntervalMs;
    }

    if (processAlive(pid)) {
        if (error != nullptr) {
            *error = QStringLiteral("进程 %1 在 %2 毫秒内未退出")
                         .arg(pid)
                         .arg(kWaitTimeoutMs);
        }
        return false;
    }
    return true;
}

void fail(const char* message) {
    std::fprintf(stderr, "dsh-desktop-updater: %s\n", message);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dsh-desktop-updater"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(DSH_DESKTOP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("DSH Desktop 自更新助手：校验并原子替换正在运行的程序。"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption pidOption(
        QStringLiteral("pid"),
        QStringLiteral("等待其退出的旧实例进程 ID。"), QStringLiteral("pid"));
    const QCommandLineOption sourceOption(
        QStringLiteral("source"),
        QStringLiteral("新的可执行文件（更新包）路径。"), QStringLiteral("source"));
    const QCommandLineOption destinationOption(
        QStringLiteral("destination"),
        QStringLiteral("要替换的已安装二进制路径。"), QStringLiteral("destination"));
    const QCommandLineOption sha256Option(
        QStringLiteral("sha256"),
        QStringLiteral("期望的来源文件 SHA-256（64 位十六进制）。"),
        QStringLiteral("sha256"));
    const QCommandLineOption installPrefixOption(
        QStringLiteral("install-prefix"),
        QStringLiteral("可信安装前缀；未提供时默认取本可执行所在目录的父目录。"),
        QStringLiteral("prefix"));

    parser.addOption(pidOption);
    parser.addOption(sourceOption);
    parser.addOption(destinationOption);
    parser.addOption(sha256Option);
    parser.addOption(installPrefixOption);
    parser.process(app);

    const QString pidText = parser.value(pidOption);
    const QString source = parser.value(sourceOption);
    const QString destination = parser.value(destinationOption);
    const QString expectedSha = parser.value(sha256Option);
    const QString installPrefix = parser.value(installPrefixOption);

    // --- 参数完整性校验 ---
    if (pidText.isEmpty() || source.isEmpty() || destination.isEmpty() ||
        expectedSha.isEmpty()) {
        fail("--pid、--source、--destination 与 --sha256 都必须提供。");
        return 2;
    }

    bool pidOk = false;
    const qint64 pid = pidText.toLongLong(&pidOk);
    if (!pidOk || pid <= 0) {
        std::fprintf(stderr, "dsh-desktop-updater: 无效的 --pid：%s\n",
                     qPrintable(pidText));
        return 2;
    }

    if (!isValidSha256Hex(expectedSha)) {
        fail("--sha256 必须是 64 个十六进制字符。");
        return 2;
    }

    // 目标目录 = ```--destination`` 的父目录；恢复与替换都在此进行。
    const QString destDir = QFileInfo(destination).absolutePath();
    // 已安装桌面二进制：与本可执行同目录的 ``dsh-desktop``。
    const QString installedBinary =
        QCoreApplication::applicationDirPath() + QStringLiteral("/dsh-desktop");
    // 可信安装前缀：未显式指定时取本可执行所在目录的父目录
    // （/usr/bin -> /usr；~/.local/bin -> ~/.local）。
    const QString trustedPrefix = installPrefix.isEmpty()
        ? QFileInfo(QCoreApplication::applicationDirPath()).absolutePath()
        : installPrefix;

    QString err;

    // --- 启动恢复上次更新崩溃遗留的孤儿 .dsh-update 文件 ---
    if (!recoverOrphanedDshUpdateFiles(destDir, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: 恢复孤儿更新文件失败：%s\n",
                     qPrintable(err));
        return 8;
    }

    // --- 等待旧实例退出（有界超时） ---
    if (!waitForExit(pid, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: %s\n", qPrintable(err));
        return 3;
    }

    // --- 校验来源与目标 ---
    if (!validateSource(source, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: 来源校验失败：%s\n",
                     qPrintable(err));
        return 4;
    }
    if (!validateInstallDestination(destination, installedBinary, trustedPrefix, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: 目标校验失败：%s\n",
                     qPrintable(err));
        return 5;
    }

    // --- 校验 SHA-256 ---
    if (!verifySha256(source, expectedSha, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: SHA-256 校验失败：%s\n",
                     qPrintable(err));
        return 6;
    }

    // --- 保留权限并原子替换 ---
    if (!atomicReplace(source, destination, &err)) {
        std::fprintf(stderr, "dsh-desktop-updater: 替换失败：%s\n",
                     qPrintable(err));
        return 7;
    }

    return 0;
}
