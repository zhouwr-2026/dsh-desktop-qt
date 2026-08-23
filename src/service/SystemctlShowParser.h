// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 解析 ``systemctl show <unit>`` 的 key=value 输出为 ServiceInfo。
//
// 本模块保持纯净：只处理传入的文本，不启动进程、不调用 systemctl、不读取
// 磁盘；环境文件内容的读取与合并交由上层完成。

#pragma once

#include "ServiceInfo.h"

#include <QString>
#include <QStringList>

namespace dsh::service {

/// 解析 ``systemctl show`` 输出的 key=value 文本。
///
/// \param text      ``systemctl show`` 的原始换行分隔输出。
/// \param unitName  单元名（如 ``dsh-web.service``）。
/// \param scope     单元所属范围（系统级 / 用户级）。``systemctl show``
///                  输出本身不含此信息，需调用方提供。
ServiceInfo parseSystemctlShow(const QString& text,
                               const QString& unitName = {},
                               ServiceScope scope = ServiceScope::System);

/// 从 ``ExecStart`` 字段提取命令参数列表。systemd 转义形式形如：:
///
/// ``{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh web --port 3080 ; ... }``
///
/// 无法识别的输入直接按整行拆分为参数。
QStringList parseExecStartArgv(const QString& execStart);

/// 判断 ExecStart 是否调用官方 ``dsh web``（或等价的 ``dsh --profile web``）。
bool invokesOfficialDshWeb(const QString& execStart);

}  // namespace dsh::service
