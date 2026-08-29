// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Sha256.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>

namespace dsh::util {

namespace {
void setError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}
}  // namespace

bool computeFileSha256(const QString& path, QString* outHex, QString* error) {
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

}  // namespace dsh::util
