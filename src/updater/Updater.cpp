// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Updater.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace dsh::updater {

namespace {

// npm 注册表：保持官方路径不变
constexpr const char* kRegistry = "https://registry.npmjs.org/@deepseek-ai/dsh";
constexpr const char* kLatestPath = "/latest";

// 严格 SemVer 2.0 正则：可选 v 前缀；可选预发布号；可选构建元数据
const QRegularExpression kSemVerPattern(
    R"(^[vV]?(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)(?:-(?P<pre>[0-9A-Za-z.-]+))?(?:\+(?P<build>[0-9A-Za-z.-]+))?$)");

struct ParsedSemVer {
    QString major;
    QString minor;
    QString patch;
    QString pre;   // 空字符串 == 稳定版
    QString build;
};

bool isNumericIdent(const QString& value) {
    if (value.isEmpty()) return false;
    for (const QChar character : value) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
    }
    return true;
}

bool validIdentifiers(const QString& value, bool rejectNumericLeadingZero) {
    if (value.isEmpty()) return true;
    const QStringList identifiers = value.split('.', Qt::KeepEmptyParts);
    for (const QString& identifier : identifiers) {
        if (identifier.isEmpty()) return false;
        if (rejectNumericLeadingZero && isNumericIdent(identifier)
            && identifier.size() > 1 && identifier.startsWith(QLatin1Char('0'))) {
            return false;
        }
    }
    return true;
}

bool parseSemVer(const QString& raw, ParsedSemVer& out) {
    const QRegularExpressionMatch match = kSemVerPattern.match(raw.trimmed());
    if (!match.hasMatch()) return false;
    out.major = match.captured("major");
    out.minor = match.captured("minor");
    out.patch = match.captured("patch");
    out.pre = match.captured("pre");
    out.build = match.captured("build");
    if ((out.major.size() > 1 && out.major.startsWith(QLatin1Char('0')))
        || (out.minor.size() > 1 && out.minor.startsWith(QLatin1Char('0')))
        || (out.patch.size() > 1 && out.patch.startsWith(QLatin1Char('0')))) {
        return false;
    }
    return validIdentifiers(out.pre, true) && validIdentifiers(out.build, false);
}

int compareNumericIdent(const QString& left, const QString& right) {
    if (left.size() != right.size()) return left.size() > right.size() ? 1 : -1;
    const int comparison = QString::compare(left, right, Qt::CaseSensitive);
    return (comparison > 0) - (comparison < 0);
}

// 严格 SemVer 比较：稳定版 > 同号预发布版；预发布号按标识符逐项比较
// （数字标识符 < 非数字标识符）。
int compareSemVer(const ParsedSemVer& a, const ParsedSemVer& b) {
    for (const auto& pair : {std::pair{a.major, b.major},
                             std::pair{a.minor, b.minor},
                             std::pair{a.patch, b.patch}}) {
        const int comparison = compareNumericIdent(pair.first, pair.second);
        if (comparison != 0) return comparison;
    }
    // SemVer 2.0：稳定版 > 预发布版
    if (a.pre.isEmpty() && !b.pre.isEmpty()) return 1;
    if (!a.pre.isEmpty() && b.pre.isEmpty()) return -1;
    if (a.pre == b.pre) return 0;
    const QStringList ai = a.pre.split('.');
    const QStringList bi = b.pre.split('.');
    const int n = std::min(ai.size(), bi.size());
    for (int i = 0; i < n; ++i) {
        const QString& x = ai[i];
        const QString& y = bi[i];
        if (x == y) continue;
        const bool xn = isNumericIdent(x);
        const bool yn = isNumericIdent(y);
        if (xn && yn) return compareNumericIdent(x, y);
        if (xn) return -1;  // 数字 < 非数字
        if (yn) return 1;
        return (x > y) - (x < y);
    }
    return (ai.size() > bi.size()) - (ai.size() < bi.size());
}

bool isTrustedRootExecutable(const QString& path) {
    const QFileInfo info(path);
    const QFileDevice::Permissions writableByOthers =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    return info.isFile() && info.isExecutable() && info.ownerId() == 0
        && !(info.permissions() & writableByOthers);
}

}  // namespace

Updater::Updater(QObject* parent) : QObject(parent) {}

