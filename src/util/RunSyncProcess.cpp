// SPDX-License-Identifier: MIT
// @author zhouwr
#include "RunSyncProcess.h"

namespace dsh::util {

namespace {
constexpr int kMinTimeoutMs = 100;
}  // namespace

SyncProcessResult runSyncProcess(
    const QString& program,
    const QStringList& args,
    int timeoutMs,
    int killGraceMs,
    QProcess::ProcessChannelMode channelMode) {
    SyncProcessResult result;
    QProcess p;
    p.setProcessChannelMode(channelMode);
    p.start(program, args);
    // QProcess::start 是异步的：用 waitForStarted 确认实际派生成功，
    // 否则在 timeoutMs 内若 QProcess 还没启动完就调 waitForFinished，
    // 会立即返回 false 并可能误判为"超时"。
    // waitForStarted 用与 waitForFinished 相同的 timeoutMs 上限。
    if (!p.waitForStarted(qMax(kMinTimeoutMs, timeoutMs))) {
        result.startedOk = false;
        return result;
    }
    result.startedOk = true;
    if (!p.waitForFinished(qMax(kMinTimeoutMs, timeoutMs))) {
        // 超时：kill 子进程并再等一小段时间收集死亡信号，避免进程泄漏。
        p.kill();
        p.waitForFinished(qMax(kMinTimeoutMs, killGraceMs));
        result.finishedOk = false;
        result.crashed = (p.exitStatus() != QProcess::NormalExit);
        result.exitCode = p.exitCode();
        result.stdoutBytes = p.readAllStandardOutput();
        result.stderrBytes = p.readAllStandardError();
        return result;
    }
    result.finishedOk = true;
    result.crashed = (p.exitStatus() != QProcess::NormalExit);
    result.exitCode = p.exitCode();
    result.stdoutBytes = p.readAllStandardOutput();
    result.stderrBytes = p.readAllStandardError();
    return result;
}

}  // namespace dsh::util
