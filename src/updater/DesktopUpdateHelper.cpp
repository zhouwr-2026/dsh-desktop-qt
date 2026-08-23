// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 自更新助手核心实现。
//
// 刻意只依赖 Qt Core（QFile / QFileInfo / QCryptographicHash / QRandomGenerator），
// 不依赖 Widgets / WebEngine / DBus，也不调用任何 shell 或 systemctl —— 这样它
// 既可以被最小化的 ``dsh-desktop-updater`` CLI 复用，也可以被纯单元测试覆盖。

#include "DesktopUpdateHelper.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <cstdio>   // std::rename
#include <cstring>  // std::strerror
#include <cerrno>   // errno
#include <fcntl.h>  // ::open / O_RDONLY
#include <unistd.h> // ::getpid / ::fsync / ::close

namespace dsh::updater {

namespace {

/// 统一的错误写出助手：error 为空指针时静默丢弃。
void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

/// 读取并执行 ``strerror``，返回可读的错误描述。
QString strerrorText(int errnoValue) {
    return QString::fromLocal8Bit(std::strerror(errnoValue));
}

/// 生成同目录临时/备份文件的后缀，避免与残留文件冲突。
QString uniqueSuffix() {
    return QStringLiteral("%1-%2")
        .arg(::getpid())
        .arg(QRandomGenerator::global()->generate());
}

/// 把一个文件的内容复制到另一个文件（分块，避免整文件载入内存）。
/// \return 成功返回 ``true``。
bool copyFileContents(const QString& source, const QString& destination,
                      QString* error) {
    static constexpr qint64 kChunkSize = 1 << 20;  // 1 MiB

    QFile srcFile(source);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开源文件以读取：%1 (%2)")
                            .arg(source, srcFile.errorString()));
        return false;
    }

    QFile dstFile(destination);
    if (!dstFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("无法创建临时文件：%1 (%2)")
                            .arg(destination, dstFile.errorString()));
        return false;
    }

    QByteArray chunk;
    while (!srcFile.atEnd()) {
        chunk = srcFile.read(kChunkSize);
        if (chunk.isEmpty()) {
            setError(error, QStringLiteral("读取源文件失败：%1")
                                .arg(srcFile.errorString()));
            dstFile.close();
            QFile::remove(destination);
            return false;
        }
        if (dstFile.write(chunk) != chunk.size()) {
            setError(error, QStringLiteral("写入临时文件失败：%1")
                                .arg(dstFile.errorString()));
            dstFile.close();
            QFile::remove(destination);
            return false;
        }
    }

    if (!dstFile.flush()) {
        setError(error, QStringLiteral("刷新临时文件失败：%1")
                            .arg(dstFile.errorString()));
        dstFile.close();
        QFile::remove(destination);
        return false;
    }

    // 尽力把临时文件数据落盘，随后关闭。
    const qintptr fd = dstFile.handle();
    if (fd >= 0) {
        ::fsync(static_cast<int>(fd));
    }
    dstFile.close();
    return true;
}

/// 对目录做一次 fsync，尽量让 rename 元数据落盘；失败忽略（尽力而为）。
void fsyncDirectory(const QString& dirPath) {
    const int fd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}

}  // namespace

