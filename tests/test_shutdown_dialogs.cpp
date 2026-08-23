// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::app::ExitDialog / dsh::app::RestartDialog 的单元测试。
//
// 只驱动对话框的默认状态（勾选框文字、默认勾选/不勾选、未运行时锁定），
// 不弹出任何模态对话框；控件需 offscreen 平台。
// 通过 QT_QPA_PLATFORM=offscreen 运行。

#include <QTest>

#include <QCheckBox>

#include "../src/app/ExitDialog.h"
#include "../src/app/RestartDialog.h"

using dsh::app::ExitDialog;
using dsh::app::RestartDialog;

class TestShutdownDialogs : public QObject {
    Q_OBJECT
private slots:
    // ExitDialog：后端运行中 → 勾选"停止"，默认不勾选
    void exitCheckbox_uncheckedByDefault_whenBackendRunning();
    void exitCheckbox_text_isStop_whenBackendRunning();
    void exitCheckbox_text_isSupervisedStop_whenBackendRunningAndSupervised();

    // ExitDialog：后端未运行 → 锁定为勾选 + 禁用
    void exitCheckbox_checkedAndDisabled_whenBackendNotRunning();
    void exitCheckbox_text_isStart_whenBackendNotRunning();
    void exitReturnsTrue_whenBackendNotRunning();

    // ExitDialog：不可管理（远程后端）→ 不渲染勾选
    void exitCheckbox_hidden_whenUnmanaged();

    // RestartDialog：与 ExitDialog 对称
    void restartCheckbox_uncheckedByDefault_whenBackendRunning();
    void restartCheckbox_text_isRestart_whenBackendRunning();
    void restartCheckbox_text_isSupervisedRestart_whenBackendRunningAndSupervised();
    void restartCheckbox_checkedAndDisabled_whenBackendNotRunning();
    void restartCheckbox_text_isStart_whenBackendNotRunning();
    void restartCheckbox_hidden_whenUnmanaged();
};

void TestShutdownDialogs::exitCheckbox_uncheckedByDefault_whenBackendRunning() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/true,
                 /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(!cb->isChecked());
    QVERIFY(!d.stopBackgroundService());
}

void TestShutdownDialogs::exitCheckbox_text_isStop_whenBackendRunning() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/true,
                 /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时停止后台 dsh web 服务  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::exitCheckbox_text_isSupervisedStop_whenBackendRunningAndSupervised() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/true,
                 /*backendRunning=*/true,
                 /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时停止由桌面端拉起的 dsh web 子进程  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::exitCheckbox_checkedAndDisabled_whenBackendNotRunning() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/false,
                 /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(cb->isChecked());
    QVERIFY(!cb->isEnabled());   // 锁定：未运行时无论如何都要拉起
    QVERIFY(d.stopBackgroundService());  // stopBackgroundService() 只反映勾选
}

void TestShutdownDialogs::exitCheckbox_text_isStart_whenBackendNotRunning() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/false,
                 /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时启动 DSH 后台服务  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::exitReturnsTrue_whenBackendNotRunning() {
    ExitDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/false,
                 /*canManageBackend=*/true);
    // 锁定勾选 → 调用方读到 true。语义上"stop"在这里其实表示"启动"
    // （见类注释），本测试只确保接口返回正确值。
    QVERIFY(d.stopBackgroundService());
}

void TestShutdownDialogs::exitCheckbox_hidden_whenUnmanaged() {
    ExitDialog d(0, QStringLiteral("http://remote.example:3080"),
                 /*supervisedMode=*/false,
                 /*backendRunning=*/true,
                 /*canManageBackend=*/false);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb == nullptr);
    QVERIFY(!d.stopBackgroundService());
}

void TestShutdownDialogs::restartCheckbox_uncheckedByDefault_whenBackendRunning() {
    RestartDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                    /*supervisedMode=*/false,
                    /*backendRunning=*/true,
                    /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(!cb->isChecked());
    QVERIFY(!d.restartBackgroundService());
}

void TestShutdownDialogs::restartCheckbox_text_isRestart_whenBackendRunning() {
    RestartDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                    /*supervisedMode=*/false,
                    /*backendRunning=*/true,
                    /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时重启后台 dsh web 服务  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::restartCheckbox_text_isSupervisedRestart_whenBackendRunningAndSupervised() {
    RestartDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                    /*supervisedMode=*/true,
                    /*backendRunning=*/true,
                    /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时重启由桌面端拉起的 dsh web 子进程  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::restartCheckbox_checkedAndDisabled_whenBackendNotRunning() {
    RestartDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                    /*supervisedMode=*/false,
                    /*backendRunning=*/false,
                    /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QVERIFY(cb->isChecked());
    QVERIFY(!cb->isEnabled());
    QVERIFY(d.restartBackgroundService());
}

void TestShutdownDialogs::restartCheckbox_text_isStart_whenBackendNotRunning() {
    RestartDialog d(0, QStringLiteral("http://127.0.0.1:3080"),
                    /*supervisedMode=*/false,
                    /*backendRunning=*/false,
                    /*canManageBackend=*/true);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb != nullptr);
    QCOMPARE(cb->text(),
             QStringLiteral("同时启动 DSH 后台服务  (http://127.0.0.1:3080)"));
}

void TestShutdownDialogs::restartCheckbox_hidden_whenUnmanaged() {
    RestartDialog d(0, QStringLiteral("http://remote.example:3080"),
                    /*supervisedMode=*/false,
                    /*backendRunning=*/true,
                    /*canManageBackend=*/false);
    auto* cb = d.findChild<QCheckBox*>();
    QVERIFY(cb == nullptr);
    QVERIFY(!d.restartBackgroundService());
}

QTEST_MAIN(TestShutdownDialogs)
#include "test_shutdown_dialogs.moc"
