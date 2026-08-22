// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Updater.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
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
    int major{0};
    int minor{0};
    int patch{0};
    QString pre;   // 空字符串 == 稳定版
    QString build;
};

bool parseSemVer(const QString& raw, ParsedSemVer& out) {
    const QRegularExpressionMatch m = kSemVerPattern.match(raw.trimmed());
    if (!m.hasMatch()) return false;
    out.major = m.captured("major").toInt();
    out.minor = m.captured("minor").toInt();
    out.patch = m.captured("patch").toInt();
    out.pre = m.captured("pre");
    out.build = m.captured("build");
    return true;
}

bool isNumericIdent(const QString& s) {
    bool ok = false;
    s.toInt(&ok);
    return ok;
}

// 严格 SemVer 比较：稳定版 > 同号预发布版；预发布号按标识符逐项比较
// （数字标识符 < 非数字标识符）。
int compareSemVer(const ParsedSemVer& a, const ParsedSemVer& b) {
    if (a.major != b.major) return (a.major > b.major) - (a.major < b.major);
    if (a.minor != b.minor) return (a.minor > b.minor) - (a.minor < b.minor);
    if (a.patch != b.patch) return (a.patch > b.patch) - (a.patch < b.patch);
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
        if (xn && yn) {
            const int xi = x.toInt(), yi = y.toInt();
            return (xi > yi) - (xi < yi);
        }
        if (xn) return -1;  // 数字 < 非数字
        if (yn) return 1;
        return (x > y) - (x < y);
    }
    return (ai.size() > bi.size()) - (ai.size() < bi.size());
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
    QTimer::singleShot(timeoutSeconds * 1000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply) return {};
    if (reply->error() != QNetworkReply::NoError) {
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
        s.detail = "SemVer 解析失败，退化为字符串比较";
        s.updateAvailable = (s.latest != s.current);
        return s;
    }
    s.updateAvailable = (compareSemVer(lat, cur) > 0);
    s.detail = "ok";
    return s;
}

bool Updater::performUpdate(const QString& label) {
    label_ = label;
    Q_UNUSED(label_);
    auto pkexec = QStandardPaths::findExecutable("pkexec");
    auto npm = QStandardPaths::findExecutable("npm");
    if (pkexec.isEmpty()) {
        emit log("updater: pkexec 未安装（请先安装 polkit）");
        return false;
    }
    if (npm.isEmpty()) {
        emit log("updater: npm 未安装");
        return false;
    }
    const QStringList args = {
        "--disable-internal-agent",
        npm, "install", "-g",
        "@deepseek-ai/dsh@latest",
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
    // 同步等待输出——UI 层会用 QThread 包装避免阻塞主事件循环
    while (p.state() != QProcess::NotRunning) {
        if (!p.waitForFinished(500)) continue;
        break;
    }
    if (p.exitStatus() != QProcess::NormalExit) return false;
    const bool ok = (p.exitCode() == 0);
    emit log(QStringLiteral("updater: pkexec npm install -> rc=%1").arg(p.exitCode()));
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