QString Updater::readLocalVersion() {
    // 优先读 npm 全局安装里的 package.json——这是最权威的来源。
    const QStringList candidates = {
        "/usr/lib/node_modules/@deepseek-ai/dsh/package.json",
        "/usr/local/lib/node_modules/@deepseek-ai/dsh/package.json",
        QDir::homePath() + "/.local/lib/node_modules/@deepseek-ai/dsh/package.json",
    };
    for (const auto& c : candidates) {
        QFile f(c);
        if (!f.exists()) continue;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) continue;
        const QString v = doc.object().value("version").toString();
        if (!v.isEmpty()) return v;
    }
    // 兜底：调用 ``dsh --version``
    const QString bin = QStandardPaths::findExecutable("dsh");
    if (bin.isEmpty()) return {};
    QProcess p;
    p.start(bin, {"--version"});
    if (!p.waitForFinished(4000)) return {};
    QString v = QString::fromLocal8Bit(p.readAllStandardOutput());
    if (v.isEmpty()) v = QString::fromLocal8Bit(p.readAllStandardError());
    return v.trimmed();
}

QString Updater::fetchLatestVersion(int timeoutSeconds) {
    QNetworkAccessManager nam;
    QEventLoop loop;
    QObject::connect(&nam, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    QNetworkRequest req(QString::fromLatin1(kRegistry) + QString::fromLatin1(kLatestPath));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "dsh-desktop/0.1 (Qt6)");
    QNetworkReply* reply = nam.get(req);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply, &loop]() {
        reply->abort();
        loop.quit();
    });
    timer.start(qMax(1, timeoutSeconds) * 1000);
    loop.exec();
    if (!reply) return {};
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return {};
    }
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus < 200 || httpStatus >= 300) {
        reply->deleteLater();
        return {};
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) return {};
    return doc.object().value("version").toString();
}

Status Updater::check(int timeoutSeconds) {
    Status s;
    s.current = readLocalVersion();
    s.latest = fetchLatestVersion(timeoutSeconds);
    if (s.current.isEmpty() || s.latest.isEmpty()) {
        s.detail = "离线或无法访问 npm 注册表";
        return s;
    }
    ParsedSemVer cur, lat;
    if (!parseSemVer(s.current, cur) || !parseSemVer(s.latest, lat)) {
        s.detail = "SemVer 解析失败，已拒绝更新";
        s.updateAvailable = false;
        return s;
    }
    s.updateAvailable = (compareSemVer(lat, cur) > 0);
    s.detail = "ok";
    return s;
}

bool Updater::performUpdate() {
    ParsedSemVer target;
    const QString version = targetVersion_.trimmed();
    if (!parseSemVer(version, target)) {
        emit log(QStringLiteral("updater: 拒绝无效目标版本 %1").arg(version));
        return false;
    }

    const QString pkexec = QStringLiteral("/usr/bin/pkexec");
    const QString npm = QStringLiteral("/usr/bin/npm");
    if (!isTrustedRootExecutable(pkexec)) {
        emit log("updater: /usr/bin/pkexec 不存在或权限不安全");
        return false;
    }
    if (!isTrustedRootExecutable(npm)) {
        emit log("updater: /usr/bin/npm 不存在或权限不安全");
        return false;
    }
    const QString packageSpec = QStringLiteral("@deepseek-ai/dsh@%1").arg(version);
    const QStringList args = {
        "--disable-internal-agent",
        npm, "install", "-g",
        packageSpec,
        "--no-audit", "--no-fund"
    };
    QProcess p;
    p.setProgram(pkexec);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForStarted(5000)) {
        emit log(QStringLiteral("updater: 启动失败 %1").arg(p.errorString()));
        return false;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    constexpr qint64 kUpdateTimeoutMs = 10 * 60 * 1000;
    while (p.state() != QProcess::NotRunning && elapsed.elapsed() < kUpdateTimeoutMs) {
        p.waitForFinished(500);
    }
    if (p.state() != QProcess::NotRunning) {
        emit log("updater: 更新进程超时，正在终止");
        p.terminate();
        if (!p.waitForFinished(3000)) {
            p.kill();
            p.waitForFinished(3000);
        }
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit) return false;
    const bool ok = (p.exitCode() == 0);
    emit log(QStringLiteral("updater: 安装 %1 -> rc=%2")
                 .arg(packageSpec).arg(p.exitCode()));
    return ok;
}

void Updater::performUpdateAsync() {
    emit updateFinished(performUpdate());
}

}  // namespace dsh::updater

int dsh::updater::compareVersions(const QString& a, const QString& b) {
    ParsedSemVer pa, pb;
    if (!parseSemVer(a, pa) || !parseSemVer(b, pb)) return 0;
    return compareSemVer(pa, pb);
}
