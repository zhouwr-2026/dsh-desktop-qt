// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 自更新助手核心 —— 纯函数式、可单元测试的校验与替换逻辑。
//
// 本模块只提供"文件交换"这一层的可验证内核，刻意不耦合网络下载、UI、
// 进程管理或 systemd：调用方（CLI ``dsh-desktop-updater`` 或未来的图形界面）
// 负责把 ``--pid`` / ``--source`` / ``--destination`` / ``--sha256`` 准备好，
// 再把它们交给这里的纯逻辑去校验与落地。
//
// 边界原则：
//   * 任何校验失败都通过返回 ``false`` 与 ``error`` 出错原因，绝不静默继续；
//   * 所有函数都不调用 shell、systemctl 或任何外部进程；
//   * 校验函数不修改文件系统；只有 ``atomicReplace`` 会实际落地文件。

#pragma once

#include <QString>

namespace dsh::updater {

/// 计算文件 SHA-256 并以小写十六进制串（64 个字符）返回。
/// \return 成功返回 ``true`` 并把结果写入 ``outHex``；
///         文件不存在/不可读时返回 ``false`` 并写入 ``error``。
bool computeSha256(const QString& path, QString* outHex, QString* error = nullptr);

/// 校验字符串是否为规范的 SHA-256 十六进制串（恰好 64 个十六进制字符）。
bool isValidSha256Hex(const QString& hex);

/// 校验文件的 SHA-256 是否与期望值一致（忽略大小写）。
/// \return 一致返回 ``true``；文件不可读或摘要不匹配返回 ``false`` 并写入 ``error``。
bool verifySha256(const QString& path, const QString& expectedHex, QString* error = nullptr);

/// 校验源文件：路径非空、存在、为常规文件（``QFileInfo::isFile``）且可执行。
bool validateSource(const QString& source, QString* error = nullptr);

/// 校验目标路径：非目录、父目录存在且为目录并可写。
/// 仅做前置校验，不改动文件系统；配合 ``atomicReplace`` 使用。
bool validateDestination(const QString& destination, QString* error = nullptr);

/// 用 ``source`` 原子性地替换 ``destination``，保留 ``destination`` 原有权限
/// （目标不存在时保留 ``source`` 的权限）。
///
/// 实行"同目录临时文件 + 备份 + rename"策略：
///   #. 在目标所在目录边写入一个临时文件（与目标同文件系统，保证 rename 原子）；
///   #. 若目标已存在，先把目标 rename 到同目录备份文件；
///   #. 把临时文件 rename 到目标路径；
///   #. 第二步失败则回滚：把备份文件 rename 回目标路径，并在出错信息中说明；
///   #. 成功后丢弃备份文件。
///
/// 成功后临时文件与备份文件均被清理。失败时尽力把现场恢复原状。
/// \return 成功返回 ``true``；失败返回 ``false`` 并写入 ``error``。
bool atomicReplace(const QString& source, const QString& destination, QString* error = nullptr);

}  // namespace dsh::updater
