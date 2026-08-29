// SPDX-License-Identifier: MIT
// @author zhouwr
//
// dsh systemd 服务管理操作类型。
//
// 独立于 DshServiceManager，供纯命令构造层和异步管理器共同使用，
// 避免命令构造工具反向 include 管理器头。

#pragma once

namespace dsh::service {

/// 管理器可执行的异步操作类型。
enum class ServiceOperation {
    Start,
    Stop,
    Restart,
    Status,
    Discovery,
    JournalTail,
    DaemonReload,
    Enable,
};

}  // namespace dsh::service
