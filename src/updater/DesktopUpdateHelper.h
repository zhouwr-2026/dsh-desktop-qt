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

/// 判定 ``path`` 的规范化绝对路径是否位于 ``prefix`` 之内（含前缀自身）。
/// 两条路径都会被绝对化并归一化 ``..``；``path`` 存在时还会解析符号链接。
/// \return 位于前缀内返回 ``true``；``path`` 或 ``prefix`` 为空、或 ``path``
///         不在前缀内时返回 ``false`` 并（可选）写入 ``error``。
bool isPathWithinPrefix(const QString& path, const QString& prefix,
                        QString* error = nullptr);

/// 校验自更新目标在安全约束下可被替换：
///   * 目标本身必须合法（见 ``validateDestination``）；
///   * 目标（规范化后）必须等于 ``installedBinary``，或位于 ``trustedPrefix`` 之内；
///   * 目标若已存在，其文件主必须与当前有效用户一致（尽力而为，防止覆写
///     他人拥有的文件）。
///
/// 这是对 ``--destination`` 的访问控制：要么它就是要更新的那个已安装二进制，
/// 要么落在可信安装根内，从而阻止自更新助手被滥用来覆写任意可写文件。
///
/// \param installedBinary 已安装桌面二进制的已知绝对路径；空串表示不特指。
/// \param trustedPrefix   可信安装前缀（绝对路径）；空串表示不做前缀放行。
/// \return 全部通过返回 ``true``；否则返回 ``false`` 并写入 ``error``。
bool validateInstallDestination(const QString& destination,
                                const QString& installedBinary,
                                const QString& trustedPrefix,
                                QString* error = nullptr);

/// 崩溃恢复：清理 ``directory`` 下上次自更新崩溃/被杀留下的孤儿
/// ``.dsh-update-*.bak-*`` 与 ``.dsh-update-*.tmp-*`` 文件。
///   * ``.bak`` 是被移走的原文件：若原目标缺失则恢复备份到原名，否则删除过时备份；
///   * ``.tmp`` 是未完成的临时写入，一律删除。
///
/// 在启动更新前调用，可让"备份已移走但新文件未就位"的崩溃现场回到一致状态。
/// \return 全部处理成功返回 ``true``；出现无法删除/恢复的条目返回 ``false``
///         并写入 ``error``。
bool recoverOrphanedDshUpdateFiles(const QString& directory, QString* error = nullptr);

/// 用 ``source`` 原子性地替换 ``destination``，保留 ``destination`` 原有权限
/// （目标不存在时保留 ``source`` 的权限）。
///
/// 实行"同目录临时文件 + 备份 + rename"策略：
///   #. 在目标所在目录边写入一个临时文件（与目标同文件系统，保证 rename 原子）；
///   #. 若目标已存在，先把目标 rename 到同目录备份文件；
///   #. 把临时文件 rename 到目标路径；
///   #. 第二步失败则回滚：把备份文件 rename 回目标路径；若回滚也失败，
///      则说明目标可能缺失，出错信息会同时给出原文件备份所在的路径；
///   #. 成功后丢弃备份文件；若备份清理失败，替换结果仍是成功的，但会在
///      ``error`` 中附上备份路径的诊断（成功且无异常时 ``error`` 保持为空）。
///
/// 成功后临时文件与备份文件均被清理。失败时尽力把现场恢复原状。
/// \return 替换成功返回 ``true``；替换失败返回 ``false`` 并写入 ``error``。
bool atomicReplace(const QString& source, const QString& destination, QString* error = nullptr);

}  // namespace dsh::updater
