// SPDX-License-Identifier: MIT
// @author zhouwr
#include "Uninstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <unistd.h>

namespace dsh::service {

Uninstaller::Uninstaller(const UninstallPlan& plan, const UninstallLayout& layout,
                         bool removeBackendData)
    : plan_(plan), layout_(layout), removeBackendData_(removeBackendData) {}

Uninstaller::Uninstaller(const UninstallContext& context, const UninstallLayout& layout,
                         bool removeBackendData)
    : Uninstaller(UninstallPlan::make(context), layout, removeBackendData) {}

bool Uninstaller::apply() {
    bool ok = true;
    if (plan_.desktopRemoval()) {
        if (!removeDesktopFiles()) ok = false;
    }
    if (plan_.backendRemoval()) {
        if (!removeBackendUnit()) ok = false;
        if (!clearOwnershipRecord()) ok = false;
        if (removeBackendData_) {
            outcome_.backendDataRemoved =
                removePath(layout_.ownedBackendDataDir);
            if (!outcome_.backendDataRemoved) ok = false;
        }
    }
    return ok;
}

bool Uninstaller::removeDesktopFiles() {
    outcome_.desktopRemovalAttempted = true;

    // 卸载器自身：绝不删除当前正在运行的二进制。
    const QString selfExe =
        QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();

    auto removeOne = [this, &selfExe](const QString& path) {
        if (path.isEmpty()) return;
        if (!selfExe.isEmpty()
            && QFileInfo(path).absoluteFilePath() == selfExe) {
            // 跳过自身，避免对正在运行的卸载器删文件。
            outcome_.missingPaths.append(path);
            return;
        }
        removePath(path);
    };

    removeOne(layout_.desktopBinary);
    removeOne(layout_.updaterBinary);
    removeOne(layout_.uninstallerBinary);
    for (const QString& p : layout_.desktopEntryPaths) removeOne(p);
    for (const QString& p : layout_.iconFiles) removeOne(p);
    removeOne(layout_.themeExportHelper);
    for (const QString& p : layout_.themeUnitFiles) removeOne(p);
    for (const QString& p : layout_.themeEnableSymlinks) removeOne(p);
    removeOne(layout_.themeRunFile);
    removeOne(layout_.polkitAction);
    for (const QString& p : layout_.socketPaths) removeOne(p);

    outcome_.desktopRemoved = outcome_.errors.isEmpty();
    return outcome_.desktopRemoved;
}

bool Uninstaller::removeBackendUnit() {
    outcome_.backendRemovalAttempted = true;
    if (!plan_.backendRemoval()) {
        // 计划不允许移除后台：绝不动手（防御）。
        return true;
    }
    const QString unitPath = backendUnitPath(plan_.scope(), layout_);
    if (unitPath.isEmpty()) {
        outcome_.errors.append(QStringLiteral("无法解析后台 unit 路径（scope=%1）")
                                   .arg(scopeToString(plan_.scope())));
        return false;
    }
    const bool ok = removePath(unitPath);
    outcome_.backendUnitRemoved = ok;
    return ok;
}

bool Uninstaller::clearOwnershipRecord() {
    if (layout_.ownershipStateFile.isEmpty()) return true;

    ServiceOwnership ownership(layout_.ownershipStateFile);
    if (ownership.load()) {
        // 后台 unit 名与 ServiceProvisioner 保持一致。
        const QString unitName = QStringLiteral("dsh-web.service");
        if (ownership.removeRecord(unitName, plan_.scope())) {
            outcome_.ownershipStateUpdated = ownership.save();
        } else {
            // 记录本来就不存在：无需更新，视为成功。
            outcome_.ownershipStateUpdated = true;
        }
    }
    return outcome_.ownershipStateUpdated;
}

bool Uninstaller::removePath(const QString& path) {
    if (path.isEmpty()) return true;  // 空路径跳过，视为成功。

    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        // 不存在：已移除，不算错误。
        outcome_.missingPaths.append(path);
        return true;
    }

