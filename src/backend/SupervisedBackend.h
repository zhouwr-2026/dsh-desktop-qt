// SPDX-License-Identifier: MIT
// @author zhouwr
// 兜底后端：在找不到 systemd unit 时直接拉起 ``dsh web`` 子进程，
// 由本类负责管理子进程直到 stop()/quit。保证用户即使没装 service 文件
// 也能用托盘体验 DSH Desktop。

#pragma once

#include "Backend.h"

#include <QProcess>
#include <QString>

namespace dsh::backend {

class SupervisedBackend : public Backend {
    Q_OBJECT
public:
    /// \param dshBin  dsh 可执行文件路径（空字符串则按 PATH 自动查找）。
    /// \param url     期望监听的 URL。
    explicit SupervisedBackend(
        const QString& dshBin = QString(),
        const QString& url = QStringLiteral("http://127.0.0.1:3080"),
        QObject* parent = nullptr);
    ~SupervisedBackend() override;

    Mode mode() const override { return Mode::Supervised; }
    QString url() const override { return url_; }
    bool isRunning() const override;
    Status status() override;
    bool start() override;
    bool stop(bool force = false) override;
    bool restart() override;

private:
    static QString resolveDshBin();

    QString dshBin_;
    QString url_;
    QProcess* proc_{nullptr};
};

}  // namespace dsh::backend