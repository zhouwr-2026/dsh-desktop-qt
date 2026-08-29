// SPDX-License-Identifier: MIT
// @author zhouwr
#include "LogViewer.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace dsh::app {

namespace {
constexpr qint64 kMaxBytes = 512 * 1024;  // 最多展示末尾 512 KB
}

LogViewer::LogViewer(const QString& log_path, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("日志查看"));
    setMinimumSize(720, 480);

    auto* layout = new QVBoxLayout(this);

    QString resolved = log_path;
    if (resolved.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(
                                QStandardPaths::AppDataLocation);
        resolved = dir + "/dsh-desktop.log";
    }

    auto* header = new QLabel(tr("<b>日志文件：</b>%1"
                                  "&nbsp;&nbsp;<span style='color:gray;'>%2</span>")
                                  .arg(QFileInfo(resolved).absoluteFilePath(),
                                       QFileInfo::exists(resolved)
                                           ? tr("%1 KB").arg(QFileInfo(resolved).size() / 1024)
                                           : tr("尚未创建")));
    header->setTextFormat(Qt::RichText);
    header->setWordWrap(true);
    layout->addWidget(header);

    auto* text = new QTextEdit();
    text->setReadOnly(true);
    text->setLineWrapMode(QTextEdit::NoWrap);
    text->setFontFamily(QStringLiteral("Monospace"));
    text->setFontPointSize(9);

    QFile f(resolved);
    // 先做 isReadable 预校验：避免在路径存在但无读权限(例如其他用户/进程的
    // /proc/<pid>/environ、/etc/shadow 等)时静默走 fallback，给出明确拒绝提示。
    // (变更理由: 安全审查 L-2)
    const QFileInfo info(resolved);
    if (info.exists() && !info.isReadable()) {
        text->setPlainText(tr("(日志文件不可读：%1)").arg(info.absoluteFilePath()));
    } else if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        text->setPlainText(tr("(尚未生成日志 — 用 --log-file 选项启动后会写到此处)"));
    } else {
        const qint64 size = f.size();
        if (size > kMaxBytes) {
            f.seek(size - kMaxBytes);
            text->setPlainText(
                tr("(日志过大，仅显示末尾 %1 KB)\n\n").arg(kMaxBytes / 1024) +
                QString::fromLocal8Bit(f.readAll()));
        } else {
            text->setPlainText(QString::fromLocal8Bit(f.readAll()));
        }
    }
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox();
    auto* refreshBtn = buttons->addButton(tr("刷新"), QDialogButtonBox::ActionRole);
    auto* openDirBtn = buttons->addButton(tr("打开所在目录"), QDialogButtonBox::ActionRole);
    buttons->addButton(tr("关闭"), QDialogButtonBox::RejectRole);
    connect(refreshBtn, &QPushButton::clicked, this, [this, resolved, text]() {
        // 简单刷新：重新打开文件并替换内容
        QFile refreshedFile(resolved);
        if (!refreshedFile.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        const qint64 size = refreshedFile.size();
        if (size > kMaxBytes) {
            refreshedFile.seek(size - kMaxBytes);
            text->setPlainText(
                tr("(日志过大，仅显示末尾 %1 KB)\n\n").arg(kMaxBytes / 1024) +
                QString::fromLocal8Bit(refreshedFile.readAll()));
        } else {
            text->setPlainText(QString::fromLocal8Bit(refreshedFile.readAll()));
        }
        text->moveCursor(QTextCursor::End);
    });
    connect(openDirBtn, &QPushButton::clicked, this, [resolved]() {
        QFileInfo info(resolved);
        const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        QProcess::startDetached(QStringLiteral("xdg-open"), {dir});
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

}  // namespace dsh::app
