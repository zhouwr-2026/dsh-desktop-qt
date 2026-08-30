// SPDX-License-Identifier: MIT
// @author zhouwr
#include "HttpProbe.h"

#include "SyncHttp.h"

#include <QUrl>

#include <cassert>

namespace dsh::util {

bool httpProbe(const QString& url) {
    // 不变量：调用方应保证 URL 非空；空 URL 会导致 syncHttpGet 发起无效请求。
    assert(!url.isEmpty());
    // 基于 QNetworkAccessManager 的同步探测（无子进程开销，每次节省 ~1 MiB）。
    // 语义：2xx/3xx/4xx = 可达，5xx/网络失败 = 不可达，超时 2s。
    const SyncHttpResult result = syncHttpGet(QUrl(url + "/"), /*timeoutSeconds=*/2);
    const int code = result.httpStatus;
    return result.ok && code >= 200 && code < 500;
}

}  // namespace dsh::util
