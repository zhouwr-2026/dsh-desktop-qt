// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::Uninstaller 的单元测试。
//
// 重点验证「按 UninstallPlan 执行、且只用显式 QFile/QDir 删除」：
//   * 桌面端产物按计划删除（默认移除）；
//   * 后台 unit 仅在计划允许（RemoveDesktopAndOwnedBackend）时才删除，且
//     通过 QFile/QDir，绝不调用 systemctl/shell；
//   * 官方/外来/指纹不一致的后台默认保留；
//   * 归属状态文件在后台移除后被清理；
//   * 缺失文件不视为错误。
//
// 所有路径都指向 QTemporaryDir 下的临时文件，绝不触碰真实 /usr、/etc、systemd。

#include <QTest>
#include <QTemporaryDir>

#include <QFile>
#include <QFileInfo>

#include "../src/service/ServiceOwnership.h"
#include "../src/service/UninstallPlan.h"
#include "../src/service/Uninstaller.h"

using dsh::service::ConsistencyResult;
using dsh::service::ServiceOrigin;
using dsh::service::ServiceScope;
using dsh::service::UninstallAction;
using dsh::service::UninstallContext;
using dsh::service::UninstallLayout;
using dsh::service::UninstallOutcome;
using dsh::service::UninstallPlan;
using dsh::service::Uninstaller;

namespace {

/// 在 \p dir 下创建一个空文件并返回其路径。
QString makeFile(const QString& dir, const QString& name) {
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return path;  // 失败时返回路径；由后续 exists() 断言捕获。
    }
    f.write(name.toUtf8());
    f.close();
    return path;
}

/// 构造一个指向 \p dir 的布局，并在其中创建一组桌面端产物。
UninstallLayout desktopLayout(const QString& dir) {
    UninstallLayout l;
    l.desktopBinary = makeFile(dir, "dsh-desktop");
    l.updaterBinary = makeFile(dir, "dsh-desktop-updater");
    l.desktopEntryPaths = {
        makeFile(dir, "dsh-desktop.desktop"),
        makeFile(dir, "autostart.desktop"),
    };
    l.iconFiles = {
        makeFile(dir, "dsh-whale.svg"),
        makeFile(dir, "dsh-whale-white.svg"),
    };
    l.themeExportHelper = makeFile(dir, "dsh-theme-export");
    l.themeUnitFiles = {
        makeFile(dir, "dsh-theme-export.service"),
        makeFile(dir, "dsh-theme-export.path"),
    };
    l.themeEnableSymlinks = {
        makeFile(dir, "enable-service"),
        makeFile(dir, "enable-path"),
    };
    l.themeRunFile = makeFile(dir, "theme-run");
    l.polkitAction = makeFile(dir, "org.dsh.desktop.policy");
    l.socketPaths = { makeFile(dir, "dsh-desktop.sock") };
    return l;
}

/// 后台只读 + 授权快照：由桌面端拥有且匹配，移除可用，目标已知。
UninstallContext ownedRemovableCtx() {
    UninstallContext ctx;
    ctx.desktopInstalled = true;
    ctx.backendDetected = true;
    ctx.scope = ServiceScope::User;
    ctx.origin = ServiceOrigin::ProvisionedByDesktop;
    ctx.ownershipConsistency = ConsistencyResult::Match;
    ctx.removeBackendService = true;
    ctx.secondaryConfirmed = true;
    ctx.serviceRemovalAvailable = true;
    ctx.targetScopePathKnown = true;
    return ctx;
}

/// 官方后台（非桌面端拥有）：即使勾选也绝不移除。
UninstallContext officialCtx() {
    UninstallContext ctx = ownedRemovableCtx();
    ctx.origin = ServiceOrigin::ExistingOfficial;
    ctx.ownershipConsistency = ConsistencyResult::NotRecorded;
    return ctx;
}

}  // namespace

class TestUninstaller : public QObject {
    Q_OBJECT
private slots:
    // 纯辅助。
    void backendUnitPath_user();
    void backendUnitPath_system();
    void backendOwnedByDesktop_onlyOwned();
    // 桌面端产物移除。
    void removeDesktopFiles_removesLayout();
    void removeDesktopFiles_missingIsOk();
    void desktopRemovalRespectsPlan();
    // 后台 unit 移除仅在计划允许时。
    void backendRemovedWhenPermitted();
    void backendRetainedForOfficial();
    void backendRemovalNotAttempted_unchecked();
    void backendRemovalNotAttempted_notDetected();
    // 归属状态清理。
    void ownershipRecordCleared();
    void ownershipRecordPreservedWhenRetained();
    // apply() 组合。
    void apply_ownedRemovesDesktopAndUnit();
};