    if (info.isDir()) {
        QDir dir(path);
        if (!dir.removeRecursively()) {
            outcome_.errors.append(QStringLiteral("无法删除目录：%1").arg(path));
            return false;
        }
    } else {
        QFile file(path);
        if (!file.remove()) {
            outcome_.errors.append(QStringLiteral("无法删除文件：%1（%2）")
                                       .arg(path, file.errorString()));
            return false;
        }
    }
    outcome_.removedPaths.append(path);
    return true;
}

bool Uninstaller::backendOwnedByDesktop(const UninstallContext& context) {
    return context.backendDetected
        && context.origin == ServiceOrigin::ProvisionedByDesktop
        && context.ownershipConsistency == ConsistencyResult::Match;
}

QString Uninstaller::backendUnitPath(ServiceScope scope, const UninstallLayout& layout) {
    if (scope == ServiceScope::User) return layout.userUnitFilePath;
    return layout.systemUnitFilePath;
}

UninstallLayout Uninstaller::layoutForPrefix(const QString& prefix) {
    UninstallLayout layout;
    if (prefix.isEmpty()) return layout;

    const QString bin = prefix + QStringLiteral("/bin");
    const QString data = prefix + QStringLiteral("/share");
    const QString lib = prefix + QStringLiteral("/lib");
    const QString libexec = prefix + QStringLiteral("/lib/dsh-desktop");

    layout.desktopBinary = bin + QStringLiteral("/dsh-desktop");
    layout.updaterBinary = bin + QStringLiteral("/dsh-desktop-updater");
    layout.desktopEntryPaths = {
        data + QStringLiteral("/applications/dsh-desktop.desktop"),
        QStringLiteral("/etc/xdg/autostart/dsh-desktop.desktop"),
    };
    // 清理旧版本遗留的高优先级副本。
    layout.desktopEntryPaths.append(
        QStringLiteral("/usr/local/share/applications/dsh-desktop.desktop"));

    // 图标：全新安装只产生 SVG，但卸载时需同时清理旧版本遗留的 PNG 位图。
    layout.iconFiles = {
        data + QStringLiteral("/icons/hicolor/scalable/apps/dsh-whale-black.svg"),
        data + QStringLiteral("/icons/hicolor/scalable/apps/dsh-whale-white.svg"),
    };
    for (int size : {16, 22, 24, 32, 48, 64}) {
        layout.iconFiles.append(
            data + QStringLiteral("/icons/breeze/apps/%1/dsh-whale.svg").arg(size));
        layout.iconFiles.append(
            data + QStringLiteral("/icons/breeze-dark/apps/%1/dsh-whale.svg").arg(size));
    }
    for (int size : {22, 32, 48, 64, 128, 256}) {
        layout.iconFiles.append(
            data + QStringLiteral("/icons/hicolor/%1x%1/apps/dsh-whale-black.png").arg(size));
        layout.iconFiles.append(
            data + QStringLiteral("/icons/hicolor/%1x%1/apps/dsh-whale-white.png").arg(size));
    }

    layout.themeExportHelper = libexec + QStringLiteral("/dsh-theme-export");
    layout.themeUnitFiles = {
        lib + QStringLiteral("/systemd/system/dsh-theme-export.service"),
        lib + QStringLiteral("/systemd/system/dsh-theme-export.path"),
    };
    layout.themeEnableSymlinks = {
        QStringLiteral("/etc/systemd/system/dsh-theme-export.service"),
        QStringLiteral("/etc/systemd/system/dsh-theme-export.path"),
    };
    layout.themeRunFile = QStringLiteral("/run/dsh-desktop/theme");
    layout.polkitAction =
        QStringLiteral("/usr/share/polkit-1/actions/org.dsh.desktop.policy");

    // 单实例 socket：清理当前用户（uninstaller 由当前用户运行）。
    const quint32 uid = ::geteuid();
    layout.socketPaths = {
        QStringLiteral("/run/user/%1/dsh-desktop.sock").arg(uid),
    };

    // 后台 unit 与归属状态。
    layout.userUnitFilePath =
        QDir::homePath() + QStringLiteral("/.config/systemd/user/dsh-web.service");
    layout.systemUnitFilePath =
        QStringLiteral("/etc/systemd/system/dsh-web.service");
    layout.ownershipStateFile = ServiceOwnership::defaultStateFilePath();
    return layout;
}

}  // namespace dsh::service
