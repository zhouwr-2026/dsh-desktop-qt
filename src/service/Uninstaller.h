// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 卸载执行器（无 GUI、无 QProcess、无 shell）：把 UninstallPlan 的决策落为
// 对桌面端安装产物与（仅在计划允许时）后台 systemd unit 的显式 QFile / QDir
// 删除。
//
// 安全约定（对应 docs/DSH-DESKTOP-SERVICE-PLAN.zh.md 第 9 节）：
//   * 桌面端产物：只要是桌面包的一部分，就按 UninstallPlan::desktopRemoval()
//     删除（二进制、桌面条目、自启动、图标、主题导出辅助服务等）。
//   * 后台 unit：只在 UninstallPlan::backendRemoval() 为真（即
//     RemoveDesktopAndOwnedBackend）时，用 QFile::remove 删除 unit 文件，
//     绝不调用 systemctl / shell，绝不删除官方/外来/指纹不一致的后台。
//   * 后台数据：默认保留（用户配置、日志、WebEngine 数据、下载文件、DSH_HOME）。
//     只有显式启用 removeBackendData（本类构造参数，默认关闭）才会删除由桌面端
//     拥有的数据目录。
//   * 服务停止：本类不负责；由上层 CLI 在二次确认后经 DshServiceManager 完成。
//     本类只处理纯文件系统操作，便于在临时目录中确定性地单元测试。
//
// 所有路径都从 UninstallLayout 注入（默认由 layoutForPrefix() 从安装前缀推导，
// 测试可指向临时目录），因此本类不触碰真实 /usr、/etc 或 systemd。

#pragma once

#include "ServiceInfo.h"
#include "UninstallPlan.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace dsh::service {

/// 一次卸载需要的所有路径。空字符串表示"跳过该项"。
struct UninstallLayout {
    // 桌面端安装产物。
    QString desktopBinary;                    ///< 主程序二进制（如 /usr/bin/dsh-desktop）。
    QString updaterBinary;                    ///< 自更新助手（如 /usr/bin/dsh-desktop-updater）。
    QString uninstallerBinary;                ///< 卸载器自身（跳过，避免自删正在运行的二进制）。
    QStringList desktopEntryPaths;            ///< .desktop：applications 与 autostart。
    QStringList iconFiles;                    ///< 所有黑白鲸鱼 SVG 图标。
    QString themeExportHelper;                ///< /usr/lib/dsh-desktop/dsh-theme-export。
    QStringList themeUnitFiles;               ///< /usr/lib/systemd/system/dsh-theme-export.{service,path}。
    QStringList themeEnableSymlinks;          ///< /etc/systemd/system/dsh-theme-export.{service,path}。
    QString themeRunFile;                     ///< /run/dsh-desktop/theme。
    QString polkitAction;                     ///< /usr/share/polkit-1/actions/org.dsh.desktop.policy。
    QStringList socketPaths;                  ///< 单实例 socket 残留（每用户一个）。

    // 后台服务 unit / 数据。
    QString userUnitFilePath;                 ///< ~/.config/systemd/user/dsh-web.service。
    QString systemUnitFilePath;               ///< /etc/systemd/system/dsh-web.service。
    QString ownershipStateFile;               ///< 归属状态文件（可为空：不更新）。
    QString ownedBackendDataDir;              ///< 由桌面端拥有的后台数据目录（默认不删）。
};

/// 一次卸载的逐步结果（供展示/测试）。
struct UninstallOutcome {
    bool desktopRemovalAttempted{false};      ///< 是否执行过桌面端产物删除。
    bool backendRemovalAttempted{false};      ///< 是否执行过后台 unit 删除。
    bool desktopRemoved{false};               ///< 桌面端产物已删除（无缺失/无失败）。
    bool backendUnitRemoved{false};           ///< 后台 unit 文件已删除。
    bool backendDataRemoved{false};           ///< 后台数据目录已删除。
    bool ownershipStateUpdated{false};        ///< 归属状态已更新（清除了记录）。

    QVector<QString> removedPaths;            ///< 已删除路径。
    QVector<QString> missingPaths;            ///< 不存在、无需删除的路径（非错误）。
    QVector<QString> errors;                  ///< 删除失败的路径与原因。

    /// 是否有致命错误（不含"未找到"）。
    bool success() const { return errors.isEmpty(); }
};

/// 卸载执行器。其余约定见文件头注释。
class Uninstaller {
public:
    /// 用已决策的计划 + 注入的布局构造。
    Uninstaller(const UninstallPlan& plan, const UninstallLayout& layout,
                bool removeBackendData = false);

    /// 便捷构造：对给定检测快照先用 UninstallPlan::make 决策，再执行。
    Uninstaller(const UninstallContext& context, const UninstallLayout& layout,
                bool removeBackendData = false);

    /// 按计划执行卸载（桌面端产物 + 计划允许时的后台 unit）。
    bool apply();

    // -----------------------------------------------------------------------
    // 可单独测试的步骤
    // -----------------------------------------------------------------------

    /// 删除桌面端安装产物（尊重 plan.desktopRemoval()）。
    bool removeDesktopFiles();

    /// 删除后台 systemd unit 文件（仅 plan.backendRemoval() 时；QFile/QDir）。
    bool removeBackendUnit();

    /// 清除归属状态文件中该 unit+scope 的记录（仅后台移除后）。
    bool clearOwnershipRecord();

    // -----------------------------------------------------------------------
    // 纯辅助（不接触磁盘/进程，可测试）
    // -----------------------------------------------------------------------

    /// 从安装前缀推导默认布局（无前缀时返回空布局）。
    static UninstallLayout layoutForPrefix(const QString& prefix);

    /// 根据后台 scope 解析 unit 文件路径（scope 已知且 layout 有对应路径时）。
    static QString backendUnitPath(ServiceScope scope, const UninstallLayout& layout);

    /// 后台是否"由桌面端创建且指纹一致"（移除判据，与 UninstallPlan/对话框一致）。
    static bool backendOwnedByDesktop(const UninstallContext& context);

    const UninstallPlan& plan() const { return plan_; }
    const UninstallLayout& layout() const { return layout_; }
    const UninstallOutcome& outcome() const { return outcome_; }

private:
    bool removePath(const QString& path);   ///< 单文件/符号链接删除；未找到视为成功。

    UninstallPlan plan_;
    UninstallLayout layout_;
    bool removeBackendData_{false};
    UninstallOutcome outcome_;
};

}  // namespace dsh::service
