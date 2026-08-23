// SPDX-License-Identifier: MIT
// @author zhouwr
// Systemd 后端实现。本类不直接持有进程，所有启动/停止都委托给
// ``systemctl``，保留 ``dsh-web.service`` 原生的失败自动重启、journal 日
// 志等行为。

#pragma once

#include "Backend.h"
#include "service/ServiceDiscovery.h"

#include <QString>

namespace dsh::backend {

class SystemdBackend : public Backend {
    Q_OBJECT
public:
    /// 用发现层选中服务的简明结果构造；scope 与 host/port 直接取自选中候选，
    /// 不再从文件系统重新推断。
    ///
    /// \param detected 选中候选（应由 ``detect()`` 提供；``valid`` 为真）。
    /// \param url      期望监听的实际 HTTP URL；为空时由 ``detected`` 的
    ///                 host/port 组装。
    explicit SystemdBackend(const dsh::service::DetectedService& detected,
                            const QString& url = QString(),
                            QObject* parent = nullptr);
    ~SystemdBackend() override = default;

    Mode mode() const override { return Mode::Systemd; }
    QString url() const override { return url_; }
    bool isRunning() const override;
    Status status() override;
    bool start() override;
    bool stop(bool force = false) override;
    bool restart() override;

    /// 执行只读发现并返回选中的服务识别结果（unit 名 / scope / host / port）。
    ///
    /// 仅当 ``systemctl show`` 验证 ``LoadState=loaded`` 且 ExecStart 调用官方
    /// ``dsh web`` 时才返回 ``valid=true``；否则返回 ``valid=false``，让上层
    /// 退化为子进程兜底。结果取自发现层的选中项，因此遵循其文档化偏好
    /// （两者都有效时优先当前用户的用户级服务）。
    static dsh::service::DetectedService detect();

private:
    bool systemctl(const QString& verb, bool escalateIfNeeded = true);

    QString unitName_;
    QString url_;
    bool unitIsSystem_{true};
};

}  // namespace dsh::backend