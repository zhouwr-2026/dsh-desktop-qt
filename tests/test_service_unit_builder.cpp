// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::service::ServiceUnitBuilder 单元测试。
//
// 只测试纯文本生成：用户级/系统级单元输出、非法输入拒绝、以及 systemd
// 安全转义与"无 shell 插值"。绝不写文件、绝不调用 systemctl / QProcess。

#include <QTest>

#include <QString>

#include "../src/service/ServiceUnitBuilder.h"

using dsh::service::ServiceScope;
using dsh::service::ServiceUnitBuilder;
using dsh::service::ServiceUnitResult;
using dsh::service::ServiceUnitSpec;

namespace {

/// 返回一份默认合法的用户级 spec，测试非法输入时再逐项破坏。
ServiceUnitSpec validUserSpec() {
    ServiceUnitSpec spec;
    spec.dshExecutable = QStringLiteral("/usr/bin/dsh");
    spec.user = QStringLiteral("alice");
    spec.workingDirectory = QStringLiteral("/home/alice");
    spec.dshHome = QStringLiteral("/home/alice/.dsh");
    spec.host = QStringLiteral("127.0.0.1");
    spec.port = 3080;
    spec.scope = ServiceScope::User;
    return spec;
}

}  // namespace

class TestServiceUnitBuilder : public QObject {
    Q_OBJECT
private slots:
    void userScopeUnitText();
    void systemScopeUnitTextWithUser();
    void userScopeOmitUser();
    void unitNameIsStable();
    void escapeEnvironmentValue();
    void escapeExecArgument();
    void rejectEmptyExecutable();
    void rejectRelativeExecutable();
    void rejectEmptyWorkingDirectory();
    void rejectRelativeWorkingDirectory();
    void rejectInvalidPort();
    void rejectEmptyHost();
    void rejectNewline();
    void rejectNul();
    void rejectMissingUserForSystemScope();
    void noShellInterpolation();
};

void TestServiceUnitBuilder::userScopeUnitText() {
    ServiceUnitSpec spec = validUserSpec();  // scope 默认 User
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY2(r.ok, qPrintable(r.error));

    const QString expected = QStringLiteral(
        "[Unit]\n"
        "Description=DSH Web backend (official dsh)\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=/usr/bin/dsh web --host 127.0.0.1 --port 3080\n"
        "Environment=\"DSH_HOME=/home/alice/.dsh\"\n"
        "WorkingDirectory=/home/alice\n"
        "Restart=on-failure\n"
        "RestartSec=5s\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n"
    );
    QCOMPARE(r.unitText, expected);
}

void TestServiceUnitBuilder::systemScopeUnitTextWithUser() {
    ServiceUnitSpec spec = validUserSpec();
    spec.scope = ServiceScope::System;
    spec.user = QStringLiteral("bob");
    spec.workingDirectory = QStringLiteral("/home/bob");
    spec.dshHome = QStringLiteral("/home/bob/.dsh");

    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY2(r.ok, qPrintable(r.error));

    const QString expected = QStringLiteral(
        "[Unit]\n"
        "Description=DSH Web backend (official dsh)\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "User=bob\n"
        "ExecStart=/usr/bin/dsh web --host 127.0.0.1 --port 3080\n"
        "Environment=\"DSH_HOME=/home/bob/.dsh\"\n"
        "WorkingDirectory=/home/bob\n"
        "Restart=on-failure\n"
        "RestartSec=5s\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n"
    );
    QCOMPARE(r.unitText, expected);
}

void TestServiceUnitBuilder::userScopeOmitUser() {
    // 用户级语义：即使给了 user，也不输出 User=。
    ServiceUnitSpec spec = validUserSpec();
    spec.user = QStringLiteral("alice");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.unitText.count(QStringLiteral("User=")), 0);
    QVERIFY(!r.unitText.contains(QStringLiteral("User=alice")));
}

void TestServiceUnitBuilder::unitNameIsStable() {
    QCOMPARE(ServiceUnitBuilder::unitName(), QStringLiteral("dsh-web.service"));
    QCOMPARE(ServiceUnitBuilder::unitNameForScope(ServiceScope::User),
             QStringLiteral("dsh-web.service"));
    QCOMPARE(ServiceUnitBuilder::unitNameForScope(ServiceScope::System),
             QStringLiteral("dsh-web.service"));
}

void TestServiceUnitBuilder::escapeEnvironmentValue() {
    // % 转成 %%（specifier 转义）；$ 在 Environment 中不作展开、保持字面。
    QCOMPARE(ServiceUnitBuilder::escapeEnvironmentValue(QStringLiteral("a%b")),
             QStringLiteral("a%%b"));
    QCOMPARE(ServiceUnitBuilder::escapeEnvironmentValue(QStringLiteral("$HOME")),
             QStringLiteral("$HOME"));
    QCOMPARE(ServiceUnitBuilder::escapeEnvironmentValue(QStringLiteral("a\"b")),
             QStringLiteral("a\\\"b"));
}

void TestServiceUnitBuilder::escapeExecArgument() {
    // $ 与 % 都要转义，避免 ExecStart 的变量展开与 specifier 展开。
    QCOMPARE(ServiceUnitBuilder::escapeExecArgument(QStringLiteral("a$b%c")),
             QStringLiteral("a$$b%%c"));
}

