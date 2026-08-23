// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 桌面端在 DownloadInterceptor::handle 拦截到 Session log 导出时通过
// page->runJavaScript 显式调用本模块返回的脚本，移除 DSH Web UI 渲染的
// "Session 导出已开始下载" 自定义 HTML 模态（含 backdrop + dialog 整组）。
// 与 LoopbackWebPage 的 javaScriptAlert/Confirm/Prompt 虚函数（只拦
// Chromium 原生 JS 对话框）不同——本模块处理的是自定义 DOM。脚本不在
// user-script 集合里注册，浏览器直开 DSH Web UI 不受影响。

#pragma once

class QString;

namespace dsh::web {

/// 返回一段纯函数 JS：扫描当前 DOM、命中"Session 导出已开始下载"等
/// 文案即找到 modal 容器并整组移除。多次执行幂等。
QString sessionExportToastRemovalScript();

}  // namespace dsh::web