void TestUninstaller::backendUnitPath_user() {
    UninstallLayout l;
    l.userUnitFilePath = QStringLiteral("/home/u/.config/systemd/user/dsh-web.service");
    l.systemUnitFilePath = QStringLiteral("/etc/systemd/system/dsh-web.service");
    QCOMPARE(Uninstaller::backendUnitPath(ServiceScope::User, l),
             l.userUnitFilePath);
}

void TestUninstaller::backendUnitPath_system() {
    UninstallLayout l;
    l.userUnitFilePath = QStringLiteral("/home/u/...");
    l.systemUnitFilePath = QStringLiteral("/etc/systemd/system/dsh-web.service");
    QCOMPARE(Uninstaller::backendUnitPath(ServiceScope::System, l),
             l.systemUnitFilePath);
}

void TestUninstaller::backendOwnedByDesktop_onlyOwned() {
    QVERIFY(Uninstaller::backendOwnedByDesktop(ownedRemovableCtx()));
    QVERIFY(!Uninstaller::backendOwnedByDesktop(officialCtx()));
}

void TestUninstaller::removeDesktopFiles_removesLayout() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    UninstallContext ctx;
    ctx.desktopInstalled = true;

    Uninstaller u(UninstallPlan::make(ctx), l);
    QVERIFY(u.apply());
    const UninstallOutcome& out = u.outcome();

    QVERIFY(out.desktopRemovalAttempted);
    QVERIFY(out.desktopRemoved);
    QVERIFY(out.errors.isEmpty());
    // 每个产物都被删除。
    QVERIFY(!QFile::exists(l.desktopBinary));
    QVERIFY(!QFile::exists(l.updaterBinary));
    for (const QString& p : l.desktopEntryPaths) QVERIFY(!QFile::exists(p));
    for (const QString& p : l.iconFiles) QVERIFY(!QFile::exists(p));
    QVERIFY(!QFile::exists(l.themeExportHelper));
    for (const QString& p : l.themeUnitFiles) QVERIFY(!QFile::exists(p));
    for (const QString& p : l.themeEnableSymlinks) QVERIFY(!QFile::exists(p));
    QVERIFY(!QFile::exists(l.themeRunFile));
    QVERIFY(!QFile::exists(l.polkitAction));
    for (const QString& p : l.socketPaths) QVERIFY(!QFile::exists(p));
}

void TestUninstaller::removeDesktopFiles_missingIsOk() {
    // 不存在的产物：不算错误，计入 missingPaths。
    UninstallLayout l;
    l.desktopBinary = QStringLiteral("/nonexistent/dsh-desktop");
    l.updaterBinary = QStringLiteral("/nonexistent/dsh-desktop-updater");
    UninstallContext ctx;
    ctx.desktopInstalled = true;

    Uninstaller u(UninstallPlan::make(ctx), l);
    QVERIFY(u.apply());
    const UninstallOutcome& out = u.outcome();
    QVERIFY(out.desktopRemoved);
    QVERIFY(out.errors.isEmpty());
    QVERIFY(out.missingPaths.contains(l.desktopBinary));
    QVERIFY(!out.backendRemovalAttempted);
}

void TestUninstaller::desktopRemovalRespectsPlan() {
    // 桌面端未安装：desktopRemoval()==false，不删除任何桌面产物。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    UninstallContext ctx;
    ctx.desktopInstalled = false;  // 桌面端本就不存在。

    Uninstaller u(UninstallPlan::make(ctx), l);
    QVERIFY(u.apply());
    QVERIFY(!u.outcome().desktopRemovalAttempted);
    QVERIFY(QFile::exists(l.desktopBinary));  // 未被删除。
}

void TestUninstaller::backendRemovedWhenPermitted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    l.userUnitFilePath = makeFile(dir.path(), "dsh-web.service");
    l.ownershipStateFile = dir.path() + QStringLiteral("/services-owned.json");

    // 先在归属状态中记录该用户级 unit，便于清除记录。
    {
        dsh::service::ServiceOwnership own(l.ownershipStateFile);
        own.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                   QStringLiteral("/usr/bin/dsh web"));
        QVERIFY(own.save());
    }

    const UninstallPlan plan = UninstallPlan::make(ownedRemovableCtx());
    QCOMPARE(plan.action(), UninstallAction::RemoveDesktopAndOwnedBackend);
    QVERIFY(plan.backendRemoval());

    Uninstaller u(plan, l);
    QVERIFY(u.apply());
    const UninstallOutcome& out = u.outcome();

    QVERIFY(out.backendRemovalAttempted);
    QVERIFY(out.backendUnitRemoved);
    QVERIFY(!QFile::exists(l.userUnitFilePath));  // unit 文件被显式删除。
    QVERIFY(out.ownershipStateUpdated);

    // 归属记录已被清除。
    dsh::service::ServiceOwnership own(l.ownershipStateFile);
    QVERIFY(own.load());
    QVERIFY(!own.contains(QStringLiteral("dsh-web.service"), ServiceScope::User));
}

