// SPDX-License-Identifier: MIT
// @author zhouwr
#include "DesktopReleaseDownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <limits>

namespace dsh::updater {

namespace {

constexpr const char* kStageDownloading = "downloading";
constexpr const char* kStageFinalizing = "finalizing";

/// 用作"不可选"哨兵的最低分。
constexpr int kSkipScore = std::numeric_limits<int>::lowest();

/// 判定主机是否为 Gitee：``gitee.com`` 或任意 ``*.gitee.com`` 子域。
bool isGiteeHost(const QString& host) {
    const QString h = host.trimmed().toLower();
    return h == QStringLiteral("gitee.com") || h.endsWith(QLatin1String(".gitee.com"));
}

/// 流式计算文件 SHA-256（小写十六进制，64 字符）。
bool computeFileSha256(const QString& path, QString* outHex) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    static constexpr qint64 kChunkSize = 1 << 20;  // 1 MiB
    QByteArray chunk;
    while (!file.atEnd()) {
        chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            return false;
        }
        hash.addData(chunk);
    }
    *outHex = QString::fromLatin1(hash.result().toHex());
    return true;
}

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

/// 判断文件名是否属于"辅助文件"（校验和/签名/说明），永远不作为下载目标。
bool isAuxiliaryName(const QString& lowerName) {
    static const char* const kSuffixes[] = {
        ".sha256", ".sha512", ".md5", ".sum", ".checksum",
        ".sig", ".asc", ".txt", ".zip.asc",
    };
    for (const char* suffix : kSuffixes) {
        if (lowerName.endsWith(QLatin1String(suffix))) {
            return true;
        }
    }
    // 混合命名（例如 ``name.AppImage.sha256``）也通过关键词兜底。
    return lowerName.contains(QLatin1String("checksum"))
        || lowerName.contains(QLatin1String("signature"));
}

/// 文件名的平台偏好基础分。
int formatScore(const QString& lowerName, const QString& osName) {
    if (osName == QLatin1String("windows")) {
        if (lowerName.endsWith(QLatin1String(".msi"))) return 100;
        if (lowerName.endsWith(QLatin1String(".exe"))) return 95;
        if (lowerName.endsWith(QLatin1String(".zip"))) return 40;
        return 10;
    }
    if (osName == QLatin1String("macos")) {
        if (lowerName.endsWith(QLatin1String(".dmg"))) return 100;
        if (lowerName.endsWith(QLatin1String(".app"))) return 95;
        if (lowerName.endsWith(QLatin1String(".pkg"))) return 90;
        if (lowerName.endsWith(QLatin1String(".zip"))) return 40;
        return 10;
    }
    // 默认按 Linux 桌面判定。
    if (lowerName.endsWith(QLatin1String(".appimage"))) return 100;
    if (lowerName.endsWith(QLatin1String(".deb"))) return 70;
    if (lowerName.endsWith(QLatin1String(".rpm"))) return 60;
    if (lowerName.endsWith(QLatin1String(".tar.gz")) || lowerName.endsWith(QLatin1String(".tgz"))) return 30;
    if (lowerName.endsWith(QLatin1String(".zip"))) return 20;
    return 10;
}

/// 文件名是否点名了"当前架构"的同义词（如 x86_64 / amd64）。
bool mentionsArch(const QString& lowerName, const QString& architecture) {
    if (architecture == QLatin1String("x86_64") || architecture == QLatin1String("amd64")) {
        return lowerName.contains(QLatin1String("x86_64")) || lowerName.contains(QLatin1String("amd64"));
    }
    if (architecture == QLatin1String("aarch64") || architecture == QLatin1String("arm64")) {
        return lowerName.contains(QLatin1String("aarch64")) || lowerName.contains(QLatin1String("arm64"))
            || lowerName.contains(QLatin1String("armv8"));
    }
    return false;
}

