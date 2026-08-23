// SPDX-License-Identifier: MIT
// @author zhouwr
#include "DesktopVersionChecker.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "Updater.h"

namespace dsh::updater {

namespace {

// 公开仓库主页与 Gitee v5 API（latest 端点，404 表示尚无发布）
constexpr const char* kRepositoryUrl = "https://gitee.com/eruditeLoong/dsh-desktop-qt";
constexpr const char* kLatestReleaseUrl =
    "https://gitee.com/api/v5/repos/eruditeLoong/dsh-desktop-qt/releases/latest";

constexpr const char* kDetailOk = "ok";

// 当数组中出现多个 release 时，挑选 SemVer 最高（且合法）的一个；
// 条目之间仅比较，不依赖服务器排序。
bool isBetterRelease(const DesktopReleaseInfo& candidate, const DesktopReleaseInfo* currentBest) {
    if (!currentBest) return true;
    return compareVersions(candidate.tagName, currentBest->tagName) > 0;
}

}  // namespace

QString DesktopVersionChecker::repositoryUrl() {
    return QString::fromLatin1(kRepositoryUrl);
}

QString DesktopVersionChecker::latestReleaseUrl() {
    return QString::fromLatin1(kLatestReleaseUrl);
}

VersionCheckStatus DesktopVersionChecker::parseReleaseObject(
    const QJsonObject& obj, DesktopReleaseInfo& out) {
    out = DesktopReleaseInfo{};
    const QString tag = obj.value("tag_name").toString();
    // tag 缺失或不是合法 SemVer -> 响应不可用
    if (tag.isEmpty() || !isValidSemVer(tag)) return VersionCheckStatus::InvalidResponse;

    out.tagName = tag;
    out.name = obj.value("name").toString();
    out.body = obj.value("body").toString();
    out.prerelease = obj.value("prerelease").toBool(false);

    const QJsonArray assets = obj.value("assets").toArray();
    out.assets.reserve(assets.size());
    for (const QJsonValue& value : assets) {
        const QJsonObject assetObj = value.toObject();
        DesktopReleaseAsset asset;
        asset.name = assetObj.value("name").toString();
        asset.url = assetObj.value("browser_download_url").toString();
        asset.size = static_cast<qint64>(assetObj.value("size").toDouble(0.0));
        out.assets.append(asset);
    }
    return VersionCheckStatus::Ok;
}

VersionCheckStatus DesktopVersionChecker::parseRelease(
    const QByteArray& json, DesktopReleaseInfo& out) {
    out = DesktopReleaseInfo{};

    const QByteArray trimmed = json.trimmed();
    if (trimmed.isEmpty()) return VersionCheckStatus::NoRelease;  // 无响应体

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // ``null`` 是合法的 JSON“空值”，表示没有任何发布；其余解析错误
        // （正文不是合法 JSON，或顶层不是对象/数组）视为响应不可用。
        return trimmed.compare("null", Qt::CaseInsensitive) == 0
                   ? VersionCheckStatus::NoRelease
                   : VersionCheckStatus::InvalidResponse;
    }

    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (arr.isEmpty()) return VersionCheckStatus::NoRelease;

        DesktopReleaseInfo best;
        const DesktopReleaseInfo* bestPtr = nullptr;
        for (const QJsonValue& value : arr) {
            const QJsonObject obj = value.toObject();
            DesktopReleaseInfo candidate;
            if (parseReleaseObject(obj, candidate) != VersionCheckStatus::Ok) {
                continue;  // 跳过非法/非 SemVer 的条目
            }
            if (isBetterRelease(candidate, bestPtr)) {
                best = candidate;
                bestPtr = &best;
            }
        }
        if (!bestPtr) return VersionCheckStatus::InvalidResponse;  // 没有可用的发布
        out = best;
        return VersionCheckStatus::Ok;
    }

    if (doc.isObject()) {
        return parseReleaseObject(doc.object(), out);
    }

    return VersionCheckStatus::InvalidResponse;
}

VersionCheckStatus DesktopVersionChecker::parseHttpResponse(
    int httpStatus, const QByteArray& json, DesktopReleaseInfo& out) {
    out = DesktopReleaseInfo{};
    if (httpStatus < 200 || httpStatus >= 300) {
        // Gitee /releases/latest 在尚无发布时返回 404
        return httpStatus == 404 ? VersionCheckStatus::NoRelease
                                 : VersionCheckStatus::InvalidResponse;
    }
    return parseRelease(json, out);
}

bool DesktopVersionChecker::isUpgrade(
    const DesktopReleaseInfo& release, const QString& localVersion) {
    const QString local = localVersion.trimmed();
    if (!isValidSemVer(release.tagName) || !isValidSemVer(local)) return false;
    return compareVersions(release.tagName, local) > 0;
}

DesktopVersionResult DesktopVersionChecker::fetchLatestRelease(int timeoutSeconds) {
    QNetworkAccessManager nam;
    QEventLoop loop;
    QObject::connect(&nam, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);

    const QUrl url = QUrl::fromEncoded(latestReleaseUrl().toUtf8());
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "dsh-desktop/0.1 (Qt6)");

    QNetworkReply* reply = nam.get(request);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply, &loop]() {
        reply->abort();
        loop.quit();
    });
    timer.start(qMax(1, timeoutSeconds) * 1000);
    loop.exec();

    if (!reply) {
        DesktopVersionResult result;
        result.status = VersionCheckStatus::InvalidResponse;
        result.detail = QStringLiteral("no network reply");
        return result;
    }

    // Qt 对 HTTP 错误状态（如 404）也会把 ``reply->error()`` 置为非 NoError，
    // 但这类回复带有 HTTP 状态码；真正的网络故障（超时/拒绝连接）没有状态码。
    // 因此先读取状态码：有状态码则交给 ``parseHttpResponse`` 语义化处理，
    // 无状态码才判定为 Offline。
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && httpStatus <= 0) {
        const QString error = reply->errorString();
        reply->deleteLater();
        DesktopVersionResult result;
        result.status = VersionCheckStatus::Offline;
        result.detail = error;
        return result;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    DesktopReleaseInfo release;
    const VersionCheckStatus status = parseHttpResponse(httpStatus, body, release);

    DesktopVersionResult result;
    result.status = status;
    result.release = release;
    result.detail = (status == VersionCheckStatus::Ok) ? QString::fromLatin1(kDetailOk)
                                                       : result.detail;
    return result;
}

DesktopVersionResult DesktopVersionChecker::check(const QString& localVersion, int timeoutSeconds) {
    DesktopVersionResult result = fetchLatestRelease(timeoutSeconds);
    if (result.status != VersionCheckStatus::Ok) return result;
    result.updateAvailable = isUpgrade(result.release, localVersion);
    return result;
}

}  // namespace dsh::updater
