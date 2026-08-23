// SPDX-License-Identifier: MIT
// @author zhouwr

#include "SystemctlShowParser.h"

#include <QFileInfo>

namespace dsh::service {

namespace {

// 尊重单/双引号与反斜杠转义的切分，去掉包围引号；其余空白作为分隔。
QStringList tokenize(const QString& text) {
    QStringList out;
    QString current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;
    for (const QChar c : text) {
        if (escaped) {
            current.append(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && !inSingleQuote) {
            escaped = true;
            continue;
        }
        if (c == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
            continue;
        }
        if (c == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }
        if (c.isSpace() && !inSingleQuote && !inDoubleQuote) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
            continue;
        }
        current.append(c);
    }
    if (!current.isEmpty()) out.append(current);
    return out;
}

LifecycleState stateFromActiveState(const QString& activeState) {
    if (activeState == QLatin1String("active")) return LifecycleState::Active;
    if (activeState == QLatin1String("inactive")) return LifecycleState::Inactive;
    if (activeState == QLatin1String("failed")) return LifecycleState::Failed;
    if (activeState == QLatin1String("activating")) return LifecycleState::Activating;
    return LifecycleState::Unknown;
}

QString flagValue(const QStringList& argv, const QString& flag, bool& found) {
    found = false;
    for (int i = 0; i < argv.size(); ++i) {
        const QString& argument = argv.at(i);
        if (argument == flag) {
            if (i + 1 < argv.size()) {
                found = true;
                return argv.at(i + 1);
            }
            return {};
        }
        if (argument.startsWith(flag + QLatin1Char('='))) {
            found = true;
            return argument.mid(flag.size() + 1);
        }
    }
    return {};
}

QString parseEnvironmentValue(const QStringList& environment, const QString& key) {
    const QString prefix = key + QLatin1Char('=');
    for (const QString& entry : environment) {
        if (entry.startsWith(prefix)) return entry.mid(prefix.size());
    }
    return {};
}

QStringList parseEnvironmentFiles(const QString& value) {
    QStringList paths;
    for (const QString& token : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const int open = token.indexOf(QLatin1Char('('));
        const QString path = (open >= 0 ? token.left(open) : token).trimmed();
        if (!path.isEmpty()) paths.append(path);
    }
    return paths;
}

}  // namespace

ServiceInfo parseSystemctlShow(const QString& text,
                               const QString& unitName,
                               ServiceScope scope) {
    ServiceInfo info;
    info.unitName = unitName;
    info.scope = scope;

    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const int equal = line.indexOf(QLatin1Char('='));
        if (equal < 0) continue;
        const QString key = line.left(equal);
        const QString value = line.mid(equal + 1);
        if (key == QLatin1String("LoadState")) {
            info.loadState = value;
        } else if (key == QLatin1String("ActiveState")) {
            info.activeState = value;
        } else if (key == QLatin1String("SubState")) {
            info.subState = value;
        } else if (key == QLatin1String("ExecStart")) {
            info.execStart = value;
        } else if (key == QLatin1String("User")) {
            info.user = value;
        } else if (key == QLatin1String("WorkingDirectory")) {
            info.workingDirectory = value;
        } else if (key == QLatin1String("Environment")) {
            info.environment = tokenize(value);
        } else if (key == QLatin1String("EnvironmentFiles")) {
            info.environmentFiles = parseEnvironmentFiles(value);
        } else if (key == QLatin1String("MainPID")) {
            bool ok = false;
            const qint64 pid = value.toLongLong(&ok);
            info.mainPid = (ok && pid > 0) ? pid : -1;
        }
    }

    info.state = (info.loadState.isEmpty() || info.loadState == QLatin1String("loaded"))
        ? stateFromActiveState(info.activeState)
        : LifecycleState::Unknown;

    const QStringList argv = parseExecStartArgv(info.execStart);
    info.invokesOfficialDshWeb = invokesOfficialDshWeb(info.execStart);

    bool hostFound = false;
    bool portFound = false;
    const QString host = flagValue(argv, QLatin1String("--host"), hostFound);
    const QString port = flagValue(argv, QLatin1String("--port"), portFound);
    if (hostFound && !host.isEmpty()) {
        info.host = host;
        info.hostIsDefault = false;
    }
    if (portFound) {
        bool ok = false;
        const int parsedPort = port.toInt(&ok);
        if (ok && parsedPort > 0) {
            info.port = parsedPort;
            info.portIsDefault = false;
        }
    }

    const QString dshHome = parseEnvironmentValue(info.environment, QLatin1String("DSH_HOME"));
    if (!dshHome.isEmpty()) {
        info.dshHome = dshHome;
        info.dshHomeSet = true;
    }

    return info;
}

QStringList parseExecStartArgv(const QString& execStart) {
    const QString trimmed = execStart.trimmed();
    if (trimmed.isEmpty()) return {};

    const QString marker = QLatin1String("argv[]=");
    const int index = trimmed.indexOf(marker);
    QString argvPart;
    if (index >= 0) {
        QString rest = trimmed.mid(index + marker.size());
        const int separator = rest.indexOf(QLatin1String(" ; "));
        if (separator >= 0) rest = rest.left(separator);
        argvPart = rest;
    } else {
        argvPart = trimmed;
    }
    return tokenize(argvPart);
}

bool invokesOfficialDshWeb(const QString& execStart) {
    const QStringList argv = parseExecStartArgv(execStart);
    if (argv.size() < 2) return false;
    const QString executable = QFileInfo(argv.at(0)).fileName();
    if (executable != QLatin1String("dsh")) return false;
    if (argv.at(1) == QLatin1String("web")) return true;
    return argv.at(1) == QLatin1String("--profile")
        && argv.size() >= 3
        && argv.at(2) == QLatin1String("web");
}

}  // namespace dsh::service
