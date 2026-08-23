// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 卸载器 CLI 入口。
//
// 行为：
//   1. 只读检测环境（桌面端是否已安装、后台 dsh-web.service 是否被探测到，
//      其 scope/origin/归属一致性），折叠成 UninstallContext；
//   2. 交互模式（默认）弹出原生 UninstallDialog：展示后台来源/范围、复选
//      「同时卸载 DSH 后台服务」（默认不勾选）、勾选后二次确认，并解释归属
//      拒绝（非桌面端拥有的后台将被保留）；
//   3. 由 UninstallPlan 做确定性决策；仅在计划允许（RemoveDesktopAndOwnedBackend）
//      时才停止后台（经 DshServiceManager）并删除后台 unit（QFile/QDir，
//      绝不调用 systemctl / shell 删除）；桌面端产物按计划删除；官方服务与
//      数据默认保留；
//   4. 非交互模式（--yes / offscreen）仅用于测试：采用默认决策（仅卸载桌面端，
//      保留后台），不弹窗、不触碰任何真实后台。
//
// 本文件不执行任何项目管理脚本；生产语法/行为由使用者保证。

#include "app/UninstallDialog.h"
#include "service/DshServiceManager.h"
#include "service/ServiceDiscovery.h"
#include "service/ServiceOwnership.h"
#include "service/Uninstaller.h"
#include "service/UninstallPlan.h"

#include "BuildVersion.h"

#include <QCoreApplication>
#include <QApplication>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <unistd.h>

namespace {

using dsh::service::ConsistencyResult;
using dsh::service::DetectedService;
using dsh::service::DiscoveredService;
using dsh::service::DiscoveryResult;
using dsh::service::OperationResult;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::ServiceResult;
using dsh::service::UninstallContext;
using dsh::service::UninstallLayout;
using dsh::service::UninstallPlan;

/// 把绝对路径重写到 \p root 之下（测试用沙箱；\p root 为空时不改写）。
QString rewritePath(const QString& root, QString path) {
    if (root.isEmpty() || path.isEmpty()) return path;
    path = path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!path.startsWith(QLatin1Char('/'))) return path;
    if (path.size() > 1 && path[1] == QLatin1Char(':')) return path;  // Windows 盘符。
    QString bare = path;
    while (bare.startsWith(QLatin1Char('/'))) bare.remove(0, 1);
    return root + QLatin1Char('/') + bare;
}

UninstallLayout sandboxLayout(const UninstallLayout& in, const QString& root) {
    if (root.isEmpty()) return in;
    UninstallLayout out = in;
    out.desktopBinary = rewritePath(root, in.desktopBinary);
    out.updaterBinary = rewritePath(root, in.updaterBinary);
    out.uninstallerBinary = rewritePath(root, in.uninstallerBinary);
    out.desktopEntryPaths.clear();
    for (const QString& p : in.desktopEntryPaths)
        out.desktopEntryPaths.append(rewritePath(root, p));
    out.iconFiles.clear();
    for (const QString& p : in.iconFiles)
        out.iconFiles.append(rewritePath(root, p));
    out.themeExportHelper = rewritePath(root, in.themeExportHelper);
    out.themeUnitFiles.clear();
    for (const QString& p : in.themeUnitFiles)
        out.themeUnitFiles.append(rewritePath(root, p));
    out.themeEnableSymlinks.clear();
    for (const QString& p : in.themeEnableSymlinks)
        out.themeEnableSymlinks.append(rewritePath(root, p));
    out.themeRunFile = rewritePath(root, in.themeRunFile);
    out.polkitAction = rewritePath(root, in.polkitAction);
    out.socketPaths.clear();
    for (const QString& p : in.socketPaths)
        out.socketPaths.append(rewritePath(root, p));
    out.userUnitFilePath = rewritePath(root, in.userUnitFilePath);
    out.systemUnitFilePath = rewritePath(root, in.systemUnitFilePath);
    out.ownershipStateFile = rewritePath(root, in.ownershipStateFile);
    out.ownedBackendDataDir = rewritePath(root, in.ownedBackendDataDir);
    return out;
}

