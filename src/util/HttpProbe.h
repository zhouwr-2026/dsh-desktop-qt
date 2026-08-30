// SPDX-License-Identifier: MIT
// @author zhouwr
//
// HTTP 健康探测工具。
//
// 单一权威实现：用 ``curl`` 子进程对给定 URL 做 HEAD/GET 等价探测
//（HTTP HEAD 让 ``-w '%{http_code}'`` 报告状态码，``-o /dev/null`` 丢弃 body），
// 2xx/3xx/4xx 都视为"服务可达"，5xx 与连接失败视为不可达。
//
// 三个 Backend 实现（Systemd / Supervised / External）原本各自有一份
// 几乎相同的 ``httpProbe`` 私有实现，且 Systemd 版本多做了 ``exitStatus`` 硬化，
// 另两处缺失；本工具统一使用 Systemd 版本的安全语义，避免出现"一处改了另一处忘改"
// 的状态码阈值/超时漂移。
//
// 副作用：会派生一个 curl 子进程（约 1 MiB 内存），调用方应在非热路径使用。
//
// (变更理由: 结构审查 #1, httpProbe 三处重复)

#pragma once

#include <QString>

namespace dsh::util {

/// 对 ``url`` 做 HTTP 健康探测（基于 QNetworkAccessManager，无子进程）。
///   * 超时 2 秒；
///   * 退出状态非正常（网络错误）视为失败；
///   * 解析 HTTP 状态码，200 ≤ code < 500 视为可达；其它（含 5xx、连接失败）
///     视为不可达。
///
/// \param url 要探测的 URL（不含末尾斜杠与路径；本函数会追加 ``/``）。
/// \return 可达返回 ``true``；否则 ``false``。
bool httpProbe(const QString& url);

}  // namespace dsh::util