void TestServiceUnitBuilder::rejectEmptyExecutable() {
    ServiceUnitSpec spec = validUserSpec();
    spec.dshExecutable = QStringLiteral("");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(r.unitText.isEmpty());
}

void TestServiceUnitBuilder::rejectRelativeExecutable() {
    ServiceUnitSpec spec = validUserSpec();
    spec.dshExecutable = QStringLiteral("usr/bin/dsh");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
}

void TestServiceUnitBuilder::rejectEmptyWorkingDirectory() {
    ServiceUnitSpec spec = validUserSpec();
    spec.workingDirectory = QStringLiteral("");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
}

void TestServiceUnitBuilder::rejectRelativeWorkingDirectory() {
    ServiceUnitSpec spec = validUserSpec();
    spec.workingDirectory = QStringLiteral("home/alice");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
}

void TestServiceUnitBuilder::rejectInvalidPort() {
    // 0、负数、大于 65535 都拒绝。
    for (const int bad : {0, -1, 65536, -100}) {
        ServiceUnitSpec spec = validUserSpec();
        spec.port = bad;
        const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
        QVERIFY2(!r.ok, qPrintable(QStringLiteral("port %1 应为非法").arg(bad)));
    }
    // 合法边界值通过。
    ServiceUnitSpec minSpec = validUserSpec();
    minSpec.port = 1;
    QVERIFY(ServiceUnitBuilder::build(minSpec).ok);
    ServiceUnitSpec maxSpec = validUserSpec();
    maxSpec.port = 65535;
    QVERIFY(ServiceUnitBuilder::build(maxSpec).ok);
}

void TestServiceUnitBuilder::rejectEmptyHost() {
    ServiceUnitSpec spec = validUserSpec();
    spec.host = QStringLiteral("");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestServiceUnitBuilder::rejectNewline() {
    // '\n' 出现在任何关键值里都应拒绝。
    ServiceUnitSpec spec = validUserSpec();
    spec.dshExecutable = QStringLiteral("/usr/bin/\ndsh");
    QVERIFY(!ServiceUnitBuilder::build(spec).ok);

    spec = validUserSpec();
    spec.workingDirectory = QStringLiteral("/home/\nalice");
    QVERIFY(!ServiceUnitBuilder::build(spec).ok);

    spec = validUserSpec();
    spec.dshHome = QStringLiteral("/home/\nalice/.dsh");
    QVERIFY(!ServiceUnitBuilder::build(spec).ok);

    spec = validUserSpec();
    spec.host = QStringLiteral("127.0.\n0.1");
    QVERIFY(!ServiceUnitBuilder::build(spec).ok);

    spec = validUserSpec();
    spec.user = QStringLiteral("ali\nce");
    QVERIFY(!ServiceUnitBuilder::build(spec).ok);
}

void TestServiceUnitBuilder::rejectNul() {
    ServiceUnitSpec spec = validUserSpec();
    // QString 可内嵌 QChar(0)；应被当作 NUL 拒绝。
    spec.dshExecutable = QStringLiteral("/usr/bin/dsh");
    spec.dshHome = QStringLiteral("/home/a") + QString(QChar(0)) + QStringLiteral("/.dsh");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestServiceUnitBuilder::rejectMissingUserForSystemScope() {
    ServiceUnitSpec spec = validUserSpec();
    spec.scope = ServiceScope::System;
    spec.user = QStringLiteral("");
    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestServiceUnitBuilder::noShellInterpolation() {
    ServiceUnitSpec spec = validUserSpec();
    // 注入 shell 元字符：含空格/$(...) 的环境值、含空格的工作目录。
    spec.workingDirectory = QStringLiteral("/home/My User");
    spec.dshHome = QStringLiteral("/home/a b/$(id)");
    spec.host = QStringLiteral("127.0.0.1");
    spec.port = 3080;
    spec.scope = ServiceScope::User;

    const ServiceUnitResult r = ServiceUnitBuilder::build(spec);
    QVERIFY2(r.ok, qPrintable(r.error));

    // 没有 shell 拼接：没有 sh -c、反引号、管道或 ; 命令分隔。
    QVERIFY(!r.unitText.contains(QStringLiteral("sh -c")));
    QVERIFY(!r.unitText.contains(QLatin1Char('`')));
    QVERIFY(!r.unitText.contains(QLatin1Char('|')));
    QVERIFY(!r.unitText.contains(QLatin1Char(';')));

    // 环境值里的空格保留在双引号内、$ 保持字面（Environment 不展开 $）。
    QVERIFY(r.unitText.contains(
        QStringLiteral("Environment=\"DSH_HOME=/home/a b/$(id)\"")));

    // 工作目录含空格时被双引号包裹，保持为单个 systemd token。
    QVERIFY(r.unitText.contains(QStringLiteral("WorkingDirectory=\"/home/My User\"")));

    // ExecStart 直接使用绝对可执行路径，不引入 shell 前缀/包裹。
    QVERIFY(r.unitText.contains(
        QStringLiteral("ExecStart=/usr/bin/dsh web --host 127.0.0.1 --port 3080")));
}

QTEST_GUILESS_MAIN(TestServiceUnitBuilder)
#include "test_service_unit_builder.moc"
