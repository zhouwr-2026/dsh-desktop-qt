// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop — 官方 ``dsh web`` 后台服务的 systemd 单元文本构建器。
//
// 本模块是纯函数式的"服务供给 (provisioning)"切片：它只把一组经过
// 校验的原始输入（dsh 可执行文件绝对路径、运行用户、工作目录、
// DSH_HOME、host、port 与 ServiceScope）拼装成一段可被 systemd 解析的
// 单元文本。它绝不写文件、绝不调用 systemctl / 任何外部进程、也绝不做
// shell 拼接；所有值都先经过严格的路径/端口校验与 systemd 安全转义/引号
// 处理，再写入输出。
//
// 默认采用用户级 (user-scope) 语义：省略 User=，unit 由拥有它的普通用户
// 运行；当 scope 为 System 时显式输出 User=<user>，供共享给本机其它用户
// 的系统级 unit 使用。

#pragma once

#include "ServiceInfo.h"

#include <QString>

namespace dsh::service {

/// 构建 ``dsh-web.service`` 所需的、已经过语义约束的输入。纯数据。
struct ServiceUnitSpec {
    QString dshExecutable;      // dsh 可执行文件绝对路径（如 /usr/bin/dsh）
    QString user;               // 运行用户；User= 只在 System scope 下输出
    QString workingDirectory;   // 工作目录/家目录；必须为绝对路径
    QString dshHome;            // DSH_HOME；为空时省略 Environment= 行
    QString host;               // --host 值
    int port{-1};               // --port 值；必须为 1..65535
    ServiceScope scope{ServiceScope::User};  // 默认用户级语义
};

/// 构建结果：成功时携带完整单元文本，失败时携带原因。纯数据。
struct ServiceUnitResult {
    bool ok{false};
    QString unitText;           // 校验通过时有效的 systemd 单元文本
    QString error;              // 校验失败时的原因
};

/// 纯函数式服务单元构建器：无磁盘 IO、无进程、无 shell 插值。
class ServiceUnitBuilder {
public:
    /// 校验输入并生成完整的、可被 systemd 解析的单元文本。
    ///
    /// 任一路径为空/非绝对、端口非法、或任何输入含换行/NUL 时返回
    /// ``ok=false`` 并给出原因；成功时 ``ok=true`` 并填充 \p unitText。
    /// 不产生任何副作用（不写文件、不执行 systemctl、不加 shell 前缀）。
    static ServiceUnitResult build(const ServiceUnitSpec& spec);

    /// 官方 ``dsh web`` 服务在用户级与系统级共用的 unit 文件名。
    static QString unitName();

    /// 按 scope 返回 unit 文件名（当前用户级与系统级均为 ``dsh-web.service``）。
    static QString unitNameForScope(ServiceScope scope);

    /// 把一段值安全地放入 systemd ``Environment=`` 赋值（双引号内部）：
    /// 转义 ``\\``、``"`` 与 ``%``（转成 ``%%``，避免 specifier 展开）。
    /// ``$`` 在 Environment= 中不作变量展开、保持字面，因此不转义。
    /// 本方法只做内容转义，不负责包裹引号。
    static QString escapeEnvironmentValue(const QString& value);

    /// 把一段参数安全地放入 systemd ``ExecStart=`` 命令行（双引号内部）：
    /// 转义 ``\\``、``"``、``%``（``%%``）与 ``$``（``$$``，避免 ExecStart
    /// 的 ``$VAR``/``${VAR}`` 展开）。本方法只做内容转义，不负责包裹引号。
    static QString escapeExecArgument(const QString& argument);
};

}  // namespace dsh::service
