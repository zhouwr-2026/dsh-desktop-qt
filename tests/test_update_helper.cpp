// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh::updater::DesktopUpdateHelper 的单元测试。
//
// 覆盖三层可验证的纯逻辑：
//   * SHA-256 计算/校验（已知向量、大小写、长度/非法字符拒收、文件缺失）；
//   * 路径校验（源/目标的各种非法情形被拒收、合法情形放行）；
//   * 原子替换（保留权限、全新安装、非法目标被拒）。
//
// 全部测试在临时目录里自建自清，不触碰真实系统文件、不触发网络/进程/shell。

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

#include "../src/updater/DesktopUpdateHelper.h"

using dsh::updater::atomicReplace;
using dsh::updater::computeSha256;
using dsh::updater::isValidSha256Hex;
using dsh::updater::validateDestination;
using dsh::updater::validateSource;
using dsh::updater::verifySha256;

namespace {

// "abc" 的公开已知 SHA-256 摘要。
const char* const kAbcSha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

/// 写入文件并返回是否成功（不含任何 QVERIFY，避免在辅助函数里断言）。
bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool ok = (f.write(data) == data.size());
    f.close();
    return ok;
}

/// 把文件权限设为 rwxr-xr-x（0755），便于"可执行"断言。
void makeExecutable(const QString& path) {
    QFile::setPermissions(
        path,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther);
}

}  // namespace

class TestUpdateHelper : public QObject {
    Q_OBJECT
private slots:
    // --- SHA-256 计算 ---
    void sha256KnownVector();
    void sha256MatchesOwnDigest();
    void sha256FileMissing();

    // --- SHA-256 十六进制校验 ---
    void isValidSha256HexValidLower();
    void isValidSha256HexValidUpper();
    void isValidSha256HexRejectsLength();
    void isValidSha256HexRejectsNonHex();
    void isValidSha256HexRejectsEmpty();

    // --- SHA-256 比对 ---
    void verifySha256Match();
    void verifySha256Mismatch();
    void verifySha256InvalidExpected();

    // --- 源文件校验 ---
    void validateSourceMissing();
    void validateSourceDirectory();
    void validateSourceNotExecutable();
    void validateSourceOk();

    // --- 目标路径校验 ---
    void validateDestinationIsDirectory();
    void validateDestinationParentNotDirectory();
    void validateDestinationMissingParent();
    void validateDestinationOk();

    // --- 原子替换 ---
    void atomicReplacePreservesPermissions();
    void atomicReplaceFreshInstall();
    void atomicReplaceInvalidDestination();
};

void TestUpdateHelper::sha256KnownVector() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.txt"));
    QVERIFY(writeFile(path, "abc"));

    QString digest;
    QVERIFY(computeSha256(path, &digest));
    QCOMPARE(digest, QString::fromLatin1(kAbcSha256));
}

void TestUpdateHelper::sha256MatchesOwnDigest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.bin"));
    const QByteArray payload = QStringLiteral("DSH self-update payload").toUtf8();
    QVERIFY(writeFile(path, payload));

    QString digest;
    QVERIFY(computeSha256(path, &digest));
    QVERIFY(isValidSha256Hex(digest));

    // 用全大写期望值也应匹配（忽略大小写）。
    QString upper = digest.toUpper();
    QVERIFY(verifySha256(path, upper));
}

void TestUpdateHelper::sha256FileMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("does-not-exist.bin"));

    QString digest;
    QString err;
    QVERIFY(!computeSha256(path, &digest, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(digest.isEmpty());
}

void TestUpdateHelper::isValidSha256HexValidLower() {
    QVERIFY(isValidSha256Hex(QString(64, QLatin1Char('a'))));
    QVERIFY(isValidSha256Hex(QString::fromLatin1(kAbcSha256)));
}

void TestUpdateHelper::isValidSha256HexValidUpper() {
    QVERIFY(isValidSha256Hex(QString(64, QLatin1Char('A'))));
    QVERIFY(isValidSha256Hex(QString::fromLatin1(kAbcSha256).toUpper()));
}

void TestUpdateHelper::isValidSha256HexRejectsLength() {
    QVERIFY(!isValidSha256Hex(QString(63, QLatin1Char('a'))));
    QVERIFY(!isValidSha256Hex(QString(65, QLatin1Char('a'))));
}

void TestUpdateHelper::isValidSha256HexRejectsNonHex() {
    QVERIFY(!isValidSha256Hex(QStringLiteral("g") + QString(63, QLatin1Char('a'))));
    QVERIFY(!isValidSha256Hex(QString(32, QLatin1Char('a')) + QString(32, QLatin1Char('z'))));
}

void TestUpdateHelper::isValidSha256HexRejectsEmpty() {
    QVERIFY(!isValidSha256Hex(QString()));
}

void TestUpdateHelper::verifySha256Match() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("payload.bin"));
    QVERIFY(writeFile(path, QStringLiteral("payload").toUtf8()));

    QString digest;
    QVERIFY(computeSha256(path, &digest));
    QVERIFY(verifySha256(path, digest));
}

