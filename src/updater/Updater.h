// SPDX-License-Identifier: MIT
// @author zhouwr
//
// `@deepseek-ai/dsh` 更新检查器。
//
// 桌面端不维护任何代码本身，只关心系统装的 dsh CLI 是否落后于 npm 注册
// 表——落后了就让用户一键升级。真正执行升级时通过 ``pkexec`` 弹 polkit
// 密码框，绝不静默提权。

#pragma once

#include <QObject>
#include <QString>

namespace dsh::updater {

/// README 与 installer 声明的 @deepseek-ai/dsh 最低支持版本。
/// (变更理由: 依赖审查建议 P0-3)
inline constexpr const char* kMinimumDshVersion = "0.1.0-rc.7";

/// 判断 ``current`` 是否满足最低版本 ``kMinimumDshVersion``。
/// - current 为空（无法探测 dsh 版本）→ 返回 ``Unknown``；
/// - current 非空但非合法 SemVer → 返回 ``Invalid``；
/// - 否则按严格 SemVer 比较，返回 ``TooOld`` 或 ``Ok``。
enum class MinimumVersionCheck { Ok, TooOld, Invalid, Unknown };
MinimumVersionCheck checkMinimumDshVersion(const QString& current);

struct Status {
    QString current;             // 当前本地版本，空字符串表示未安装
    QString latest;              // npm latest 版本，空字符串表示网络失败
    bool updateAvailable{false};
    QString detail;
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    void setTargetVersion(const QString& version) { targetVersion_ = version; }

    /// 读取本地 dsh CLI 版本（例如 ``0.1.0-rc.7``）。
    static QString readLocalVersion();

    /// 查询 npm 注册表获取最新稳定版本；网络失败时返回空字符串。
    static QString fetchLatestVersion(int timeoutSeconds = 8);

    /// 综合本地版本与 npm 版本，按严格 SemVer 比较。
    static Status check(int timeoutSeconds = 8);

    /// 使用检查时确认的明确版本执行提权更新，避免 ``latest`` 在确认后漂移。
    bool performUpdate();

public slots:
    void performUpdateAsync();

signals:
    void log(const QString& line);
    void updateFinished(bool ok);

private:
    QString targetVersion_;
};

/// 严格 SemVer 2.0 比较（暴露给单元测试使用）。
/// \return 负 / 零 / 正，类似 ``strcmp``。
int compareVersions(const QString& a, const QString& b);

/// 判断字符串是否为严格 SemVer 2.0 版本（允许可选 v 前缀）。
/// 复用内部 ``parseSemVer``，与 ``compareVersions`` 共享同一套判定逻辑。
bool isValidSemVer(const QString& version);

}  // namespace dsh::updater
