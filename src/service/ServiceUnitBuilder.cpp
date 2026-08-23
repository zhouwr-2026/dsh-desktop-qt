// SPDX-License-Identifier: MIT
// @author zhouwr

#include "ServiceUnitBuilder.h"

#include <QFileInfo>

namespace dsh::service {

namespace {

/// 是否包含 systemd / 文件路径中危险的换行、回车或 NUL 字符。
bool containsForbiddenChar(const QString& text) {
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == 0 || u == '\n' || u == '\r') return true;
    }
    return false;
}

bool isAbsolutePath(const QString& path) {
    return QFileInfo(path).isAbsolute();
}

/// 双引号内部的安全转义。expandDollar 为 true 时把 ``$`` 转为 ``$$``
/// （ExecStart/Exec* 会做 ``$VAR``/``${VAR}`` 变量展开），否则保留字面
/// ``$``（Environment=、WorkingDirectory= 等设置不作 $ 展开；但 ``%``
/// 在 unit 文件中始终是 specifier，需转成 ``%%``）。
QString escapeInsideQuotes(const QString& text, bool expandDollar) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\\') {
            out += QStringLiteral("\\\\");
        } else if (u == '"') {
            out += QStringLiteral("\\\"");
        } else if (u == '%') {
            out += QStringLiteral("%%");  // specifier 转义
        } else if (expandDollar && u == '$') {
            out += QStringLiteral("$$");  // ExecStart 字面 $ 转义
        } else {
            out += c;
        }
    }
    return out;
}

/// 文本内含空白、引号、反斜杠、``%``（或 ExecStart 场景下的 ``$``）时，
/// 必须用双引号包裹，以免被 systemd 拆成多个 token。
bool needsQuotes(const QString& text, bool expandDollar) {
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (c.isSpace() || u == '"' || u == '\'' || u == '\\' || u == '%'
            || (expandDollar && u == '$')) {
            return true;
        }
    }
    return false;
}

/// 生成一个 systemd token：需要时用双引号包裹并做内部转义。
QString token(const QString& text, bool expandDollar) {
    const QString inner = escapeInsideQuotes(text, expandDollar);
    if (!needsQuotes(text, expandDollar)) return inner;
    return QLatin1Char('"') + inner + QLatin1Char('"');
}

}  // namespace

ServiceUnitResult ServiceUnitBuilder::build(const ServiceUnitSpec& spec) {
    ServiceUnitResult result;

    const auto fail = [&result](const QString& message) {
        result.ok = false;
        result.error = message;
        result.unitText.clear();
        return result;
    };

    // ------------------------------------------------------------------
    // 校验：空/不安全路径、非绝对路径、非法端口、含换行/NUL 的值。
    // ------------------------------------------------------------------
    if (spec.dshExecutable.isEmpty())
        return fail(QStringLiteral("dsh executable path is empty"));
    if (containsForbiddenChar(spec.dshExecutable))
        return fail(QStringLiteral("dsh executable path contains a newline/NUL character"));
    if (!isAbsolutePath(spec.dshExecutable))
        return fail(QStringLiteral("dsh executable path is not absolute: ") + spec.dshExecutable);

    if (spec.workingDirectory.isEmpty())
        return fail(QStringLiteral("working directory is empty"));
    if (containsForbiddenChar(spec.workingDirectory))
        return fail(QStringLiteral("working directory contains a newline/NUL character"));
    if (!isAbsolutePath(spec.workingDirectory))
        return fail(QStringLiteral("working directory is not absolute: ") + spec.workingDirectory);

    if (spec.port < 1 || spec.port > 65535)
        return fail(QStringLiteral("invalid port: ") + QString::number(spec.port));

    if (spec.host.isEmpty())
        return fail(QStringLiteral("host is empty"));
    if (containsForbiddenChar(spec.host))
        return fail(QStringLiteral("host contains a newline/NUL character"));

    if (containsForbiddenChar(spec.dshHome))
        return fail(QStringLiteral("DSH_HOME contains a newline/NUL character"));

    if (containsForbiddenChar(spec.user))
        return fail(QStringLiteral("user contains a newline/NUL character"));
    if (spec.scope == ServiceScope::System && spec.user.isEmpty())
        return fail(QStringLiteral("user is required for system-scope units"));

    // ------------------------------------------------------------------
    // 拼装单元文本。不写文件、不执行 systemctl、无 shell 插值。
    // ------------------------------------------------------------------
    QString unit;
    unit += QStringLiteral("[Unit]\n");
    unit += QStringLiteral("Description=DSH Web backend (official dsh)\n");
    unit += QLatin1Char('\n');

    unit += QStringLiteral("[Service]\n");
    unit += QStringLiteral("Type=simple\n");
    if (spec.scope == ServiceScope::System) {
        unit += QStringLiteral("User=") + token(spec.user, /*expandDollar=*/false)
                + QLatin1Char('\n');
    }

    unit += QStringLiteral("ExecStart=")
        + token(spec.dshExecutable, /*expandDollar=*/true)
        + QStringLiteral(" web --host ")
        + token(spec.host, /*expandDollar=*/true)
        + QStringLiteral(" --port ")
        + QString::number(spec.port) + QLatin1Char('\n');

    if (!spec.dshHome.isEmpty()) {
        // systemd 的 Environment= 赋值：值含空白等字符时必须把整个赋值包在
        // 双引号内（"DSH_HOME=..."）。这里始终包裹，保证任意值都安全。
        unit += QStringLiteral("Environment=\"DSH_HOME=")
            + escapeInsideQuotes(spec.dshHome, /*expandDollar=*/false)
            + QStringLiteral("\"\n");
    }

    unit += QStringLiteral("WorkingDirectory=")
        + token(spec.workingDirectory, /*expandDollar=*/false) + QLatin1Char('\n');
    unit += QStringLiteral("Restart=on-failure\n");
    unit += QStringLiteral("RestartSec=5s\n");
    unit += QLatin1Char('\n');

    unit += QStringLiteral("[Install]\n");
    unit += (spec.scope == ServiceScope::System)
        ? QStringLiteral("WantedBy=multi-user.target\n")
        : QStringLiteral("WantedBy=default.target\n");

    result.ok = true;
    result.unitText = unit;
    return result;
}

QString ServiceUnitBuilder::unitName() {
    return QStringLiteral("dsh-web.service");
}

QString ServiceUnitBuilder::unitNameForScope(ServiceScope) {
    return unitName();
}

QString ServiceUnitBuilder::escapeEnvironmentValue(const QString& value) {
    return escapeInsideQuotes(value, /*expandDollar=*/false);
}

QString ServiceUnitBuilder::escapeExecArgument(const QString& argument) {
    return escapeInsideQuotes(argument, /*expandDollar=*/true);
}

}  // namespace dsh::service