void TestUpdateHelper::verifySha256Mismatch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("payload.bin"));
    QVERIFY(writeFile(path, QStringLiteral("what-the-source-is").toUtf8()));

    QString err;
    QVERIFY(!verifySha256(path, QString(64, QLatin1Char('0')), &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::verifySha256InvalidExpected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("payload.bin"));
    QVERIFY(writeFile(path, "x"));

    QString err;
    QVERIFY(!verifySha256(path, QStringLiteral("not-a-hash"), &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateSourceMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    QVERIFY(!validateSource(dir.filePath(QStringLiteral("nope")), &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateSourceDirectory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dirPath = dir.filePath(QStringLiteral("adir"));
    QVERIFY(QDir().mkpath(dirPath));
    QString err;
    QVERIFY(!validateSource(dirPath, &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateSourceNotExecutable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plain.txt"));
    QVERIFY(writeFile(path, "not executable"));
    QString err;
    QVERIFY(!validateSource(path, &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateSourceOk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.bin"));
    QVERIFY(writeFile(path, "binary"));
    makeExecutable(path);
    QString err;
    QVERIFY(validateSource(path, &err));
    QVERIFY(err.isEmpty());
}

void TestUpdateHelper::validateDestinationIsDirectory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dirPath = dir.filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(dirPath));
    QString err;
    QVERIFY(!validateDestination(dirPath, &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateDestinationParentNotDirectory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("plainfile"));
    QVERIFY(writeFile(filePath, "x"));
    // 把"文件"当成父目录来用 -> 父路径不是目录。
    const QString dest = filePath + QStringLiteral("/child");
    QString err;
    QVERIFY(!validateDestination(dest, &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateDestinationMissingParent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    QVERIFY(!validateDestination(dir.filePath(QStringLiteral("no/such/app")), &err));
    QVERIFY(!err.isEmpty());
}

void TestUpdateHelper::validateDestinationOk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    QVERIFY(validateDestination(dir.filePath(QStringLiteral("app")), &err));
    QVERIFY(err.isEmpty());
}

void TestUpdateHelper::atomicReplacePreservesPermissions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString dest = dir.filePath(QStringLiteral("dsh-desktop"));
    const QString source = dir.filePath(QStringLiteral("dsh-desktop-new"));

    QVERIFY(writeFile(dest, "old binary"));
    makeExecutable(dest);
    QVERIFY(writeFile(source, "new binary"));
    makeExecutable(source);

    QString sourceDigest;
    QVERIFY(computeSha256(source, &sourceDigest));

    QString err;
    QVERIFY2(atomicReplace(source, dest, &err), qPrintable(err));
    QVERIFY(err.isEmpty());

    // 内容已替换。
    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("new binary"));
    f.close();

    // 原目标权限（可执行位）被保留。
    QVERIFY(QFileInfo(dest).isExecutable());

    // 源文件仍然存在，便于外部校验。
    QVERIFY(QFileInfo(source).exists());
}

void TestUpdateHelper::atomicReplaceFreshInstall() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString source = dir.filePath(QStringLiteral("new-app"));
    QVERIFY(writeFile(source, "brand new binary"));
    makeExecutable(source);

    const QString dest = dir.filePath(QStringLiteral("not-yet-installed"));
    QVERIFY(!QFileInfo(dest).exists());

    QString err;
    QVERIFY2(atomicReplace(source, dest, &err), qPrintable(err));
    QVERIFY(err.isEmpty());

    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("brand new binary"));
    f.close();

    // 目标不存在时沿用源文件权限（可执行）。
    QVERIFY(QFileInfo(dest).isExecutable());
}

void TestUpdateHelper::atomicReplaceInvalidDestination() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString source = dir.filePath(QStringLiteral("new-app"));
    QVERIFY(writeFile(source, "x"));
    makeExecutable(source);

    const QString destDir = dir.filePath(QStringLiteral("target-dir"));
    QVERIFY(QDir().mkpath(destDir));

    QString err;
    QVERIFY(!atomicReplace(source, destDir, &err));
    QVERIFY(!err.isEmpty());
}

QTEST_GUILESS_MAIN(TestUpdateHelper)
#include "test_update_helper.moc"
