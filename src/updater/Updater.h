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

    /// 读取本地 dsh CLI 版本（例如 ``0.1.0-rc.7``）。
    static QString readLocalVersion();

    /// 查询 npm 注册表获取最新稳定版本；网络失败时返回空字符串。
    static QString fetchLatestVersion(int timeoutSeconds = 8);

    /// 综合本地版本与 npm 版本，按严格 SemVer 比较。
    static Status check(int timeoutSeconds = 8);

    /// 执行 ``pkexec npm install -g @deepseek-ai/dsh@latest``，
    /// 输出逐行通过 ``log`` 信号转发。返回 ``(ok, 摘要)``。
    bool performUpdate(const QString& label = QStringLiteral("update"));

public slots:
    void performUpdateAsync();

signals:
    void log(const QString& line);
    void updateFinished(bool ok);

private:
    QString label_;
};

/// 严格 SemVer 2.0 比较（暴露给单元测试使用）。
/// \return 负 / 零 / 正，类似 ``strcmp``。
int compareVersions(const QString& a, const QString& b);

}  // namespace dsh::updater
