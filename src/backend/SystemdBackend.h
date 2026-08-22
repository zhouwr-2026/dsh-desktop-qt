// SPDX-License-Identifier: MIT
// @author zhouwr
// Systemd 后端实现。本类不直接持有进程，所有启动/停止都委托给
// ``systemctl``，保留 ``dsh-web.service`` 原生的失败自动重启、journal 日
// 志等行为。

#pragma once

#include "Backend.h"

#include <QString>

namespace dsh::backend {

class SystemdBackend : public Backend {
    Q_OBJECT
public:
    /// \param  unitName  systemd unit 名（默认 ``dsh-web.service``）。
    /// \param  url      期望监听的 HTTP URL。
    explicit SystemdBackend(const QString& unitName = QStringLiteral("dsh-web.service"),
                            const QString& url = QStringLiteral("http://127.0.0.1:3080"),
                            QObject* parent = nullptr);
    ~SystemdBackend() override = default;

    Mode mode() const override { return Mode::Systemd; }
    QString url() const override { return url_; }
    bool isRunning() const override;
    Status status() override;
    bool start() override;
    bool stop(bool force = false) override;
    bool restart() override;

    /// 在 ``/etc/systemd/system``、``/usr/lib/systemd/system`` 与用户目录
    /// ``~/.config/systemd/user/`` 三处查找 dsh-web.service，返回已定位的
    /// unit 名（找不到时为空字符串）。
    static QString detectUnit();

private:
    bool systemctl(const QString& verb, bool escalateIfNeeded = true);

    QString unitName_;
    QString url_;
    bool unitIsSystem_{true};
};

}  // namespace dsh::backend