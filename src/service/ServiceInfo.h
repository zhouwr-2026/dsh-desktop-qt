// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 服务发现数据模型：纯数据类型，不含任何进程、systemctl 或磁盘 IO。
//
// 这些类型描述 ``systemctl show`` 观测到的 service 单元，供上层发现层
// （DshServiceManager 一类）组装和使用。解析逻辑见 SystemctlShowParser。

#pragma once

#include <QString>
#include <QStringList>

namespace dsh::service {

/// 官方 ``dsh web`` 服务的默认监听地址与端口。
inline constexpr const char* kDefaultHost = "127.0.0.1";
inline constexpr int kDefaultPort = 3080;

/// service 运行范围：系统级 unit 或当前用户级 unit。
enum class ServiceScope {
    System,
    User,
};

/// service 来源：决定提示文案、卸载与权限策略。
///
/// 由上层发现层根据桌面端自有状态文件与 unit 位置判定；从 ``systemctl
/// show`` 输出本身无法区分 ``ExistingOfficial`` 与 ``ProvisionedByDesktop``，
/// 因此解析时保持默认值。
enum class ServiceOrigin {
    ExistingOfficial,      // 官网/用户已有的官方 DSH service
    ProvisionedByDesktop,  // 由 DSH Desktop 自己补齐的 unit
    SupervisedFallback,    // systemd 不可用时的子进程兜底
    External,              // 远程后端，不管理本机生命周期
};

/// 生命周期状态。Active/Inactive/Failed/Activating 由 ActiveState 推导，
/// Unknown 为兜底；Unmanaged 由上层在发现该 service 不属于当前用户时标记，
/// 只读展示、不做本机生命周期管理。
enum class LifecycleState {
    Active,
    Inactive,
    Failed,
    Activating,
    Unknown,
    Unmanaged,
};

/// 一个已发现的 dsh-web.service 单元的只读快照。
struct ServiceInfo {
    QString unitName;
    ServiceScope scope{ServiceScope::System};
    ServiceOrigin origin{ServiceOrigin::ExistingOfficial};
    LifecycleState state{LifecycleState::Unknown};

    // 原始 ``systemctl show`` 字段，未经深入处理，便于上层直接展示。
    QString loadState;
    QString activeState;
    QString subState;
    QString execStart;
    QString user;
    QString workingDirectory;
    QStringList environment;       // 每条为未加引号的 KEY=VALUE
    QStringList environmentFiles;  // 每个为环境文件路径
    qint64 mainPid{-1};            // MainPID，缺失或非正数时为 -1

    // 派生结果。
    bool invokesOfficialDshWeb{false};

    // DSH_HOME：能从 Environment 解析到时记录绝对路径并置 ``dshHomeSet``，
    // 否则保持空字符串，由调用方各自应用默认值（安全默认标记）。
    QString dshHome;
    bool dshHomeSet{false};

    // 监听地址/端口。解析不到时回退到官方默认值并把对应 ``*IsDefault``
    // 标记为 true，供上层决定是否提示用户（fallback-to-default 标记）。
    QString host{kDefaultHost};
    bool hostIsDefault{true};
    int port{kDefaultPort};
    bool portIsDefault{true};
};

}  // namespace dsh::service
