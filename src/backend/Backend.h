// SPDX-License-Identifier: MIT
// @author zhouwr
// dsh web 后端主管。
//
// 三种实现：
//   * ``SystemdBackend``（默认）：对接 ``/etc/systemd/system/dsh-web.service``
//     （或 ``~/.config/systemd/user/dsh-web.service``）。
//   * ``SupervisedBackend``：在无 systemd unit 时直接拉起 ``dsh web`` 子进程。
//   * 外部模式：连接远程 dsh-web，不管理本机服务生命周期。
//
// 抽象类 ``Backend`` 是上层 UI 唯一的对外接口。

#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace dsh::backend {

enum class Mode {
    Systemd,     // 由 systemd 托管
    Supervised,  // 由桌面端拉起的子进程
    External,    // 远程后端，不由桌面端管理
};

struct Status {
    bool running{false};
    Mode mode{Mode::Systemd};
    QString url;          // 例如 http://127.0.0.1:3080
    QString detail;       // 人类可读的描述（systemd state / 子进程 pid 等）
    int activeTasks{0};   // 后端探测到的活跃任务数（粗略）
};

class Backend : public QObject {
    Q_OBJECT
public:
    /// 默认 URL：官方 DSH web 默认绑定的 loopback 地址 + 端口。
    static QString defaultUrl();

    /// 根据当前系统环境选择最合适的后端实现：发现 systemd unit 优先用
    /// systemd，否则退化为子进程模式。
    static std::unique_ptr<Backend> createForHost(
        const QString& url = QString(), QObject* parent = nullptr);

    explicit Backend(QObject* parent = nullptr) : QObject(parent) {}
    ~Backend() override = default;

    virtual Mode mode() const = 0;
    virtual QString url() const = 0;

    /// \return true 表示 ``http://<url>/`` 在超时内返回 2xx/3xx/4xx。
    virtual bool isRunning() const = 0;

    virtual Status status() = 0;

    /// 启动后端，幂等。
    virtual bool start() = 0;

    /// 停止后端，幂等。``force=true`` 时使用更激进的关闭方式。
    virtual bool stop(bool force = false) = 0;

    /// ``stop + start``。
    virtual bool restart() = 0;

signals:
    void log(const QString& message);
};

}  // namespace dsh::backend

Q_DECLARE_METATYPE(dsh::backend::Status)
