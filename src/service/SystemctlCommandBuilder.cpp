// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SystemctlCommandBuilder.h"

namespace dsh::service {

QStringList SystemctlCommandBuilder::systemctlArguments(ServiceOperation operation,
                                                      ServiceScope scope,
                                                      const QString& unitName) {
    QStringList args;
    if (scope == ServiceScope::User) args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager");
    switch (operation) {
        case ServiceOperation::Start:
            args << QStringLiteral("start");
            break;
        case ServiceOperation::Stop:
            args << QStringLiteral("stop");
            break;
        case ServiceOperation::Restart:
            args << QStringLiteral("restart");
            break;
        case ServiceOperation::Status:
        case ServiceOperation::Discovery:
            args << QStringLiteral("show");
            break;
        case ServiceOperation::DaemonReload:
            args << QStringLiteral("daemon-reload");
            break;
        case ServiceOperation::Enable:
            args << QStringLiteral("enable");
            break;
        case ServiceOperation::JournalTail:
            // journalctl 另行构造。
            break;
    }
    // daemon-reload 只刷新配置，不带 unit 名；其余命令把 unit 名作为尾部参数。
    if (operation != ServiceOperation::DaemonReload) {
        args << unitName;
    }
    return args;
}

QStringList SystemctlCommandBuilder::journalctlArguments(ServiceScope scope,
                                                       const QString& unitName,
                                                       int lines,
                                                       bool follow) {
    QStringList args;
    if (scope == ServiceScope::User) args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager");
    if (lines > 0) args << QStringLiteral("-n") << QString::number(lines);
    args << QStringLiteral("-u") << unitName;
    if (follow) args << QStringLiteral("-f");
    return args;
}

bool SystemctlCommandBuilder::operationNeedsElevation(ServiceOperation operation,
                                                     ServiceScope scope,
                                                     qint64 euid) {
    if (scope != ServiceScope::System) return false;
    switch (operation) {
        case ServiceOperation::Start:
        case ServiceOperation::Stop:
        case ServiceOperation::Restart:
        case ServiceOperation::DaemonReload:
        case ServiceOperation::Enable:
            return euid != 0;
        default:
            return false;
    }
}

ResolvedCommand SystemctlCommandBuilder::resolveCommand(ServiceOperation operation,
                                                      ServiceScope scope,
                                                      const QString& unitName,
                                                      const QString& systemctlExe,
                                                      const QString& pkexecExe,
                                                      qint64 euid) {
    ResolvedCommand cmd;
    const QStringList sysArgs = systemctlArguments(operation, scope, unitName);
    if (operationNeedsElevation(operation, scope, euid) && !pkexecExe.isEmpty()) {
        cmd.program = pkexecExe;
        cmd.arguments = {QStringLiteral("--disable-internal-agent"), systemctlExe};
        cmd.arguments.append(sysArgs);
    } else {
        cmd.program = systemctlExe;
        cmd.arguments = sysArgs;
    }
    return cmd;
}

bool SystemctlCommandBuilder::isValidUnitName(const QString& unitName, QString* error) {
    auto fail = [&error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (unitName.isEmpty()) return fail(QStringLiteral("unit 名不能为空"));
    if (unitName.size() > 255) return fail(QStringLiteral("unit 名过长"));
    if (unitName.contains(QLatin1Char('/')))
        return fail(QStringLiteral("unit 名不能包含路径分隔符 '/'"));
    if (unitName.startsWith(QLatin1Char('.')))
        return fail(QStringLiteral("unit 名不能以 '.' 开头"));

    for (const QChar c : unitName) {
        const ushort u = c.unicode();
        if (u == 0 || c.isSpace() || u == '"' || u == '\'' || u == '\\' || u == '`'
            || u == ';' || u == '&' || u == '|' || u == '$' || u == '(' || u == ')'
            || u < 0x20) {
            return fail(QStringLiteral("unit 名包含非法字符"));
        }
    }

    static const QStringList kSuffixes = {
        QStringLiteral(".service"), QStringLiteral(".socket"), QStringLiteral(".target"),
        QStringLiteral(".timer"),   QStringLiteral(".path"),   QStringLiteral(".mount"),
        QStringLiteral(".automount"), QStringLiteral(".slice"), QStringLiteral(".scope"),
    };
    bool okSuffix = false;
    for (const QString& suffix : kSuffixes) {
        if (unitName.endsWith(suffix)) {
            okSuffix = true;
            break;
        }
    }
    if (!okSuffix) return fail(QStringLiteral("unit 名必须以已知 systemd 单元后缀结尾"));
    return true;
}

}  // namespace dsh::service
