// SPDX-License-Identifier: MIT
// @author zhouwr
#include "SuppressExportToast.h"

#include <QString>

namespace dsh::web {

namespace {

// 纯函数式 JS：找到 toast 所在的 modal 容器（最准确的是
// `role="presentation"`——它把 backdrop 和 dialog 包在一起；否则
// `role="dialog"`；否则就近挑子树不超过上限的祖先）然后整棵 hide()，
// 并扫子树里 role/aria-modal/aria-hidden 节点也清掉防漏。
//
// 不注册 user script，只被 C++ 侧在 DownloadInterceptor::handle 通过
// page->runJavaScript 显式调用。脚本自带 0 / 100 / 400 / 1200 ms 四次幂等
// 重试覆盖异步出现的时序，约 1.5 秒后无任何活动。
constexpr const char* kScriptSource = R"JS(
(function () {
  'use strict';
  var PATTERNS = [
    /Session\s*导出已开始下载/,
    /导出已开始下载/,
    /浏览器正在下载\s*Session/,
    /正在下载\s*Session/i,
    /Downloading\s+Session\s+ZIP/i,
  ];
  // 子树超过该元素数就放弃清理（防误删整页）。
  var MAX_DESCENDANTS = 500;

  function matches(text) {
    if (!text) return false;
    for (var i = 0; i < PATTERNS.length; i++) {
      if (PATTERNS[i].test(text)) return true;
    }
    return false;
  }
  function countDesc(el) {
    try { return el.querySelectorAll('*').length; } catch (e) { return 0; }
  }
  function isUnsafeTarget(el) {
    if (!el || el.nodeType !== 1) return true;
    return el === document.body || el === document.documentElement;
  }
  function hide(el) {
    if (isUnsafeTarget(el) || el.__dshHidden) return false;
    if (countDesc(el) > MAX_DESCENDANTS) return false;
    el.__dshHidden = true;
    try { el.remove(); } catch (e) {}
    return true;
  }

  // 选 toast 所在的"modal 容器根"。按优先级匹配：
  //   1. `role="presentation"` 祖先 —— 包裹 backdrop + dialog 的 Portal 根，
  //      清掉它就整组 modal 都消失（Antd / rc-dialog / Chakra / Element Plus
  //      都这么用）。
  //   2. `role="dialog"` / `role="alertdialog"` 祖先 —— 没有 presentation
  //      包装时的 dialog 直接父级。
  //   3. 兜底：向上爬到"子树不超过上限且不是 body 的最近祖先"。
  function findStackingContext(toastEl) {
    if (isUnsafeTarget(toastEl)) return null;
    if (typeof toastEl.closest !== 'function') return toastEl;
    var pres = toastEl.closest('[role="presentation"]');
    if (pres && !isUnsafeTarget(pres) && countDesc(pres) <= MAX_DESCENDANTS)
      return pres;
    var dlg = toastEl.closest('[role="dialog"], [role="alertdialog"]');
    if (dlg && !isUnsafeTarget(dlg)) return dlg;
    var cur = toastEl.parentElement;
    for (var d = 0; cur && cur !== document.body && d < 6; d++) {
      if (!isUnsafeTarget(cur) && countDesc(cur) <= MAX_DESCENDANTS) return cur;
      cur = cur.parentElement;
    }
    return toastEl;
  }

  // 摧毁 modal 根 + 子树里独立的 mask/dialog 节点（防 portal 把它们
  // 拆开挂在 modal 根之外）。
  function cleanStackingContext(ctx) {
    if (!ctx || isUnsafeTarget(ctx)) return false;
    if (countDesc(ctx) > MAX_DESCENDANTS) return false;
    var removed = hide(ctx);
    try {
      var all = ctx.querySelectorAll(
        '[role="dialog"], [role="alertdialog"], [role="presentation"],' +
        ' [aria-modal="true"], [aria-hidden="true"]'
      );
      for (var i = 0; i < all.length; i++) {
        if (hide(all[i])) removed = true;
      }
    } catch (e) {}
    return removed;
  }

  function processRoot(root) {
    var textNodes = [];
    var walker;
    try {
      walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null, false);
    } catch (e) { return false; }
    var n;
    while ((n = walker.nextNode())) {
      if (matches(n.nodeValue || '')) textNodes.push(n);
    }
    var removed = false;
    for (var i = 0; i < textNodes.length; i++) {
      var el = textNodes[i].parentElement;
      if (!el) continue;
      var ctx = findStackingContext(el);
      if (!ctx) continue;
      // hide() 自身有 __dshHidden 守卫，多次调用安全；不再需要 seenCtx 去重。
      if (cleanStackingContext(ctx)) removed = true;
    }
    return removed;
  }

  function run() {
    if (!document.body) return false;
    var removed = processRoot(document.body);
    // shadow root：少数 toast 会挂在 shadow DOM 里。
    try {
      var all = document.querySelectorAll('*');
      for (var i = 0; i < all.length; i++) {
        if (all[i].shadowRoot && processRoot(all[i].shadowRoot)) removed = true;
      }
    } catch (e) {}
    return removed;
  }

  run();
  setTimeout(run, 100);
  setTimeout(run, 400);
  setTimeout(run, 1200);
})();
)JS";

}  // namespace

QString sessionExportToastRemovalScript() {
  return QString::fromUtf8(kScriptSource);
}

}  // namespace dsh::web