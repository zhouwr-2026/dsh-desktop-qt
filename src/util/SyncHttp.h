// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 同步 HTTP GET 工具（一次性 NAM + QEventLoop + 超时 abort）。
//
// 单一权威实现：``Updater::fetchLatestVersion`` 与
// ``DesktopVersionChecker::fetchLatestRelease`` 原本各自重复了同一套
// QNetworkAccessManager + QEventLoop + QTimer::singleShot 同步模式
// （含相同的 Accept / User-Agent），本工具消除该重复并统一：
//   * 状态码语义：有 HTTP 状态码时保留作为 ``httpStatus``；网络失败
//     （超时/连接拒绝/Qt 网络错误）时 ``httpStatus = 0``、``ok = false``；
//   * User-Agent 默认值为 ``dsh-desktop/<DSH_DESKTOP_VERSION> (Qt6)``；
//   * 请求头统一追加 ``Accept: application/json``（与原版一致）。
//
// 调用方按 ``httpStatus`` 自行决定如何语义化（如 parseHttpResponse）；
// ``ok`` 仅表示"网络层拿到响应且无 Qt 网络错误"，并不保证 2xx。
//
// 阻塞式：在调用线程内同步等待（最长 timeoutSeconds 秒）。不应在 UI
// 主线程的快路径调用。
//
// (变更理由: 结构审查 #3, 同步 HTTP 模式两处重复)

#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace dsh::util {

/// 同步 HTTP GET 响应结果。
struct SyncHttpResult {
    /// true 表示网络层拿到响应（reply 无 Qt 网络错误且可读 body）。
    /// 注意 HTTP 4xx/5xx **也** 算 ok=true，只是 ``httpStatus`` 显示非 2xx，
    /// 由调用方按业务语义判定。false 表示网络层失败（超时/连接拒绝等）。
    bool ok{false};
    /// 原始 HTTP 状态码（200/404/500 等）；网络层失败或无状态码时为 0。
    int httpStatus{0};
    /// 响应 body；网络层失败时为空。
    QByteArray body;
    /// 人类可读错误字符串；成功时为空。
    QString errorString;
};

/// 对 ``url`` 做一次同步 HTTP GET 请求（最多 ``timeoutSeconds`` 秒）。
///
/// 请求头默认追加：
///   * ``Accept: application/json``
///   * ``User-Agent: dsh-desktop/<DSH_DESKTOP_VERSION> (Qt6)``（除非显式传入覆盖）
///
/// \param url            完整 URL。
/// \param timeoutSeconds 超时（秒）；< 1 时视作 1 秒。
/// \param userAgent      User-Agent 覆盖；为空时使用默认值。
SyncHttpResult syncHttpGet(const QUrl& url, int timeoutSeconds,
                           const QByteArray& userAgent = QByteArray());

}  // namespace dsh::util