/// 文件名是否点名了与"当前架构"冲突的其它已知架构。
bool mentionsConflictingArch(const QString& lowerName, const QString& architecture) {
    const bool isX86 = (architecture == QLatin1String("x86_64") || architecture == QLatin1String("amd64"));
    const bool isArm = (architecture == QLatin1String("aarch64") || architecture == QLatin1String("arm64"));
    if (isX86) {
        return lowerName.contains(QLatin1String("aarch64")) || lowerName.contains(QLatin1String("arm64"))
            || lowerName.contains(QLatin1String("armv8"));
    }
    if (isArm) {
        return lowerName.contains(QLatin1String("x86_64")) || lowerName.contains(QLatin1String("amd64"));
    }
    return false;
}

}  // namespace

DesktopReleaseDownloader::DesktopReleaseDownloader(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

DesktopReleaseDownloader::DesktopReleaseDownloader(const QString& cachePath, QObject* parent)
    : DesktopReleaseDownloader(parent) {
    cachePath_ = cachePath;
}

DesktopReleaseDownloader::~DesktopReleaseDownloader() {
    if (reply_ != nullptr) {
        reply_->abort();
    }
}

QString DesktopReleaseDownloader::defaultCachePath() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

bool DesktopReleaseDownloader::isAllowedUrl(const QUrl& url, QString* reason) {
    if (!url.isValid()) {
        setError(reason, QStringLiteral("invalid URL"));
        return false;
    }
    if (url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) {
        setError(reason, QStringLiteral("scheme must be https, got '%1'").arg(url.scheme()));
        return false;
    }
    if (!isGiteeHost(url.host())) {
        setError(reason,
                 QStringLiteral("host '%1' is not a Gitee host").arg(url.host()));
        return false;
    }
    return true;
}

bool DesktopReleaseDownloader::isAllowedUrl(const QString& urlString, QString* reason) {
    const QUrl url(urlString);
    return isAllowedUrl(url, reason);
}

QString DesktopReleaseDownloader::sanitizeFileName(const QString& assetName, QString* error) {
    if (assetName.isEmpty()) {
        setError(error, QStringLiteral("asset name is empty"));
        return {};
    }
    if (assetName.indexOf(QLatin1Char('\0')) >= 0) {
        setError(error, QStringLiteral("asset name contains a NUL byte"));
        return {};
    }
    // 拒绝任何路径分隔符：``/`` 与 ``\``（覆盖 Windows 与 POSIX 两种分隔）。
    if (assetName.contains(QLatin1Char('/')) || assetName.contains(QLatin1Char('\\'))) {
        setError(error, QStringLiteral("asset name contains a path separator"));
        return {};
    }
    // 拒绝首尾空白，避免拼路径时产生含糊。
    if (assetName.trimmed() != assetName) {
        setError(error, QStringLiteral("asset name has leading/trailing whitespace"));
        return {};
    }
    // ``QDir::cleanPath`` 会把 ``.`` / ``..`` / 冗余分隔规整化；任何改写都说明
    // 这个名字不是纯净的单层文件名。
    const QString cleaned = QDir::cleanPath(assetName);
    if (cleaned != assetName) {
        setError(error, QStringLiteral("asset name is not a plain file name"));
        return {};
    }
    if (cleaned == QLatin1String(".") || cleaned == QLatin1String("..")) {
        setError(error, QStringLiteral("asset name is a dot entry"));
        return {};
    }
    // 双保险：最终必须是单个文件名分量，且不是目录。
    const QFileInfo info(cleaned);
    if (info.isDir() || info.fileName() != cleaned) {
        setError(error, QStringLiteral("asset name resolves to a directory or non-plain path"));
        return {};
    }
    return cleaned;
}

QString DesktopReleaseDownloader::hostOsName() {
    const QString kernel = QSysInfo::kernelType().toLower();
    if (kernel.contains(QLatin1String("darwin"))) {
        return QStringLiteral("macos");
    }
    if (kernel.contains(QLatin1String("win"))) {
        return QStringLiteral("windows");
    }
    return QStringLiteral("linux");
}

QString DesktopReleaseDownloader::hostArchitecture() {
    return QSysInfo::currentCpuArchitecture().toLower();
}

int DesktopReleaseDownloader::assetScore(const DesktopReleaseAsset& asset,
                                         const QString& osName,
                                         const QString& architecture) {
    const QString lowerName = asset.name.toLower();
    if (lowerName.isEmpty()) {
        return kSkipScore;
    }
    // URL 非法（非 HTTPS / 非 Gitee）的资产不可作为下载目标。
    if (!isAllowedUrl(asset.url)) {
        return kSkipScore;
    }
    // 辅助 / 校验 / 签名文件永远不选。
    if (isAuxiliaryName(lowerName)) {
        return kSkipScore;
    }

    int score = formatScore(lowerName, osName);

    // 架构匹配加分；点名冲突架构则减分。
    if (mentionsArch(lowerName, architecture)) {
        score += 30;
    } else if (mentionsConflictingArch(lowerName, architecture)) {
        score -= 30;
    }

    // 源码 / debug / 测试 产物不应作为桌面发布包落下。
    if (lowerName.contains(QLatin1String("source"))
        || lowerName.contains(QLatin1String("-debug"))
        || lowerName.startsWith(QLatin1String("debug-"))
        || lowerName.contains(QLatin1String("-test"))) {
        score -= 30;
    }

    return score;
}

const DesktopReleaseAsset* DesktopReleaseDownloader::selectBestAsset(
    const QVector<DesktopReleaseAsset>& assets,
    const QString& osName,
    const QString& architecture) {
    const DesktopReleaseAsset* best = nullptr;
    int bestScore = kSkipScore;
    for (const DesktopReleaseAsset& asset : assets) {
        const int score = assetScore(asset, osName, architecture);
        if (score == kSkipScore) {
            continue;
        }
        if (best == nullptr || score > bestScore) {
            best = &asset;
            bestScore = score;
        }
    }
    return best;
}

QString DesktopReleaseDownloader::effectiveCacheRoot() const {
    return cachePath_.isEmpty() ? defaultCachePath() : cachePath_;
}

bool DesktopReleaseDownloader::safeCachePath(const QString& fileName,
                                             QString* outPath,
                                             QString* error) const {
    const QString root = effectiveCacheRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("no usable cache directory"));
        return false;
    }

    QDir dir(root);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("cannot create cache directory '%1'").arg(root));
        return false;
    }

    const QString absRoot = QFileInfo(root).absoluteFilePath();
    const QString absPath = QFileInfo(dir.filePath(fileName)).absoluteFilePath();

    // 防御路径穿越：最终绝对路径必须落在缓存根之内，且仍是单层文件名。
    if (absPath != absRoot && !absPath.startsWith(absRoot + QLatin1Char('/'))) {
        setError(error, QStringLiteral("target path escapes the cache directory"));
        return false;
    }
    if (QFileInfo(absPath).fileName() != fileName) {
        setError(error, QStringLiteral("target file name is not safe"));
        return false;
    }

    *outPath = absPath;
    return true;
}

