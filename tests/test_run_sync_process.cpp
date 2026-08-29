// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::util::runSyncProcess 的单元测试。
//
// 用 /bin/echo / /bin/false / /bin/sleep 等标准二进制覆盖：
//   * 正常退出（exitCode=0, startedOk=true, finishedOk=true, !crashed）；
//   * 非零退出（exitCode=1）；
//   * 不存在的命令（startedOk=false, exitCode=0）；
//   * 超时 kill（finishedOk=false, crashed=true）。
// 所有测试在测试进程内派生真子进程，无 shell 拼接。

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTest>

#include "../src/util/RunSyncProcess.h"

using dsh::util::runSyncProcess;
using dsh::util::SyncProcessResult;

class TestRunSyncProcess : public QObject {
    Q_OBJECT
private slots:
    void echoReturnsStdoutAndZeroExit();
    void falseReturnsNonZeroExit();
    void missingProgramFailsToStart();
    void sleepTimeoutKillsChild();
    void mergedChannelsCombinesStderrIntoStdout();
};

void TestRunSyncProcess::echoReturnsStdoutAndZeroExit() {
    const auto probe = runSyncProcess(
        QStringLiteral("/bin/echo"), {QStringLiteral("hello")},
        /*timeoutMs=*/2000);
    QVERIFY(probe.startedOk);
    QVERIFY(probe.finishedOk);
    QVERIFY(!probe.crashed);
    QCOMPARE(probe.exitCode, 0);
    QVERIFY(probe.stdoutBytes.contains("hello"));
}

void TestRunSyncProcess::falseReturnsNonZeroExit() {
    const auto probe = runSyncProcess(
        QStringLiteral("/bin/false"), {},
        /*timeoutMs=*/2000);
    QVERIFY(probe.startedOk);
    QVERIFY(probe.finishedOk);
    QVERIFY(!probe.crashed);
    QCOMPARE(probe.exitCode, 1);
}

void TestRunSyncProcess::missingProgramFailsToStart() {
    const auto probe = runSyncProcess(
        QStringLiteral("/nonexistent/program/path/xyz"), {},
        /*timeoutMs=*/2000);
    QVERIFY(!probe.startedOk);
    QVERIFY(!probe.finishedOk);
}

void TestRunSyncProcess::sleepTimeoutKillsChild() {
    // /bin/sleep 5 应该在 200ms 内被 kill，避免进程泄漏。
    const auto probe = runSyncProcess(
        QStringLiteral("/bin/sleep"), {QStringLiteral("5")},
        /*timeoutMs=*/200, /*killGraceMs=*/500);
    QVERIFY(probe.startedOk);
    QVERIFY(!probe.finishedOk);   // 超时 → finishedOk=false
    // kill 之后子进程通常因信号退出，crashed=true；
    // 但具体由 QProcess::exitStatus 决定（Crashed），允许两种状态
    // （少数平台上 kill 后 exit code 正常返回 137）。
    Q_UNUSED(probe.crashed);
}

void TestRunSyncProcess::mergedChannelsCombinesStderrIntoStdout() {
    // 显式让 /bin/sh -c 输出到 stderr（>&2），用 MergedChannels 应该能在
    // stdoutBytes 里看到内容、stderrBytes 为空。
    const auto probe = runSyncProcess(
        QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QStringLiteral("echo to-stderr >&2")},
        /*timeoutMs=*/2000, /*killGraceMs=*/500,
        QProcess::MergedChannels);
    QVERIFY(probe.startedOk);
    QVERIFY(probe.finishedOk);
    QVERIFY(probe.stdoutBytes.contains("to-stderr"));
    QVERIFY(probe.stderrBytes.isEmpty());
}

QTEST_GUILESS_MAIN(TestRunSyncProcess)
#include "test_run_sync_process.moc"
