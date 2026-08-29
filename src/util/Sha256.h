// SPDX-License-Identifier: MIT
// @author zhouwr
//
// SHA-256 文件哈希工具。
//
// 单一权威实现（流式、1 MiB chunk），所有需要校验文件完整性的模块
// （DesktopReleaseDownloader / DesktopUpdateHelper / 未来其它模块）
// 都应复用本工具，避免散落各处出现流式/分块策略不一致。
//
// 纯 Qt6::Core 依赖，可被 `dsh_desktop_core` 与 `dsh_desktop_updater_helper`
// 两个静态库各自编译链接一份，互不耦合。
//
// (变更理由: 结构审查 #2, computeFileSha256 / computeSha256 重复)

#pragma once

#include <QString>

namespace dsh::util {

/// 流式计算文件 SHA-256（小写十六进制、64 字符）。
///
/// \param path    要计算的文件路径
/// \param outHex  成功时填充 64 字符小写十六进制摘要；可为 nullptr
/// \param error   失败时填充人类可读错误信息；可为 nullptr
/// \return true 表示成功；false 表示路径不存在/无法打开/读取失败
///
/// 行为约束：
///   * 文件以 ``QIODevice::ReadOnly`` 打开（不要求可执行）；
///   * 1 MiB 分块，避免大文件一次性读入内存；
///   * 读到 0 字节且未到 EOF 时视为 IO 错误并返回 false（防止 EOF/错误歧义）。
bool computeFileSha256(const QString& path, QString* outHex, QString* error = nullptr);

}  // namespace dsh::util