bool computeSha256(const QString& path, QString* outHex, QString* error) {
    if (path.isEmpty()) {
        setError(error, QStringLiteral("路径为空"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开文件：%1 (%2)")
                            .arg(path, file.errorString()));
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    static constexpr qint64 kChunkSize = 1 << 20;  // 1 MiB

    QByteArray chunk;
    while (!file.atEnd()) {
        chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            setError(error, QStringLiteral("读取文件失败：%1 (%2)")
                                .arg(path, file.errorString()));
            return false;
        }
        hash.addData(chunk);
    }

    if (outHex != nullptr) {
        *outHex = QString::fromLatin1(hash.result().toHex());
    }
    return true;
}

bool isValidSha256Hex(const QString& hex) {
    if (hex.size() != 64) {
        return false;
    }
    const char* data = hex.toLatin1().constData();
    for (int i = 0; i < 64; ++i) {
        const char c = data[i];
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isLowerHex = (c >= 'a' && c <= 'f');
        const bool isUpperHex = (c >= 'A' && c <= 'F');
        if (!(isDigit || isLowerHex || isUpperHex)) {
            return false;
        }
    }
    return true;
}

bool verifySha256(const QString& path, const QString& expectedHex, QString* error) {
    if (!isValidSha256Hex(expectedHex)) {
        setError(error, QStringLiteral("期望的 SHA-256 不是合法的 64 位十六进制串"));
        return false;
    }

    QString actual;
    if (!computeSha256(path, &actual, error)) {
        return false;
    }

    if (actual.compare(expectedHex, Qt::CaseInsensitive) != 0) {
        setError(error, QStringLiteral("SHA-256 不匹配：实际 %1，期望 %2")
                            .arg(actual, expectedHex));
        return false;
    }
    return true;
}

bool validateSource(const QString& source, QString* error) {
    if (source.isEmpty()) {
        setError(error, QStringLiteral("源路径为空"));
        return false;
    }

    const QFileInfo info(source);
    if (!info.exists()) {
        setError(error, QStringLiteral("源文件不存在：%1").arg(source));
        return false;
    }
    if (!info.isFile()) {
        setError(error, QStringLiteral("源不是常规文件：%1").arg(source));
        return false;
    }
    if (!info.isExecutable()) {
        setError(error, QStringLiteral("源文件不可执行：%1").arg(source));
        return false;
    }
    return true;
}

bool validateDestination(const QString& destination, QString* error) {
    if (destination.isEmpty()) {
        setError(error, QStringLiteral("目标路径为空"));
        return false;
    }

    const QString absDest = QFileInfo(destination).absoluteFilePath();
    if (absDest == QLatin1String("/") || QFileInfo(absDest).fileName().isEmpty()) {
        setError(error, QStringLiteral("目标路径不合法：%1").arg(destination));
        return false;
    }

    const QFileInfo destInfo(absDest);
    if (destInfo.isDir()) {
        setError(error, QStringLiteral("目标是目录：%1").arg(absDest));
        return false;
    }

    const QString parentPath = QFileInfo(absDest).absolutePath();
    const QFileInfo parentInfo(parentPath);
    if (!parentInfo.exists()) {
        setError(error, QStringLiteral("目标父目录不存在：%1").arg(parentPath));
        return false;
    }
    if (!parentInfo.isDir()) {
        setError(error, QStringLiteral("目标父路径不是目录：%1").arg(parentPath));
        return false;
    }
    if (!parentInfo.isWritable()) {
        setError(error, QStringLiteral("目标父目录不可写：%1").arg(parentPath));
        return false;
    }
    return true;
}

bool atomicReplace(const QString& source, const QString& destination,
                   QString* error) {
    QString err;
    if (!validateSource(source, &err)) {
        setError(error, err);
        return false;
    }
    if (!validateDestination(destination, &err)) {
        setError(error, err);
        return false;
    }

    const QString absDest = QFileInfo(destination).absoluteFilePath();
    const QString destDir = QFileInfo(absDest).absolutePath();
    const QString baseName = QFileInfo(absDest).fileName();
    const QString suffix = uniqueSuffix();

    const QString tempPath =
        destDir + QLatin1Char('/') + QStringLiteral(".dsh-update-%1.tmp-%2")
                                           .arg(baseName, suffix);
    const QString backupPath =
        destDir + QLatin1Char('/') + QStringLiteral(".dsh-update-%1.bak-%2")
                                           .arg(baseName, suffix);

    // 1. 把源文件内容写入目标目录下的临时文件。
    if (!copyFileContents(source, tempPath, error)) {
        return false;
    }

    // 2. 决定新文件应具备的权限：目标已存在则保留其权限，否则沿用源文件权限。
    const QFileInfo destInfo(absDest);
    const QFile::Permissions permissions =
        destInfo.exists() ? destInfo.permissions()
                          : QFileInfo(source).permissions();
    if (!QFile::setPermissions(tempPath, permissions)) {
        setError(error, QStringLiteral("无法设置新文件权限：%1").arg(tempPath));
        QFile::remove(tempPath);
        return false;
    }

    // 3. 若目标已存在，先把它移到同目录备份文件，腾出位置且保留回滚能力。
    const bool destExisted = destInfo.exists();
    if (destExisted) {
        if (std::rename(QFile::encodeName(absDest).constData(),
                        QFile::encodeName(backupPath).constData()) != 0) {
            const int errNo = errno;
            QFile::remove(tempPath);
            setError(error, QStringLiteral("无法把现有目标移到备份：%1 (%2)")
                                .arg(absDest, strerrorText(errNo)));
            return false;
        }
    }

    // 4. 把临时文件 rename 到目标路径；失败则回滚备份。
    if (std::rename(QFile::encodeName(tempPath).constData(),
                    QFile::encodeName(absDest).constData()) != 0) {
        const int errNo = errno;
        if (destExisted) {
            std::rename(QFile::encodeName(backupPath).constData(),
                        QFile::encodeName(absDest).constData());
        }
        QFile::remove(tempPath);
        setError(error, QStringLiteral("无法把新文件放入目标位置：%1 (%2)")
                            .arg(absDest, strerrorText(errNo)));
        return false;
    }

    // 5. 成功：丢弃备份文件，并尽力刷盘目录元数据。
    if (destExisted) {
        QFile::remove(backupPath);
    }
    fsyncDirectory(destDir);
    return true;
}

}  // namespace dsh::updater