bool DesktopReleaseDownloader::start(const DesktopReleaseAsset& asset) {
    // 清理上一次（若有）未完成的下载。
    cleanup();
    lastError_.clear();

    // 1. URL 校验。
    QString urlReason;
    if (!isAllowedUrl(asset.url, &urlReason)) {
        lastError_ = urlReason;
        return false;
    }

    // 2. 文件名净化。
    QString nameError;
    const QString safeName = sanitizeFileName(asset.name, &nameError);
    if (safeName.isEmpty()) {
        lastError_ = nameError;
        return false;
    }

    // 3. 落到受控缓存并防止路径穿越。
    QString targetPath;
    if (!safeCachePath(safeName, &targetPath, &lastError_)) {
        return false;
    }

    sourceUrl_ = asset.url;
    targetPath_ = targetPath;
    bytesReceived_ = 0;
    bytesTotal_ = -1;

    // 4. 打开原子写入目标。
    saveFile_ = new QSaveFile(targetPath_);
    if (!saveFile_->open(QIODevice::WriteOnly)) {
        lastError_ = QStringLiteral("cannot open target file '%1' (%2)")
                         .arg(targetPath_, saveFile_->errorString());
        delete saveFile_;
        saveFile_ = nullptr;
        return false;
    }

    // 5. 发起异步 GET。
    QNetworkRequest request{QUrl(sourceUrl_)};
    request.setRawHeader("User-Agent", "dsh-desktop/0.1 (Qt6)");
    request.setRawHeader("Accept", "application/octet-stream");

    reply_ = nam_->get(request);
    connect(reply_, &QNetworkReply::readyRead, this, &DesktopReleaseDownloader::onReadyRead);
    connect(reply_, &QNetworkReply::downloadProgress,
            this, &DesktopReleaseDownloader::onProgress);
    connect(reply_, &QNetworkReply::finished, this, &DesktopReleaseDownloader::onFinished);

    emit stageChanged(QString::fromLatin1(kStageDownloading));
    return true;
}