/// 只读检测：折叠成 UninstallContext，并把选中的 DetectedService 透传给调用方
/// （供需要停止服务时使用）。
UninstallContext detectContext(const UninstallLayout& layout,
                               DetectedService* detectedOut = nullptr) {
    UninstallContext ctx;

    // 桌面端是否已安装（以主程序二进制或桌面条目为准）。
    ctx.desktopInstalled =
        QFile::exists(layout.desktopBinary)
        || (!layout.desktopEntryPaths.isEmpty()
            && QFile::exists(layout.desktopEntryPaths.first()));

    // 归属状态（只读加载）。
    dsh::service::ServiceOwnership ownership(layout.ownershipStateFile);
    ownership.load();

    // 只读实况发现（systemctl show 系统域 + 用户域），绝不变更系统状态。
    const DiscoveryResult dr =
        dsh::service::discoverDshWebService(QStringLiteral("dsh-web.service"));
    const DiscoveredService* sel = dr.selected();
    if (sel && sel->valid) {
        const dsh::service::ServiceInfo& info = sel->info;
        ctx.backendDetected = true;
        ctx.scope = info.scope;
        ctx.origin = ownership.contains(info.unitName, info.scope)
            ? ServiceOrigin::ProvisionedByDesktop
            : ServiceOrigin::ExistingOfficial;
        ctx.ownershipConsistency =
            ownership.checkConsistency(info.unitName, info.scope, info.execStart);
        ctx.targetScopePathKnown = true;
        // 移除可用性：用户级 unit 位于当前用户可写目录；系统级 unit 需 root。
        ctx.serviceRemovalAvailable =
            (info.scope == ServiceScope::User) || (::geteuid() == 0);

        if (detectedOut) {
            detectedOut->unitName = info.unitName;
            detectedOut->scope = info.scope;
            detectedOut->host = info.host;
            detectedOut->port = info.port;
            detectedOut->valid = true;
        }
    } else if (dr.systemctlAvailable == false) {
        // systemctl 缺失：当作未检测到后台，仅卸载桌面端。
        ctx.backendDetected = false;
    } else {
        // 未发现有效后台：仅卸载桌面端。
        ctx.backendDetected = false;
    }
    return ctx;
}

