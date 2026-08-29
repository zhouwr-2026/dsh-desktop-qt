// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 同步 QProcess 运行工具：派生子进程、等待完成、超时则 kill。
//
// 单一权威实现：消除散落在 SystemdBackend / ServiceDiscovery / Updater 等
// 多处匿名 ``QProcess p; ... waitForFinished(...); kill()`` 模板的重复，
// 并把其中一处缺失的超时 kill（is-active 探测的进程泄漏 bug）补上。
//
// 副作用：会派生一个子进程（最多 timeoutMs + killGraceMs）；调用方应在非热
// 路径使用。绝不拼接 shell：所有调用方均传入 ``QStringList`` 显式 argv。
//
// (变更理由: 结构审查不确定项 + is-active 进程泄漏 bug 修复)

#pragma once

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace dsh::util {

/// 同步 QProcess 运行结果。
struct SyncProcessResult {
    /// QProcess::start() 调用本身是否成功（waitForStarted 在超时内完成）。
    /// ``startedOk=false`` 时 ``exitCode`` / stdout / stderr 都无意义。
    bool startedOk{false};
    /// 子进程是否在 ``timeoutMs`` 内正常结束（exitStatus == NormalExit
    /// 且 waitForFinished 返回 true）。``finishedOk=false`` 表示超时被 kill。
    bool finishedOk{false};
    /// 是否因信号崩溃退出（exitStatus != NormalExit）。
    bool crashed{false};
    /// 子进程退出码（仅 startedOk 时有意义）；超时被 kill 时仍读取 ``exitCode``
    /// 可能为 0 或其它已设置值。
    int exitCode{0};
    /// stdout 内容；``channelMode = SeparateChannels`` 时不含 stderr。
    QByteArray stdoutBytes;
    /// stderr 内容；``channelMode = MergedChannels`` 时为空（输出全在 stdoutBytes）。
    QByteArray stderrBytes;
};

/// 同步运行 ``program <args>``：派生 QProcess、显式 argv 传递（绝不拼接 shell）、
/// 等启动、等完成；超时则 ``kill()`` 并再等 ``killGraceMs`` 收集死亡信号。
///
/// \param program     要执行的程序名（建议来自 ``QStandardPaths::findExecutable``
///                    或固定的 /usr/bin 路径，避免 PATH 劫持）。
/// \param args        显式 argv 列表。
/// \param timeoutMs   等待子进程结束的总超时（毫秒）；< 100 时视作 100ms。
/// \param killGraceMs 超时后调用 ``kill()`` 到 ``waitForFinished`` 返回的容忍
///                    时长（毫秒）；< 100 时视作 100ms。
/// \param channelMode 通道合并模式；默认 SeparateChannels（stdout/stderr 分开
///                    收集），MergedChannels 把 stderr 合并进 stdoutBytes。
SyncProcessResult runSyncProcess(
    const QString& program,
    const QStringList& args,
    int timeoutMs,
    int killGraceMs = 3000,
    QProcess::ProcessChannelMode channelMode = QProcess::SeparateChannels);

}  // namespace dsh::util