void DesktopReleaseDownloader::cancel() {
    if (reply_ != nullptr) {
        reply_->abort();
    }
}

void DesktopReleaseDownloader::onReadyRead() {
    if (reply_ == nullptr || saveFile_ == nullptr) {
        return;
    }
    const QByteArray data = reply_->readAll();
    if (data.isEmpty()) {
        return;
    }
    if (saveFile_->write(data) != data.size()) {
        // 写入失败：终止网络（会触发 finished 统一收尾），绝不 commit。
        reply_->abort();
        return;
    }
    bytesReceived_ += data.size();
    emit progressChanged(bytesReceived_, bytesTotal_);
}

void DesktopReleaseDownloader::onProgress(qint64 bytesReceived, qint64 bytesTotal) {
    Q_UNUSED(bytesReceived);
    bytesTotal_ = bytesTotal;  // 可能为 -1（未知总长）
    // 实际已写入字节以 ``bytesReceived_`` 为准（只统计 readyRead 的存量），
    // 这里仅同步更新总长并上报，避免与写盘计数不一致。
    emit progressChanged(bytesReceived_, bytesTotal_);
}

void DesktopReleaseDownloader::onFinished() {
    DesktopDownloadResult result;
    result.sourceUrl = sourceUrl_;

    bool ok = false;
    if (reply_ == nullptr) {
        result.error = QStringLiteral("no network reply");
    } else {
        if (reply_->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("download failed: %1").arg(reply_->errorString());
        } else {
            const int httpStatus =
                reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300)) {
                result.error = QStringLiteral("unexpected HTTP status: %1").arg(httpStatus);
            } else {
                ok = true;
            }
        }
    }

    emit stageChanged(QString::fromLatin1(kStageFinalizing));

    if (ok && saveFile_ != nullptr) {
        if (!saveFile_->commit()) {
            result.error =
                QStringLiteral("commit to target file failed: %1").arg(saveFile_->errorString());
            ok = false;
        } else {
            result.cachedPath = targetPath_;
            result.size = bytesReceived_;
            QString digest;
            if (computeFileSha256(targetPath_, &digest)) {
                result.sha256 = digest;
            } else {
                result.error =
                    QStringLiteral("cannot compute SHA-256 for '%1'").arg(targetPath_);
                ok = false;
            }
        }
    } else if (ok) {
        result.error = QStringLiteral("write channel not ready");
        ok = false;
    }

    cleanup();
    emit finished(result);
}

void DesktopReleaseDownloader::cleanup() {
    if (reply_ != nullptr) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (saveFile_ != nullptr) {
        delete saveFile_;  // 未 commit 时自动清理其临时文件
        saveFile_ = nullptr;
    }
    sourceUrl_.clear();
    targetPath_.clear();
    bytesReceived_ = 0;
    bytesTotal_ = -1;
}

}  // namespace dsh::updater