void TestUninstaller::backendRetainedForOfficial() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    l.userUnitFilePath = makeFile(dir.path(), "dsh-web.service");
    l.ownershipStateFile = dir.path() + QStringLiteral("/services-owned.json");
    // 官方后台不在归属记录中。
    {
        dsh::service::ServiceOwnership own(l.ownershipStateFile);
        QVERIFY(own.save());
    }

    const UninstallPlan plan = UninstallPlan::make(officialCtx());
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnownedOrForeign);
    QVERIFY(!plan.backendRemoval());

    Uninstaller u(plan, l);
    QVERIFY(u.apply());
    const UninstallOutcome& out = u.outcome();

    QVERIFY(!out.backendRemovalAttempted);
    QVERIFY(!out.backendUnitRemoved);
    QVERIFY(QFile::exists(l.userUnitFilePath));  // 官方 unit 保留。
}

void TestUninstaller::backendRemovalNotAttempted_unchecked() {
    // 未勾选「同时卸载后台」：即便存在拥有后台，也只保留（RetainBackendBecauseUnchecked）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    l.userUnitFilePath = makeFile(dir.path(), "dsh-web.service");

    UninstallContext ctx = ownedRemovableCtx();
    ctx.removeBackendService = false;  // 默认不勾选。

    const UninstallPlan plan = UninstallPlan::make(ctx);
    QCOMPARE(plan.action(), UninstallAction::RetainBackendBecauseUnchecked);
    QVERIFY(!plan.backendRemoval());

    Uninstaller u(plan, l);
    QVERIFY(u.apply());
    QVERIFY(!u.outcome().backendRemovalAttempted);
    QVERIFY(QFile::exists(l.userUnitFilePath));
}

void TestUninstaller::backendRemovalNotAttempted_notDetected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    l.userUnitFilePath = makeFile(dir.path(), "dsh-web.service");

    UninstallContext ctx = ownedRemovableCtx();
    ctx.backendDetected = false;

    const UninstallPlan plan = UninstallPlan::make(ctx);
    QVERIFY(!plan.backendRemoval());

    Uninstaller u(plan, l);
    QVERIFY(u.apply());
    QVERIFY(!u.outcome().backendRemovalAttempted);
    QVERIFY(QFile::exists(l.userUnitFilePath));
}

void TestUninstaller::ownershipRecordCleared() {
    // 直接测试 clearOwnershipRecord（后台移除后清理状态）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l;
    l.ownershipStateFile = dir.path() + QStringLiteral("/services-owned.json");
    {
        dsh::service::ServiceOwnership own(l.ownershipStateFile);
        own.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                   QStringLiteral("/usr/bin/dsh web"));
        QVERIFY(own.save());
    }

    const UninstallPlan plan = UninstallPlan::make(ownedRemovableCtx());
    Uninstaller u(plan, l);
    QVERIFY(u.clearOwnershipRecord());
    QVERIFY(u.outcome().ownershipStateUpdated);

    dsh::service::ServiceOwnership own(l.ownershipStateFile);
    QVERIFY(own.load());
    QVERIFY(!own.contains(QStringLiteral("dsh-web.service"), ServiceScope::User));
}

void TestUninstaller::ownershipRecordPreservedWhenRetained() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l;
    l.ownershipStateFile = dir.path() + QStringLiteral("/services-owned.json");
    {
        dsh::service::ServiceOwnership own(l.ownershipStateFile);
        own.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                   QStringLiteral("/usr/bin/dsh web"));
        QVERIFY(own.save());
    }

    const UninstallPlan plan = UninstallPlan::make(officialCtx());
    Uninstaller u(plan, l);
    QVERIFY(u.apply());
    QVERIFY(!u.outcome().ownershipStateUpdated);
    QVERIFY(!u.outcome().backendRemovalAttempted);
}

void TestUninstaller::apply_ownedRemovesDesktopAndUnit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UninstallLayout l = desktopLayout(dir.path());
    l.userUnitFilePath = makeFile(dir.path(), "dsh-web.service");
    l.ownershipStateFile = dir.path() + QStringLiteral("/services-owned.json");
    {
        dsh::service::ServiceOwnership own(l.ownershipStateFile);
        own.record(QStringLiteral("dsh-web.service"), ServiceScope::User,
                   QStringLiteral("/usr/bin/dsh web"));
        QVERIFY(own.save());
    }

    const UninstallPlan plan = UninstallPlan::make(ownedRemovableCtx());
    Uninstaller u(plan, l);
    QVERIFY(u.apply());

    const UninstallOutcome& out = u.outcome();
    QVERIFY(out.desktopRemoved);
    QVERIFY(out.backendUnitRemoved);
    QVERIFY(!QFile::exists(l.userUnitFilePath));
    QVERIFY(!QFile::exists(l.desktopBinary));
}

QTEST_GUILESS_MAIN(TestUninstaller)
#include "test_uninstaller.moc"
