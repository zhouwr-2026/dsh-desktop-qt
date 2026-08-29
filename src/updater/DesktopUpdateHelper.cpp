// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 自更新助手核心实现。
//
// 刻意只依赖 Qt Core（QFile / QFileInfo / QCryptographicHash / QRandomGenerator），
// 不依赖 Widgets / WebEngine / DBus，也不调用任何 shell 或 systemctl —— 这样它
// 既可以被最小化的 ``dsh-desktop-updater`` CLI 复用，也可以被纯单元测试覆盖。

#include "DesktopUpdateHelper.h"

#include "../util/Sha256.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>

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

/// 返回"尽力规范化"的绝对路径：文件存在时用 ``canonicalFilePath``（解析符号
/// 链接与 ``..``）；否则对父目录 canonical 后拼接文件名，最后以 ``cleanPath``
/// 兜底。用于把（可能尚不存在的）替换目标与可信前缀放到同一坐标系比较。
QString normalizedAbsolutePath(const QString& path) {
    const QFileInfo info(path);
    if (info.exists()) {
        const QString canon = info.canonicalFilePath();
        if (!canon.isEmpty()) return canon;
    }
    const QFileInfo parentInfo(info.absolutePath());
    const QString parentCanon = parentInfo.canonicalFilePath();
    const QString base = QFileInfo(info.absoluteFilePath()).fileName();
    if (!parentCanon.isEmpty()) {
        return parentCanon + QLatin1Char('/') + base;
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

/// 两条路径经 ``normalizedAbsolutePath`` 后是否相等。
bool pathsEqual(const QString& a, const QString& b) {
    return normalizedAbsolutePath(a) == normalizedAbsolutePath(b);
}

}  // namespace

bool computeSha256(const QString& path, QString* outHex, QString* error) {
    // 薄封装：实际计算下沉到 dsh::util::computeFileSha256，避免与
    // DesktopReleaseDownloader 出现两套流式 SHA-256 实现。
    // (变更理由: 结构审查 #2, computeSha256 / computeFileSha256 重复)
    return dsh::util::computeFileSha256(path, outHex, error);
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

bool isPathWithinPrefix(const QString& path, const QString& prefix,
                        QString* error) {
    if (path.isEmpty() || prefix.isEmpty()) {
        setError(error, QStringLiteral("路径或前缀为空"));
        return false;
    }
    const QString normPath = normalizedAbsolutePath(path);
    const QString normPrefix = normalizedAbsolutePath(prefix);
    // 明确把根前缀 "/" 当作信任整个文件系统（管理员显式选择）。
    if (normPrefix == QLatin1String("/")) {
        return true;
    }
    const bool equal = (normPath == normPrefix);
    const bool nested = normPath.startsWith(normPrefix + QLatin1Char('/'));
    if (!equal && !nested) {
        setError(error, QStringLiteral("路径不在允许的前缀内：%1（前缀 %2）")
                            .arg(path, prefix));
        return false;
    }
    return true;
}

bool validateInstallDestination(const QString& destination,
                                const QString& installedBinary,
                                const QString& trustedPrefix,
                                QString* error) {
    QString err;
    if (!validateDestination(destination, &err)) {
        setError(error, err);
        return false;
    }

    // 只允许：等于已知桌面二进制，或位于可信安装前缀之内。
    const bool isInstalledBinary =
        !installedBinary.isEmpty() && pathsEqual(destination, installedBinary);
    const bool inPrefix =
        !trustedPrefix.isEmpty() &&
        isPathWithinPrefix(destination, trustedPrefix, nullptr);
    if (!isInstalledBinary && !inPrefix) {
        setError(error, QStringLiteral("目标不在允许的自更新范围内：%1")
                            .arg(destination));
        return false;
    }

    // 目标若已存在，校验文件主与当前有效用户一致（尽力而为）。
    const QFileInfo destInfo(QFileInfo(destination).absoluteFilePath());
    if (destInfo.exists()) {
        const uint fileOwner = destInfo.ownerId();
        const uint euid = static_cast<uint>(::geteuid());
        if (fileOwner != euid) {
            setError(error, QStringLiteral("目标文件主（uid %1）与当前用户（uid %2）"
                                           "不一致：%3")
                                .arg(fileOwner)
                                .arg(euid)
                                .arg(destination));
            return false;
        }
    }
    return true;
}

bool recoverOrphanedDshUpdateFiles(const QString& directory, QString* error) {
    if (directory.isEmpty()) {
        setError(error, QStringLiteral("目录为空"));
        return false;
    }
    const QFileInfo dirInfo(directory);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        setError(error, QStringLiteral("目录不存在或不是目录：%1").arg(directory));
        return false;
    }

    // 匹配 ``.dsh-update-<base>.<kind>-<pid>-<rand>``，kind ∈ {bak, tmp}。
    static const QRegularExpression kOrphanPattern(
        QStringLiteral("^\\.dsh-update-(.+)\\.(bak|tmp)-(\\d+)-(\\d+)$"));

    const QDir dir(directory);
    const QStringList entries =
        dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& name : entries) {
        if (!name.startsWith(QStringLiteral(".dsh-update-"))) {
            continue;
        }
        const QRegularExpressionMatch match = kOrphanPattern.match(name);
        if (!match.hasMatch()) {
            // 名字不符合本模块约定，不是我们生成的残留文件，不动它。
            continue;
        }
        const QString base = match.captured(1);
        const QString kind = match.captured(2);
        const QString entryPath = dir.filePath(name);

        if (kind == QStringLiteral("bak")) {
            const QString originalPath = dir.filePath(base);
            if (QFileInfo(originalPath).exists()) {
                // 新文件已就位，备份已过时——删除。
                if (!QFile::remove(entryPath)) {
                    setError(error, QStringLiteral("无法删除过时备份：%1")
                                        .arg(entryPath));
                    return false;
                }
            } else {
                // 原目标缺失，把备份恢复回原名。
                if (std::rename(QFile::encodeName(entryPath).constData(),
                                QFile::encodeName(originalPath).constData()) != 0) {
                    const int errNo = errno;
                    setError(error, QStringLiteral("无法恢复更新备份：%1 -> %2 (%3)")
                                        .arg(entryPath, originalPath,
                                             strerrorText(errNo)));
                    return false;
                }
            }
        } else {  // tmp —— 未完成的临时写入，一律删除。
            if (!QFile::remove(entryPath)) {
                setError(error, QStringLiteral("无法清理临时更新文件：%1")
                                    .arg(entryPath));
                return false;
            }
        }
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
            // 回滚：把备份文件 rename 回目标路径。若回滚也失败，则目标可能
            // 缺失（原文件仍躺在备份路径上），必须明确报告备份所在位置，
            // 否则现场会留下"目标缺失但备份无人知晓"的危险状态。
            if (std::rename(QFile::encodeName(backupPath).constData(),
                            QFile::encodeName(absDest).constData()) != 0) {
                const int rollbackErr = errno;
                QFile::remove(tempPath);
                setError(
                    error,
                    QStringLiteral("无法把新文件放入目标位置：%1 (%2)；"
                                   "且恢复备份失败：目标可能缺失，原文件备份位于 %3 (%4)")
                        .arg(absDest, strerrorText(errNo),
                             backupPath, strerrorText(rollbackErr)));
                return false;
            }
        }
        // 回滚成功（或目标原本就不存在）：临时新文件已无用，删除之。
        QFile::remove(tempPath);
        setError(error, QStringLiteral("无法把新文件放入目标位置：%1 (%2)")
                            .arg(absDest, strerrorText(errNo)));
        return false;
    }

    // 5. 成功：丢弃备份文件，并尽力刷盘目录元数据。
    if (destExisted) {
        // 替换已成功；若备份清理失败，不能把调用方当成"替换失败"（替换不可逆地
        // 已经成功），因此在返回 ``true`` 的前提下通过 ``error`` 附上诊断，
        // 避免遗留备份文件造成的磁盘占用与现场混乱被静默吞掉。
        if (!QFile::remove(backupPath)) {
            fsyncDirectory(destDir);
            setError(error, QStringLiteral("替换成功，但无法删除备份文件：%1")
                                .arg(backupPath));
            return true;
        }
    }
    fsyncDirectory(destDir);
    return true;
}

}  // namespace dsh::updater