/// 经 DshServiceManager 停止后台服务（仅当用户已确认；同步等待结果）。
bool stopBackendService(const DetectedService& detected) {
    dsh::service::DshServiceManager mgr;
    mgr.setDetectedService(detected);
    if (!mgr.hasTarget() || !mgr.isValidated()) return false;

    QEventLoop loop;
    bool finished = false;
    bool ok = false;
    QObject::connect(&mgr, &dsh::service::DshServiceManager::operationFinished,
                     [&](const OperationResult& r) {
                         finished = true;
                         ok = (r.result == ServiceResult::Success);
                         loop.quit();
                     });
    const qint64 id = mgr.stop();
    if (id < 0) return false;
    // 安全超时：事件循环最长等待 20s。
    QTimer::singleShot(20000, &loop, [&]() {
        if (!finished) {
            finished = true;
            ok = false;
            loop.quit();
        }
    });
    loop.exec();
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName(QStringLiteral("anywhere-labs"));
    QCoreApplication::setApplicationName(QStringLiteral("dsh-desktop"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(DSH_DESKTOP_VERSION));

    // 交互模式需要 QApplication（Widgets）；--yes 无 GUI，可用 QCoreApplication。
    bool offscreen = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--offscreen") == 0
            || qstrcmp(argv[i], "--yes") == 0
            || qstrcmp(argv[i], "--help") == 0
            || qstrcmp(argv[i], "-h") == 0
            || qstrcmp(argv[i], "--version") == 0
            || qstrcmp(argv[i], "-v") == 0) {
            offscreen = true;
            break;
        }
    }
    if (offscreen && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    std::unique_ptr<QCoreApplication> app;
    if (offscreen) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QApplication>(argc, argv);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("DSH Desktop 卸载器——移除桌面端产物，并按启用状态保留/同时移除后台服务"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption yesOpt(QStringLiteral("yes"),
                              QStringLiteral("非交互模式（测试用）：采用默认决策，仅卸载桌面端，保留后台。"));
    QCommandLineOption offscreenOpt(QStringLiteral("offscreen"),
                                    QStringLiteral("强制 offscreen 平台（测试用）。"));
    QCommandLineOption prefixOpt(QStringLiteral("prefix"),
                                 QStringLiteral("安装前缀（缺省从可执行文件位置推导）。"),
                                 QStringLiteral("dir"));
    QCommandLineOption rootOpt(QStringLiteral("root"),
                               QStringLiteral("把绝对路径沙箱化到该目录（测试用）。"),
                               QStringLiteral("dir"));
    QCommandLineOption removeDataOpt(QStringLiteral("remove-backend-data"),
                                     QStringLiteral("同时移除由桌面端拥有的后台数据目录（默认保留）。"));
    parser.addOption(yesOpt);
    parser.addOption(offscreenOpt);
    parser.addOption(prefixOpt);
    parser.addOption(rootOpt);
    parser.addOption(removeDataOpt);
    parser.process(*app);

    const bool yesMode = parser.isSet(yesOpt);
    const QString sandboxRoot = parser.value(rootOpt);

    // 从可执行文件位置推导安装前缀（<prefix>/bin/dsh-desktop-uninstaller）。
    QString prefix = parser.value(prefixOpt);
    if (prefix.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        prefix = QFileInfo(appDir).absoluteFilePath();
        // 若 /bin 是安装前缀一部分，取父目录作为前缀。
        QString parent = QFileInfo(prefix).absolutePath();
        if (QFileInfo(prefix).fileName() == QLatin1String("bin")) {
            prefix = parent;
        }
    }

    UninstallLayout layout =
        dsh::service::Uninstaller::layoutForPrefix(prefix);
    layout = sandboxLayout(layout, sandboxRoot);
    layout.ownedBackendDataDir = sandboxRoot.isEmpty()
        ? QString()  // 不启用数据删除默认路径；仅当显式指定时由调用方提供。
        : layout.ownedBackendDataDir;

    DetectedService detected;
    UninstallContext ctx = detectContext(layout, &detected);

    if (!yesMode) {
        dsh::app::UninstallDialog dialog(ctx);
        if (dialog.exec() != QDialog::Accepted) {
            std::fprintf(stderr, "dsh-desktop-uninstaller: 已取消卸载。\n");
            return 0;
        }
        ctx = dialog.mergedContext();
    }

    // 非交互/测试模式：采用安全默认——仅卸载桌面端，后台保留。
    if (yesMode) {
        ctx.removeBackendService = false;
        ctx.secondaryConfirmed = false;
    }

    const UninstallPlan plan = UninstallPlan::make(ctx);

    // 仅在计划允许且用户已二次确认时停止后台服务（经 DshServiceManager）。
    if (plan.backendRemoval() && detected.valid) {
        std::fprintf(stdout, "dsh-desktop-uninstaller: 停止后台服务 %s (%s)\n",
                     detected.unitName.toLocal8Bit().constData(),
                     dsh::service::scopeToString(detected.scope).toLocal8Bit().constData());
        if (!stopBackendService(detected)) {
            std::fprintf(stderr,
                         "dsh-desktop-uninstaller: 停止后台服务失败，已保留后台。\n");
        }
    }

    dsh::service::Uninstaller uninstaller(plan, layout,
                                          parser.isSet(removeDataOpt));
    const bool ok = uninstaller.apply();
    const dsh::service::UninstallOutcome& out = uninstaller.outcome();

    for (const QString& path : out.removedPaths) {
        std::fprintf(stdout, "  已删除: %s\n", path.toLocal8Bit().constData());
    }
    for (const QString& err : out.errors) {
        std::fprintf(stderr, "  删除失败: %s\n", err.toLocal8Bit().constData());
    }
    std::fprintf(stdout,
                 "dsh-desktop-uninstaller: 决策=%s, 桌面端=%s, 后台unit=%s, 后台数据=%s\n",
                 dsh::service::uninstallActionToString(plan.action()).toLocal8Bit().constData(),
                 out.desktopRemoved ? "已移除" : "未移除",
                 out.backendUnitRemoved ? "已移除" : "未移除/保留",
                 out.backendDataRemoved ? "已移除" : "保留");

    return ok ? 0 : 1;
}
