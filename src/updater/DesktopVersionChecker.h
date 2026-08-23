// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 桌面版本检查器（只读）。
//
// 与 ``Updater``（检查 npm 上的 dsh CLI）不同，本类检查的是桌面端自身的
// 发布版本，来源为公开 Gitee 仓库：
//     https://gitee.com/eruditeLoong/dsh-desktop-qt
//
// 本切片只做版本探测与严格 SemVer 校验，**不下载、不更新**。
// 解析逻辑剥离为可单元测试的纯函数（给定 JSON 字节流即可判定状态），
// 网络调用遵循 ``Updater`` 的同步 Qt Network 风格。

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsh::updater {

/// 版本检查的强类型结果状态，避免依赖本地化字符串做分支判断。
enum class VersionCheckStatus {
    Ok,              // 成功解析出一个有效（SemVer 合法）的发布版本
    NoRelease,       // 仓库没有发布：空/无响应体、``[]`` 或 ``null``、404
    Offline,         // 网络失败或超时
    InvalidResponse, // 服务器可达，但响应不可用：非 2xx（除 404）、JSON 解析失败、tag 缺失或非法
};

/// Gitee release ``assets`` 数组中的单个附件。
struct DesktopReleaseAsset {
    QString name;   // 例如 ``dsh-desktop-0.1.0.AppImage``
    QString url;    // ``browser_download_url``
    qint64 size{0}; // 字节数

    bool operator==(const DesktopReleaseAsset& o) const {
        return name == o.name && url == o.url && size == o.size;
    }
    bool operator!=(const DesktopReleaseAsset& o) const { return !(*this == o); }
};

/// 解析出的一个 Gitee release。
struct DesktopReleaseInfo {
    QString tagName;      // ``tag_name``，例如 ``v0.1.0``（保留原始形式）
    QString name;         // ``name``
    QString body;         // ``body``（发布说明）
    bool prerelease{false}; // Gitee ``prerelease`` 标记
    QVector<DesktopReleaseAsset> assets;

    bool operator==(const DesktopReleaseInfo& o) const {
        return tagName == o.tagName && name == o.name && body == o.body
            && prerelease == o.prerelease && assets == o.assets;
    }
    bool operator!=(const DesktopReleaseInfo& o) const { return !(*this == o); }
};

/// 一次版本检查的完整结果。
struct DesktopVersionResult {
    VersionCheckStatus status{VersionCheckStatus::InvalidResponse};
    DesktopReleaseInfo release;   // 仅当 ``status == Ok`` 时有意义
    bool updateAvailable{false};  // 仅当给出本地版本并成功比较后有意义
    QString detail;               // 供日志或 UI 使用的简要诊断
};

/// 只读版本检查器：全部为静态方法，无内部状态。
class DesktopVersionChecker {
public:
    /// 公开仓库主页地址。
    static QString repositoryUrl();

    /// Gitee v5 ``/releases/latest`` API 地址。
    static QString latestReleaseUrl();

    /// 解析单个 Gitee release 对象（tag 缺失或非法 SemVer 视为 InvalidResponse）。
    static VersionCheckStatus parseReleaseObject(const QJsonObject& obj, DesktopReleaseInfo& out);

    /// 解析 ``/releases`` 数组（新→旧）或 ``/releases/latest`` 单对象响应。
    /// 对数组会挑选 SemVer 最高的合法发布。纯函数，无网络。
    static VersionCheckStatus parseRelease(const QByteArray& json, DesktopReleaseInfo& out);

    /// HTTP 状态 + 响应体的纯解析：非 2xx（404 -> NoRelease，其余 -> InvalidResponse）
    /// 会先判错误，再委托 ``parseRelease``。纯函数，无网络。
    static VersionCheckStatus parseHttpResponse(int httpStatus, const QByteArray& json, DesktopReleaseInfo& out);

    /// 判断 ``release.tagName`` 是否为严格 SemVer，且高于 ``localVersion``。
    /// 复用 ``Updater::compareVersions`` / ``isValidSemVer``。
    static bool isUpgrade(const DesktopReleaseInfo& release, const QString& localVersion);

    /// 同步读取 Gitee 上最新发布（Qt Network，超时即 Offline）。
    /// 返回的结果中 ``status`` 决定后续是否可更新。
    static DesktopVersionResult fetchLatestRelease(int timeoutSeconds = 8);

    /// 结合本地版本做一次完整检查：离线/无效响应 -> 相应状态；
    /// 成功时按 SemVer 比较设置 ``updateAvailable``。
    static DesktopVersionResult check(const QString& localVersion, int timeoutSeconds = 8);
};

}  // namespace dsh::updater
