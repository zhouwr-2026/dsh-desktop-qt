// SPDX-License-Identifier: MIT
// @author zhouwr
//
// systemd 命令构造工具集（纯函数，无状态）。
//
// 把 DshServiceManager 中"构造 systemctl / journalctl argv"和"校验 unit 名"
// 等纯函数抽到独立类。每个函数都可以独立单元测试，无运行时依赖。
//
// 拆分的理由：
//   * DshServiceManager.cpp 871 行（拆前），主体应聚焦"异步状态机 + Qt 事件循环"。
//   * systemctlArguments / journalctlArguments / isValidUnitName 等是纯字符串
//     处理，与 DshServiceManager 的状态完全无关，留在大类里增加阅读负担。
//   * 测试已经覆盖所有这些函数（test_service_manager.cpp ~30 处调用），
//     拆分时改命名空间机械且可验证。
//
// (变更理由: 结构审查 #5 上帝类拆分 —— SystemctlCommandBuilder + JournalTailReader + ManageabilityPolicy 中的第一块)

#pragma once

#include "ServiceInfo.h"
#include "ServiceOperation.h"

#include <QString>
#include <QStringList>

namespace dsh::service {

/// 一条已解析的完整命令：program + 显式参数列表。
///
/// 从 DshServiceManager.h 迁移过来（与 resolveCommand 同处），
/// 由 SystemctlCommandBuilder::resolveCommand 返回。
struct ResolvedCommand {
    QString program;
    QStringList arguments;
};

class SystemctlCommandBuilder {
public:
    SystemctlCommandBuilder() = delete;

    /// 构造 ``systemctl <verb> [<unit>]`` 的 argv 列表（不含 program）。
    static QStringList systemctlArguments(ServiceOperation operation,
                                         ServiceScope scope,
                                         const QString& unitName);

    /// 构造 ``journalctl -u <unit> [-n N] [-f]`` 的 argv 列表（不含 program）。
    static QStringList journalctlArguments(ServiceScope scope,
                                          const QString& unitName,
                                          int lines,
                                          bool follow);

    /// 判定给定 (operation, scope, euid) 是否需要 pkexec 提权。
    /// System scope + 非只读 + 当前非 root 时返回 true。
    static bool operationNeedsElevation(ServiceOperation operation,
                                          ServiceScope scope,
                                          qint64 euid);

    /// 构造完整 ResolvedCommand（program + argv 列表），按需插入 pkexec 包裹。
    static ResolvedCommand resolveCommand(ServiceOperation operation,
                                          ServiceScope scope,
                                          const QString& unitName,
                                          const QString& systemctlExe,
                                          const QString& pkexecExe,
                                          qint64 euid);

    /// 校验 unit 名合法性（白名单字符 + 已知 systemd 后缀）。
    /// 错误信息（可选）写入 ``*error``。
    static bool isValidUnitName(const QString& unitName, QString* error = nullptr);
};

}  // namespace dsh::service
