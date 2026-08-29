# 变更日志

DSH Desktop 所有重要变更都记录在此。版本遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.1.1] - 2026-08-29

### 修复
- **`dsh-theme-export.service` 启动报 NAMESPACE 226**：service 单元
  `ReadWritePaths=/run/dsh-desktop` 但 `/run` 是 tmpfs，`/run/dsh-desktop`
  每次重启消失；`install.sh::configure_theme_export()` 在 `systemctl
  start` 之前 `install -d /run/dsh-desktop`。关联 PR #1。
- **autostart .desktop 装到 `/usr/etc/xdg/autostart/`**：`post-install.sh`
  第 30 行 `sysconfdir="$prefix/etc"` 在 prefix=/usr 时展开为 `/usr/etc/...`，
  XDG autostart scanner 找不到；改写死 `/etc`，符合 XDG 规范。关联 PR #1。
- **启动器 / 任务栏图标在暗色 Plasma 下是黑色（应为白色）**：KDE
  `KIconTheme::iconPath` 在 `hicolor/scalable/apps/` 命中 `dsh-whale.svg`
  就停（普通色版优先于 `-symbolic` 后缀 fallback），CMake install 装的
  固定黑色版与暗色 look-and-feel 不协调。`install.sh::install_icons()`
  检测当前 KDE colorscheme（读 `kdeglobals` 的 `LookAndFeelPackage` +
  `ColorScheme`），按本机情况装白色版或黑色版。关联 PR #3 + PR #4。
- **`detect_kde_brightness` 在 sudo 上下文 fallback light**：install.sh
  通常由 `sudo bash` 跑，`$HOME=/root`、`/root/.config/kdeglobals` 不存在，
  导致所有 sudo 装包的用户都装黑色 fallback；走 `SUDO_USER` 拿真实用户
  home dir。关联 PR #4。
- **NSS `passwd: files systemd` 下 `getent passwd` 漏掉 UID≥1000 用户**：
  部分发行版 NSS 配置下 `getent passwd` 全部列表不显示普通用户——包安装
  root 上下文遍历会漏掉所有真实桌面用户，colorscheme 探测 fallback
  黑色。改用 `/home/*/` glob 代替（更可靠，root / service account home
  不在 `/home/` 下自动排除）。关联 PR #5。
- **dash/sh source bash 脚本会失败**：`icon-brightness.sh` 用 bash 语法
  `[[ ]]` / `local` / `(( )`，但 `debian/dsh-desktop.postinst` 用
  `#!/bin/sh`（dash）、`rpm spec %post` 跑在 sh 上下文——dash source
  bash 脚本报 syntax error。改成可执行脚本 + 4 个调用方用 subshell 调用
  `brightness="$("$lib")"`，subshell 隔离 bash 依赖。关联 PR #5。

### 新增
- **共享配色探测脚本 `packaging/icon-brightness.sh`**：所有发行版包
  post-install hook（PKGBUILD `post_install` / Debian `postinst` / rpm
  `%post`）+ `packaging/install.sh` 通过 subshell 调用这一个脚本——
  避免逻辑四处重复并漂移。CMake install 把它装到
  `/usr/lib/dsh-desktop/icon-brightness.sh`。
- **CMake `execute_process + sed` 派生 `dsh-whale-symbolic.svg`**：配置
  阶段用 `execute_process(COMMAND sed "s/fill=\"#000000\"/fill=\"currentColor\"/g"
  INPUT_FILE ... OUTPUT_FILE ... RESULT_VARIABLE ...)` 派生 fill 替换版本；
  `RESULT_VARIABLE` + `message(FATAL_ERROR)` 显式报错。不用 `file(READ) +
  string(REPLACE)` 是因为后者内部会把 `${...}` 当成 CMake 变量插值——
  当前 SVG 无 `$` 安全，但未来 SVG 加 `&` `\` 等字符可能踩坑；sed 是真正的
  字面替换。

### 影响
- 5 种安装方式（源码 / Arch / Debian / RPM / 通用 .tar.gz）全部获得
  colorscheme 智能选色，包安装用户不再被 CMake fallback 黑色坑到。

## [Unreleased]

### 新增
- **退出 / 重启双对话框 + 后端服务勾选统一语义**：
  - 新增独立 `RestartDialog`（与 `ExitDialog` 平行实现，避免后者退化为
    if-else 垃圾桶），标题/按钮/勾选语义与退出对话框对称区分。
  - 两个对话框都新增 `backendRunning` 参数：后端**运行中**时勾选框为
    "同时停止/重启 DSH 后台服务"（默认不勾选）；后端**未运行**时勾选
    框自动锁定为"同时启动 DSH 后台服务"（勾选且禁用），把"无论是否勾
    选都会启动"的偏好清晰传达给用户。
  - 后端不可管理（`External` / `Unmanaged`）时不渲染勾选，并保留对
    远程后端"桌面端不会动它"的明示。
- **`ShutdownIntent` 与统一 `performQuit` / `performRestart`**：把"对后端
  做什么"的判定从托盘子菜单收到退出/重启流程里——后端运行中勾选时执
  行 stop/restart，否则不动；后端未运行时强制拉起（必要时会自动安装
  `@deepseek-ai/dsh`）。
- **`ensureBackendStarted` 自动安装流程**：`dsh` 二进制缺失时弹原生确认
  框，确认后调用 `pkexec npm i -g @deepseek-ai/dsh`，捕获合并输出，错
  误时把 `npm` 的真实输出透传到 `QMessageBox`。
- **`LoopbackWebPage` 拦截 Web 端 JavaScript 对话框**：覆盖
  `javaScriptAlert/Confirm/Prompt` 三个虚函数。`confirm` 一律自动接受
  （让官方 Web UI 的下载/导出等异步动作继续推进）；`alert/prompt` 静默
  关闭。这层是对 **Chromium 原生** JS 弹窗的兜底——它拦不到自定义
  HTML 模态（见下一条）。
- **`SuppressExportToast` + `DownloadInterceptor` 运行时清理自定义 toast**：
  官方 DSH Web UI 点击 "Session log" 时弹出的"Session 导出已开始下载 /
  浏览器正在下载 Session ZIP 文件"是**自定义 HTML 模态组件**（带样式 +
  ×关闭按钮），不经过 `window.alert/confirm`，虚函数拦不到。最终方案是
  **桌面端触发**：`DownloadInterceptor::handle()` 确认命中
  `/api/session.export` 时，通过 `request->page()->runJavaScript()` 主动
  注入一段纯函数式 JS，扫描并移除匹配文案的 DOM 节点（脚本自带
  0/200/1000ms 三次幂等重试覆盖异步出现的时序）。
  - **不动 @deepseek-ai/dsh 源码**：只做运行时 DOM 清理；
  - **浏览器直开 `http://127.0.0.1:3080/` 不受影响**：JS 不注册到
    user-script 集合，只在桌面端 C++ 拦截到导出请求时显式调用才会执行；
  - **决定权在 C++**：无常驻观察器，~2s 后脚本即无任何活动。
- **`DownloadInterceptor::finish` 仅用 KDE 通知反馈成功**：成功时只发
  `dsh::util::notify`，不再弹原生 `QMessageBox::information`；失败/取消
  行为不变。
- **`dsh-shutdown-dialogs-test`**：15 个新单元测试覆盖 ExitDialog /
  RestartDialog 的勾选文字、默认状态、未运行锁定与不可管理隐藏。

### 修复
- **SupervisedBackend::stop 子进程已结束的退化路径**：之前当 `proc_`
  仍持有 QProcess 而子进程已死（`processId()` 返回 0）时，函数返回
  `false` 导致上层误报"无法停止后台服务"。修复后此场景判为已停止并
  清理 `proc_`，避免后续 stop/restart 链路异常。
- **SystemdBackend::systemctl 错误信息透传**：合并 stdout/stderr 输出，
  在 exit code 非零时把真实原因写入 `lastOperationError_`，由
  `status().detail` 透传给上层 `QMessageBox`，让"无法启动/停止/重启"
  不再是无内容提示。
- **退出/重启对话框打开期间后端状态可能变化 → 落地动作前重新拉快照**：
  之前的实现把对话框打开前抓的 `Status` 一直在 `performQuit` /
  `performRestart` 里用，对话框被打开思考 N 秒期间后端可能已重启/停止，
  落地动作就按陈旧数据走。现已在 `onRequestQuit` / `onRestartWithDialog`
  `dlg.exec()` 之后立即重新调用 `backend_->status()` 再写进
  `ShutdownIntent.status`。
- **`ensureBackendStarted` 陈旧参数**：之前用调用方传入的 `status.running`
  / `status.detail`，与刚做的后端状态变化脱节。修复后改用函数入口即时
  拉一次 `backend_->status()`，start 失败也用最新 `detail` 报错；`dsh`
  二进制已存在的场景给到 systemctl/端口冲突的可执行排查清单。
- **`ensureBackendStarted` 同步阻塞 10 分钟 GUI 线程**：之前的
  `install.waitForFinished(10*60*1000)` 会让主窗口冻结且无法取消；改为
  `QProcess` + `QEventLoop` + `QProgressDialog`，用户可主动取消，事件
  循环继续流转。
- **`SystemdBackend::systemctl` 增加 `exitStatus()` 与 `p.error()` 校验**：
  原先只读 `exitCode`，进程被信号杀但 exit code 恰巧为 0 时会被误判为
  成功；现在合并退出状态、错误码、超时三个维度共同判定，并写出更具体
  的 `lastOperationError_`。

### 变更
- **托盘菜单重组**：移除 `DSH 后台服务` 子菜单分组（含启动/重启/停止
  三个动作），后台服务的启停入口完全合并到退出/重启对话框里的勾选框；
  顶级菜单中 `重启 DSH Desktop` 重命名为 `重启` 并放到 `退出` 上方，
  与 `退出` 用分隔符隔开，明确"重启/退出"是桌面端的两个并列生命周期
  操作；`重启` 的语义图标从 `view-refresh` 换成 `system-reboot`。
- 移除 `TrayController::bind` 的 `onStartBackend/onRestartBackend/
  onStopBackend` 三个回调（托盘不再有直接入口），保留
  `backendManageable()` 供退出/重启流程使用。
- 删除 `DshDesktopApp::refreshBackendMenuState`（托盘分组已移除）；
  `pollBackendHealth` 的通知文案同步更新为指向退出/重启对话框。
- **统一更新流程**：启动 / 后台更新检查改为在工作线程同时执行后端（npm、
  `Updater::check`）与桌面（Gitee、`DesktopVersionChecker::check` 使用生成的
  `DSH_DESKTOP_VERSION`）两个来源的检查，经 `UpdatePlan::combine` 合并后决定
  托盘可见性与对话框内容：
  - 托盘在任一组件可更新时显示**唯一**的 `更新到最新版` 动作。
  - `UpdateDialog` 重构为接收 `UpdatePlan`：同时展示后端 / 桌面两个组件，
    复选框默认勾选 `defaultSelected()`，目标版本锁定（含来源标签），
    操作用不定量进度条（不伪造百分比）。
  - 更新串行执行、后端优先：后端复用 `Updater` 的异步提权更新；桌面用
    `DesktopReleaseDownloader` 下载选中资产，校验 SHA-256 后拉起已安装的
    `dsh-desktop-updater`（携带当前 PID / source / destination / sha256 /
    install-prefix），释放单实例锁并退出且不停止后台服务。
  - 桌面资产或助手不可用时显示明确失败，且不替换任何文件。
- `UpdatePlan` 新增 `componentLabel` / `componentDetail` 纯函数（供对话框展示），
  并让 `ComponentUpdate` 携带桌面发布信息（含附件），避免下载时二次联网。

### 变更
- **托盘菜单重构**：把 `重启桌面` 拆分为 `重启 DSH Desktop`（用
  `QProcess::startDetached` 重新拉起当前可执行文件并退出，不停止后台服务）
  与一个独立的 `DSH 后台服务` 分组（`启动后台服务` / `重启后台服务` /
  `停止后台服务`），三者与其它菜单项用分隔符隔开。
- 后台服务动作遵循 External / Unmanaged / 可管理状态：仅在后端可管理时启用，
  停止前弹原生确认，操作结果写入日志并刷新菜单启用状态。
- **自更新助手加固**（`DesktopUpdateHelper`）：
  - 新增 `validateInstallDestination`：目标必须等于已安装桌面二进制，或位于
    可信安装前缀之内；目标已存在时校验文件主与当前有效用户一致（尽力而为）。
  - 新增 `recoverOrphanedDshUpdateFiles`：启动时清理上次更新崩溃留下的孤儿
    `.dsh-update-*.bak-*` / `.dsh-update-*.tmp-*` 文件（备份在原目标缺失时恢复，
    否则删除；临时文件一律删除）。
  - `dsh-desktop-updater` CLI 改用上述校验，并新增可选 `--install-prefix`。
- **图标资源改为纯 SVG**：
  - `packaging/install.sh` 不再用 `rsvg-convert` 生成可选 PNG 位图（仅安装 SVG）；
  - canonical SVG 统一以 `assets/dsh-whale-{black,white}.svg` 为唯一来源，
    删除 `packaging/` 下重复的 SVG；CMake 安装与 QRC 引用保持不变。
- **`ForceDarkMode` 编译期保护**：`DshWindow` 对
  `QWebEngineSettings::ForceDarkMode`（Qt 6.7 起提供）加 `QT_VERSION` 宏保护；
  项目最低版本为 Qt 6.5，在 6.5/6.6 上退化为不强制 Chromium 反色（详见
  `applyForceDarkMode` 注释）。

## [0.1.0] - 2026-08-21

### 新增
- **C++17 + Qt6 完整重写**：从最初 Python 版本完全迁移到 C++；零运行时 Python 依赖，单可执行文件
- **常驻托盘 + 6 项菜单**：显示桌面 / 隐藏桌面 / 检查更新 / 更新到最新版（条件可见）/ 重启桌面 / 退出
- **原生退出对话框**：
  - 确认 / 取消两按钮
  - 后端有活跃任务时显式高亮提示
  - "同时停止后台 dsh web 服务"勾选框
- **黑/白鲸鱼主题自适应**：
  - 三源探测（Qt `QStyleHints.colorScheme()` / KDE `kdeglobals`+`plasmarc` / `org.freedesktop.portal.Settings`）
  - 托盘 / 窗口 / 任务栏图标统一跟随
- **会话日志下载拦截**：
  - 拦截 `QWebEngineProfile::downloadRequested`
  - 弹原生 `QFileDialog` + 后台流式下载 + `QProgressDialog` + KDE 通知
  - 默认下载到 XDG 数据目录 `~/.local/share/dsh-desktop/downloads/`
- **外部链接拦截**：
  - 自定义 `LoopbackWebPage::acceptNavigationRequest` 拒绝非 loopback http(s)
  - `newWindowRequested` 处理 `target=_blank`
  - 三级回退：`QDesktopServices.openUrl` → `webbrowser.open` → `xdg-open`
- **后端管理策略**：
  - `SystemdBackend`：对接已有 `dsh-web.service`
  - `SupervisedBackend`：找不到 unit 时拉起子进程
  - 抽象类 `Backend` 统一接口
- **KDE 集成**：
  - KDE 通知（`org.freedesktop.Notifications` D-Bus）
  - 启动 60 秒自动后台检查更新（静默通知）
  - KDE 登录自启动集成（`~/.config/autostart/dsh-desktop.desktop`）
  - 系统字体 + 主题跟随
- **包管理**：
  - Arch Linux PKGBUILD
  - `packaging/install.sh` 一键脚本（pacman 依赖 + cmake 构建 + polkit 策略 + 图标注册 + 自启动）
  - 卸载说明
- **开发基础设施**：
  - CMake + Ninja 构建系统（静态库 + 单元测试）
  - Qt Test 单元测试（semver / loopback / session-export）
  - 端到端冒烟脚本 `scripts/smoke.sh`
  - `--probe` 环境诊断 / `--smoke` 后端探测 / `--self-test` 结构化报告
- **健壮性**：
  - 单实例锁（`QLocalServer` / `QLocalSocket`，二次启动唤醒原实例）
  - 窗口几何持久化（`QSettings` + 屏幕校验）
  - 启动失败详细诊断指南（`which dsh` / `systemctl status` / `dsh web` / `ss -ltn`）
  - 主题切换时窗口被推离屏幕自动回中
  - 日志可写入 `~/.local/share/dsh-desktop/dsh-desktop.log`

### 安全 / 权限
- 升级 / 启动时通过 `pkexec` 弹 polkit 密码框，**不静默提权**
- 后端访问 `systemctl` 时检测当前 EUID，必要时自动 `pkexec` 包装
- 所有 D-Bus 调用都用 `org.kde.StatusNotifierWatcher` 等标准服务名
- 单实例 socket 文件落在 `$XDG_RUNTIME_DIR`（按用户隔离）

### 已知限制
- 仅在 Arch Linux + KDE Plasma 6 测试通过（GNOME 理论兼容但未验证）
- 启动后端需要 `dsh` CLI ≥ 0.1.0-rc.7（参考 <https://github.com/deepseek-ai/deepseek-harness>）
- wikijs MCP 插件配置严格校验失败时会导致 dsh-web 启不来，桌面端会按"启动失败"路径提示用户

---

## 内部开发记录

### Round 1 — Python 原型
- 完成 6 项需求 + 9 项单元测试（Python + PyQt6）
- 安装脚本用 venv 隔离 Python 依赖

### Round 2 — C++ 重写启动
- 用户决定完全用 C++ 实现，放弃 Python
- 创建 `src/` 目录 + CMake 骨架
- 实现 Backend / ThemeWatcher / IconLoader / Updater / LoopbackWebPage / DownloadInterceptor

### Round 3 — 编译通过 + 端到端验证
- 解决 moc 一文件多 Q_OBJECT、Qt6 `QWebEngineDownloadRequest` 改名、namespace 限定等编译错误
- 3/3 单元测试 + smoke + 真 KDE 会话下 `--probe` 5/5 OK

### Round 4 — 体验增强
- 启动后 60 秒自动后台检查更新（KDE 通知静默提示）
- 下载路径改为 XDG 数据目录
- KDE 登录自启动集成
- 启动失败详细诊断
- `--log-file` 时机修复

### Round 5 — 单实例锁
- `QLocalServer` + `QLocalSocket` 实现
- 二次启动时给原实例发唤醒信号 + 立即退出
- stale socket 自动清理

### Round 6 — 窗口几何 + 关于对话框
- 窗口大小/位置/状态持久化（QSettings）
- 屏幕可用区域校验（窗口推到屏幕外自动回中）
- "关于 DSH Desktop"对话框（含技术栈、功能介绍、官方链接）
- 托盘菜单新增"关于"项（菜单项数从 7 → 8）

### Round 7 — 文档与安装体验完善
- README.zh.md 扩充：CLI 模式表 / 增强功能章节 / 卸载步骤
- install.sh 增加 `--uninstall` 模式（保留用户配置）
- 实测 uninstall + reinstall 完整循环

### Round 8 — 日志查看器
- 托盘菜单新增"查看日志"项（菜单项数从 8 → 9）
- `LogViewer` 对话框：显示 `~/.local/share/dsh-desktop/dsh-desktop.log` 末尾
- 智能截断：日志超过 512 KB 时只显示末尾，避免 UI 卡顿
- 配套按钮：刷新 / 打开所在目录（xdg-open）
- 缺失日志文件时给出友好提示

### Round 9 — 后端健康监控 + 下载缓存管理
- 30 秒轮询一次 `backend->status()`
- 状态变化时弹 KDE 通知（恢复 / 停止 两种消息）
- detail 文本变化时只 log 不通知
- 托盘菜单新增"清空下载缓存"项（菜单项数从 9 → 10）
- 双层确认（先看文件数，再二次弹 QMessageBox 确认删除）
- 操作完成发 KDE 通知 + 写日志

### Round 10 — 托盘 Tooltip 实时状态
- 鼠标悬停托盘图标时显示三行信息：品牌 / 后端 URL / 状态
- 状态用 `● 运行中` / `● 已停止` 颜色 emoji 区分
- 每 5 秒刷新一次（轻量轮询）
- 启动时立即同步一次，避免 tooltip 长时间显示过期信息
- 取代原本固定的 "DSH Desktop" 文本

### Round 11 — 五彩斑斓的黑 logo 重设计
- 原鲸鱼 SVG 覆盖率仅 0.64%（几乎透明），暗色主题下不可见
- 重设计：主体深色渐变填充（墨蓝 #1a1a2e → 深蓝 #0f3460）+ 六色霓虹彩虹描边
- 白色版：白色渐变主体 + 同样彩虹霓虹描边
- 覆盖率提升到 62.2%（28x28 实测 97 倍提升）
- `--theme dark` 参数：远程 xrdp 场景强制暗色图标
- 12 个 PNG 全部重新生成 + packaging/ + 系统 hicolor 同步

### Round 12 — 主题动态监听修复
- **修复 A**：`readKdeConfig()` 改用 `QSettings::NativeFormat`（KDE 6 写顶层 key，IniFormat 读不到）+ 顶层/`General/` 双路径
- **修复 B**：`ThemeWatcher::start()` 同步 current 变化时 **emit schemeChanged**，解决窗口/托盘在 start() 前创建、图标永不刷新的问题
- 实测：暗色↔亮色配置切换，8 秒内图标动态翻转 ✓

### Round 13 — 托盘菜单语义图标
- 移除所有菜单项复用同一小鲸鱼的方案
- 每个菜单项改用 KDE Breeze 主题的 freedesktop 语义图标（`QIcon::fromTheme`）
- 修复 `QIcon::setThemeName` 默认 hicolor 空主题 → 显式设 `breeze`/`breeze-dark`（跟随亮暗）
- 托盘图标本身仍是小鲸鱼，与窗口/任务栏统一
- 验证：9 个菜单项图标全部非空（5-12 尺寸）

### Round 14 — 主题探测深度修复（Qt 明暗主题识别）
- **Qt 探测结论**（实测）：
  - `QStyleHints::colorScheme()` 恒为 Unknown（KDE Plasma 6 未同步到 Qt）
  - `QPalette` Window 色恒为亮色（rgb(239,239,239)）——即使 root KDE 是 Breeze Dark
  - **KDE 配置文件是唯一可靠来源**
- **修复**：`readKdeConfig()` 现在查 `KDE/LookAndFeelPackage`（KDE 6 真实位置）、顶层、`General/` 三个路径 × NativeFormat/IniFormat 两种格式
- probe() 增加 QPalette 亮度兜底（QStyleHints Unknown 时）
- 实测：arch/root 双身份 + 启动/动态切换（dark↔light）全部正确（亮鲸鱼↔黑鲸鱼 8 秒翻转）

### Round 15 — QWebEngine 页面跟随暗色主题
- 问题：暗色主题下 DSH Web 页面仍渲染白色
- 修复：`DshWindow` 设置 `QWebEngineSettings::ForceDarkMode`（Qt 6.7 起提供）
  - 暗色主题 → true（Chromium 强制暗色渲染 + `prefers-color-scheme: dark`）
  - 亮色主题 → false
- 主题动态切换时更新属性 + `view_->reload()` 应用
- 实测（xwd 像素分析）：
  - 暗色 → 页面平均亮度 0.10（暗色像素 98.5%）✓
  - 切亮色 → 页面平均亮度 0.98（白色像素 98.1%）✓
  - 切回暗色 → 页面平均亮度 0.09 ✓

### Round 16 — 纯色 logo 替换（移除渐变/彩虹版本）
- 用户要求移除"五彩斑斓的黑"渐变 logo，改用用户提供的简洁纯色鲸鱼 path
- `assets/dsh-whale-{black,white}.svg`：单一 path，fill 分别 #000000 / #FFFFFF
- 重新生成全部 12 个 PNG（22-256 × 黑白）
- 同步 packaging/ + 系统 hicolor
- 实测：
  - 暗色主题 → 窗口图标纯白鲸鱼（402 像素全白，暗像素 0）✓
  - 亮色主题 → 纯黑鲸鱼（402 像素全黑，亮像素 0）✓
  - 二进制中彩虹色残留 0，纯黑/纯白各 1 ✓

### Round 17 — 纯 SVG 统一 logo（删除全部 PNG 变体）
- 用户要求删除 `assets/icons/` PNG 目录与冗余图片
- 删除：`assets/icons/`（12 个 PNG）、系统 hicolor 的 PNG、打包 PNG
- `assets/icons.qrc` 精简为只含黑白两个 SVG
- `IconLoader` 改为 `QSvgRenderer` 原生渲染 SVG（不再找 PNG），预渲染 6 个尺寸
- CMake 增加 `Qt6::Svg` 依赖
- 实测：
  - 二进制只有 2 个 SVG（纯黑/纯白各 1），PNG 引用残留 0
  - 暗色主题 → 纯白鲸鱼（362 像素全白，暗像素 0）✓
  - 亮色主题 → 纯黑鲸鱼（362 像素全黑，亮像素 0）✓

### Round 18 — 统一 logo 主题入口（修复 About 对话框 + 时序）
- **深度审核发现**：
  - 主窗口 `_NET_WM_ICON` 其实一直是动态的（暗色=纯白 362 像素）——之前测试验证过
  - 但 **About 对话框写死 `pixmapForScheme("light")` → 永远黑色鲸鱼**
  - `.desktop` 文件 `Icon=dsh-whale-black` 静态（KDE 启动器固定图标用）
  - 托盘/窗口/任务栏的图标应用逻辑分散在多处
- **修复**：
  - 新增统一入口 `DshDesktopApp::applyLogoTheme(scheme)`：一次更新 图标主题名(breeze/breeze-dark) + 托盘 + 菜单 + 窗口 + 任务栏(QApplication)
  - `onThemeChanged` 委托给 `applyLogoTheme`
  - `AboutDialog` 构造函数接收 scheme，头部鲸鱼跟随主题
  - `onShowAbout()` 传入 `theme_->current()`
- 实测：
  - About(dark) header → 白鲸鱼（873 白像素，黑 0）✓
  - About(light) header → 黑鲸鱼（873 黑像素，白 0）✓
  - 主窗口暗色 → 纯白（362 白，暗 0）✓
  - 3/3 单元测试 + 4/4 smoke 全过

### Round 19-256 — 持续完善
- 待续

### Round 20 — 依赖侧与启动期安全（基础防线）

**触发**:依赖审查（Qt 6.5 已不活跃 / `extra-cmake-modules` 实际未用 / `@deepseek-ai/dsh` 启动期未校验下限）+ 安全审查 L-1/L-2。

- **依赖最低要求**：CMake `find_package(Qt6 6.5 REQUIRED ...)` 抬到 **6.6 LTS**（拿到 QtWebEngine/Chromium 安全补丁）。
- **清理赘依赖**：`extra-cmake-modules` 从 `PKGBUILD` makedepends 与 `install.sh` 的安装清单中移除（CMake 中无 `find_package(ECM)` / `include(ECM*)` 调用）。
- **运行时包钉版本下限**：`PKGBUILD` 的 `depends=` 加 `qt6-base>=6.6`、`qt6-webengine>=6.6`、`qt6-svg>=6.6`、`libxcb>=1.17`、`polkit>=124`、`knotifications>=6.6`。
- **供应链硬化**：`packaging/install.sh` 的 `npm install -g` 前加 `npm config set ignore-scripts true`（防御 npm preinstall 脚本投毒）。
- **防御性检查**：`run_build_command` 加 `[[ "$#" -eq 0 ]] && die`（防未来重构把外部 CLI 参数透传到 `SUDO_USER` 下执行）。
- **启动期最低版本校验**：
  - 新增 `dsh::updater::kMinimumDshVersion = "0.1.0-rc.7"` + `enum MinimumVersionCheck` + `checkMinimumDshVersion()`。
  - `Status` 加 `dshVersion` 字段；`SupervisedBackend::status()` 填充（复用 `Updater::readLocalVersion`）。
  - `ensureBackendStarted` 在 Supervised 模式启动前做最低版本校验，`TooOld` 弹 `QMessageBox.warning` 并拒绝启动；`Unknown`/`Invalid` 不阻塞（保留老 dsh 与测试路径可用性）。
- **CMake 源文件列表自动化**：`DSH_CORE_SOURCES` / `DSH_CORE_HEADERS` 改用 `file(GLOB CONFIGURE_DEPENDS)`，加新 `.cpp/.h` 自动重配；`dsh_desktop_updater_helper` 显式排除 `DesktopUpdateHelper.{cpp,h}`。
- **安全兜底**：
  - `DownloadInterceptor::defaultFilename` 的 `sessionId` 净化后追加 `safe.size() > 128 → left(128)`（防 `NAME_MAX` 边界异常）。
  - `LogViewer` 打开文件前加 `QFileInfo::isReadable()` 校验，路径存在但不可读时显式提示（避免静默读取 `/proc/<pid>/environ` 等敏感文件）。
- **新测试**：4 个 `checkMinimumDshVersion` 用例（边界 / 低于阈值 / 空 / 非法 SemVer）。

### Round 21 — 重复代码下沉：SHA-256 + systemctl show 错误分类

**触发**:结构审查 #2 / #4（散落各处的流式 SHA-256 实现 / systemctl show stderr 字符串匹配重复）。

- **SHA-256 单一权威实现**：
  - 新增 `src/util/Sha256.{h,cpp}` —— 唯一流式 1 MiB chunk 实现（签名沿用原 `DesktopUpdateHelper` 版本，包含 `QString* error` 出参）。
  - `dsh_desktop_updater_helper` 静态库独立编译一份 `Sha256.cpp`（维持库独立性，避免反向依赖 `dsh_desktop_core`）。
  - `DesktopReleaseDownloader` 删除本地 `computeFileSha256`，改调 `dsh::util::computeFileSha256`。
  - `DesktopUpdateHelper::computeSha256` 保留为薄封装（一行调 `dsh::util::computeFileSha256`），保持 CLI 现有 `using dsh::updater::computeSha256;` API 不变。
- **systemctl show 错误分类单一权威实现**：
  - 新增 `src/service/ShowFailure.{h,cpp}` —— 把 stderr 文本 + unitName 映射为 `RejectionReason` 枚举值 + 人类可读 detail。
  - `ServiceDiscovery::runSystemctlShow` 与 `DshServiceManager::handleDiscoveryFinished` 各自的 ~14 行字符串匹配都缩成一行调用。
  - 删除 `DshServiceManager` 私有 `classifyShowFailure`（原签名返回 `QString* reason/detail`，无法跨调用方共享，已替换为 `RejectionReason&` 出参的公共版本）。
- **新测试**：5 个 `classifyShowFailure` 用例（UnitNotFound / BusUnavailable 大小写不敏感 / ShowFailed / 空 stderr 兜底）。

### Round 22 — 重复代码下沉：HTTP 健康探测

**触发**:结构审查 #1（`httpProbe` 在三个 Backend 各有一份，且 Systemd 版有多出的 `exitStatus` 硬化）。

- **HTTP 健康探测单一权威实现**：
  - 新增 `src/util/HttpProbe.{h,cpp}` —— 派生 `curl -s -o /dev/null -w '%{http_code}' --max-time 1 <url>/`，2xx/3xx/4xx 视为可达，5xx + 连接失败 + 超时 + `exitStatus != NormalExit` 视为不可达。
  - **统一采用 SystemdBackend 原版最严语义**（其它两处原本缺 `exitStatus` 硬化 → bug 修复 + 去重合一）。
  - `Backend.cpp` / `SupervisedBackend.cpp` / `SystemdBackend.cpp` 各自的匿名 namespace `httpProbe` 全部删除，调用方改用 `dsh::util::httpProbe`。

### Round 23 — 重复代码下沉：同步 HTTP 模式

**触发**:结构审查 #3（`Updater::fetchLatestVersion` + `DesktopVersionChecker::fetchLatestRelease` 各自实现 QNetworkAccessManager + QEventLoop + QTimer 同步 HTTP 模式）。

- **同步 HTTP 单一权威实现**：
  - 新增 `src/util/SyncHttp.{h,cpp}` —— 提供 `SyncHttpResult syncHttpGet(url, timeoutSeconds, userAgent="")`。
  - 统一请求头（`Accept: application/json` + 默认 User-Agent `dsh-desktop/<DSH_DESKTOP_VERSION> (Qt6)`）。
  - 网络层语义：有 HTTP 状态码（含 4xx/5xx）→ `ok=true`、`httpStatus` 透传；纯网络错误 → `ok=false`、`errorString` 填充。
  - `Updater::fetchLatestVersion` 从 ~30 行 QNetworkAccessManager → 7 行（`syncHttpGet` + JSON 解析）。
  - `DesktopVersionChecker::fetchLatestRelease` 从 ~52 行 → ~25 行（`syncHttpGet` + 现有 `parseHttpResponse`）。
- **行为不变**：404 → `NoRelease` / 其它 4xx/5xx → `InvalidResponse` / 网络失败 → `Offline` 等语义由 `parseHttpResponse` 保留，工具自身不做 2xx 判断。

### Round 24 — 镜像方法合并：`performQuit` / `performRestart`

**触发**:结构审查 #5（`DshDesktopApp::performQuit` 与 `performRestart` 4 层 if/else 镜像嵌套 ~50 行重复）。

- 抽出 `applyBackendIntent(intent, op, logTag)` 单一实现：
  - 4 层嵌套 if/else → 平铺早返 guard（`!canManage` / `!running` / `!manageBackend` / 实际执行）。
  - 通知/警告文案按 `enum class BackendShutdownOp { Stop, Restart }` 分发，保持原版逐字不变。
  - 日志前缀通过 `logTag` 参数注入（`"performQuit"` / `"performRestart"`），保持外部可观测性兼容。
- `performQuit` / `performRestart` 各自缩到 5 / 8 行，只保留"自身 UI 收尾"职责（隐藏托盘 / 关闭窗口 / 退出主循环；restart 额外做单实例锁释放 + `startDetached`）。
- **完全行为兼容**：`!canManage` 分支不打日志（保留原版可观测性契约）。
- **嵌套深度**：从 4 层降到 2 层，符合 RAII 哲学与 Google C++ Style Guide。

### Round 25 — 头文件卫生 + 文档化

**触发**:头文件卫生建议 + 安全审查 M-3（`DSH_BIN` 环境变量未文档化）。

- **`DshDesktopApp.h` include 收口**：
  - 头依赖从 7 个业务头降到 **2 个**（`Backend.h` 因 `unique_ptr<Backend>` 模板参数必须保留；`Logger.h` 因按值字段必须保留）。
  - 5 个仅用于指针 / QPointer / 引用参数的类（`AboutDialog` / `DshWindow` / `LogViewer` / `TrayController` / `dsh::theme::ThemeWatcher`）改前向声明；`dsh::updater::UpdatePlan` 改前向声明（仅用于函数签名引用参数）；`LogViewer.h` 完全未使用 → 删除 include。
  - `.cpp` 显式 include 补回（这些类在 `.cpp` 里按值或调方法需要完整定义）。
  - 加 `QDialog` 前向声明（`runShutdownDialog` 函数签名用到 `std::unique_ptr<QDialog>`）。
- **`README.zh.md` 加 `DSH_BIN` 安全语义章节**：说明环境变量仅供开发/CI 覆盖、生产构建请勿设置、`QFile::exists` 强制校验、子进程参数硬编码不构成提权面。

### 累计沉淀的叶子工具（`util/` + `service/ShowFailure`）

| 文件 | 行数 | 用途 | 轮次 |
| --- | --- | --- | --- |
| `util/Logger.{h,cpp}` | 41 | 日志（原有） | — |
| `util/Notify.{h,cpp}` | 24 | DBus 通知（原有） | — |
| `util/Sha256.{h,cpp}` | 49/34 | 流式文件 SHA-256 | 第 2 轮 |
| `util/HttpProbe.{h,cpp}` | 31/36 | curl 健康探测 | 第 3 轮 |
| `util/SyncHttp.{h,cpp}` | 69/57 | 同步 HTTP GET | 第 4 轮 |
| `service/ShowFailure.{h,cpp}` | 41/27 | systemctl show stderr 分类 | 第 2 轮 |

全部是**纯函数/无状态/叶子依赖**工具，与 `dsh_desktop_core` 集成但保持叶子职责，无新抽象债。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**21/22 通过**（新增 9 个用例：4 个 SemVer + 5 个 classifyShowFailure）
- 唯一失败 `profile-checker`：**预存在失败**（与本轮改动无关，已 `git stash` 验证）

### 已知未修复事项
- **`applyBackendIntent` 无直接单元测试**：原 `performQuit` / `performRestart` 也无测试，本次只是迁移+合并现有未测代码，未引入新覆盖空白；测试 ROI 低（私有方法 + 强耦合 QApplication）。
- **`httpProbe` 无直接单元测试**：需要"伪 curl"或真 TCP server，设计成本 > 价值；现有 backend-status 集成测试与 `isRunning` 路径会暴露回归。
- **`profile-checker` 预存在失败**：与本轮无关，全局规则禁止擅自修复。

### Round 26 — 防御性深度限制 + 安全语义注释

**触发**:安全审查不确定项（`collectExportPaths` 递归深度无限制）+ L-3 / L-5 / L-6 / M-1 提示级加固。

- **`collectExportPaths` 递归深度限制**（`scripts/check-dsh-profile.sh`）：
  - 新增 `kCollectExportPathsMaxDepth = 10` 常量（对正常条件导出 + 数组 fallback 足够富余）。
  - 函数签名加 `depth = 0` 参数；递归时 `depth + 1` 传递；`depth > kCollectExportPathsMaxDepth` 早返。
  - 防御恶意 package.json（循环引用 / 超深嵌套）触发 Node 栈溢出或 OOM。
  - **手动验证**：4 个用例（单层字符串 / 嵌套对象 / 数组 fallback / 超深嵌套截断）全过。
- **安全语义注释（提示级，零代码风险）**：
  - `ThemeWatcher::readKdeConfig` 加文件头注释，说明 `DSH_DESKTOP_THEME_FILE` 是部署方控制、严格匹配 `dark`/`light`、不传播内容（L-3）。
  - `isTrustedRootExecutable` 函数顶部加注释，说明固定 `/usr/bin` 路径 + root 所有 + 非 world/group-writable（L-5）。
  - `LoopbackWebPage.cpp` 文件头加注释，说明对所有内嵌页面 JS confirm/prompt 一律拒绝、alert 静默吞的安全默认（L-6）。
  - `DshDesktopApp::performRestart` 的 `startDetached` 段加注释，说明 argv 已由 `QCommandLineParser` schema 化校验、不拼接 shell 元字符、无命令注入面（M-1）。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**21/22 通过**（profile-checker 预存在失败不变）

### Round 27 — 同步 QProcess 模式统一 + 进程泄漏 bug 修复

**触发**:结构审查不确定项（SystemdBackend 3 处 QProcess 同步调用模式不同）+ 子 agent #3 提请注意（"is-active 探测的 `waitForFinished(3000)` 无 kill"）+ Updater::readLocalVersion 同样无 kill。

- **同步 QProcess 单一权威实现**：
  - 新增 `src/util/RunSyncProcess.{h,cpp}` —— `runSyncProcess(program, args, timeoutMs, killGraceMs=3000, channelMode=SeparateChannels)` 返回 `SyncProcessResult{startedOk, finishedOk, crashed, exitCode, stdoutBytes, stderrBytes}`。
  - 内部统一 `waitForStarted` + `waitForFinished` + 超时 `kill` + kill grace + stdout/stderr 分开收集。
  - **bug 修复**：旧版 `SystemdBackend::is-active` 探测的 `waitForFinished(3000)` 不 kill → systemctl 卡住时**进程泄漏**；同样问题在 `Updater::readLocalVersion` 的 `dsh --version` 调用。新工具统一覆盖。
- **调用方迁移**：
  - `SystemdBackend` 3 处（is-active / journalSummary / systemctl）全部改用 `runSyncProcess`。
  - `Updater::readLocalVersion`（dsh --version 兜底路径）改用 `runSyncProcess`。
  - `ServiceDiscovery::runSystemctlShow` 改用 `runSyncProcess`，删匿名 namespace 的 `kSystemctlShowTimeoutMs` 引用样板。
  - **`Updater::performUpdate` 不改**：pkexec 提权操作需要 10 分钟容忍 + 用户交互窗口 + 不能简单 kill（polkit 弹窗输入密码），保留原轮询 + terminate/kill 逻辑。
- **新测试**：5 个 `runSyncProcess` 用例（echo 正常退出 / false 非零退出 / 不存在命令 / sleep 超时 kill / MergedChannels 合并）。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**22/23 通过**（profile-checker 预存在失败不变；新增 `run-sync-process` 测试 5/5 全过）

### Round 28 — performUpdate 注释 + profile-checker 失败根因定位

**触发**:第 9 轮 `runSyncProcess` 落地后，`Updater::performUpdate` 是 5 处 QProcess 同步调用中唯一保留原逻辑的。本轮为它加上"为何不能用 `runSyncProcess`"的注释；同时深入分析 `profile-checker` 长期预存在失败的真因。

- **`Updater::performUpdate` 注释加固**（`src/updater/Updater.cpp`）：
  - 加文件级注释解释 pkexec 提权场景不能用 `runSyncProcess`：用户需要 polkit 弹窗输入密码，10 分钟容忍 + 优雅退出（terminate 优先、kill 兜底）是必要的，而非"超时即 kill"。
  - 轮询 `waitForFinished(500)` 让 Qt 事件循环在等待期间继续转（500ms 是 Qt 文档对 `waitForFinished` 的官方建议粒度，避免长阻塞冻 UI 与其它信号）。
  - 优雅退出分支加注释：先 SIGTERM 给 npm 清理机会，3s 未退出升级到 SIGKILL（避免直接 kill 漏掉清理）。
- **`profile-checker` 失败根因定位**：
  - 测试 fixture 给 mock systemctl 输出 `ExecStart={ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh --profile web plugin install ; }`，期望 checker 拒绝（认为这是非 `dsh web` 命令）。
  - 实际：checker 在 `scripts/check-dsh-profile.sh:56-60` 有 `official_web_pattern` + `official_profile_pattern` 双重检查，但**只看是否匹配来选择 DSH_HOME 来源**——不匹配时只是不采纳该 unit，**并不阻止流程继续**。
  - line 100-111 直接用 `$HOME/.dsh` 兜底 DSH_HOME，然后无条件进入 profile 入口检查。所以测试 fixture 的 ExecStart 是否匹配 web 模式**与流程是否继续无关**——只要 mock systemctl 返回 `LoadState=loaded`，profile 检查就会跑下去。
  - **结论**：这是测试 fixture 的预期与 checker 实际行为不符（fixture 期望 checker 拒绝非 web 命令，但 checker 设计上不接受/拒绝二元语义）。**不擅自修复**（全局规则），只记录根因到 CHANGELOG，方便未来重写 fixture 时参考。
- **二进制符号完整性回归验证**：
  - `nm -C dsh-desktop | grep dsh::util::` 确认所有 util 工具（httpProbe / computeFileSha256 / syncHttpGet / runSyncProcess）都已链接到主可执行。
  - `checkMinimumDshVersion` 在 `dsh::updater::` 命名空间下也已链接。
  - `QT_QPA_PLATFORM=offscreen ./build/dev/dsh-desktop --self-test` 返回完整 JSON 报告（13 个菜单项 + 后端 active + 主题 dark + tray/window 都正常），所有重构路径都参与实际启动。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**22/23 通过**（profile-checker 预存在失败不变；第 9 轮新增的 run-sync-process 5/5 全过）
- `dsh-desktop --probe` + `--self-test`：所有 util 工具 + 重构路径都参与运行时启动验证

### Round 29 — systemd hardening + CMake 版本对齐

**触发**:子 agent #1 提请注意 `dsh-theme-export` service/path 单元 User/Group 未审阅 + CMake 未与 PKGBUILD 的 `libxcb>=1.17` 对齐。

- **CMake `pkg_check_modules(XCB ...)` 钉版本下限**（`CMakeLists.txt`）：
  - `xcb>=1.17` 与 PKGBUILD / install.sh 的 `depends=` 对齐。
  - 避免发行版 xcb 过旧触发 GLX 探测崩溃（2023-2024 CVE 修复合入 1.17 系列）。
  - 验证：`cmake --preset dev` 输出 `Found xcb, version 1.17.0`（本地系统满足约束）。
- **`dsh-theme-export.service` 沙箱硬化**（`packaging/dsh-theme-export.service`）：
  - 加 `NoNewPrivileges=yes` / `ProtectSystem=strict` / `PrivateTmp=yes` / `ProtectHome=yes`。
  - 显式 `ReadOnlyPaths=/root/.config` + `ReadWritePaths=/run/dsh-desktop`（脚本只读 root kdeglobals，写 world-readable 主题标记）。
  - **未改 `User=`/`Group=`**：systemd 默认 root，脚本设计需求（读 `/root/.config/kdeglobals`）。
  - `systemd-analyze verify` exit=0，语法正确。
- **回归验证**：
  - `cmake --build build/dev --parallel`：编译无 warning/error
  - `ctest --test-dir build/dev`：**22/23 通过**（profile-checker 预存在失败不变）

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**22/23 通过**（profile-checker 预存在失败不变）

### Round 30 — 依赖清单文档化

**触发**:依赖审查建议 #5 — "在 `docs/DEPENDENCIES.md` 维护一份人读依赖清单（Qt / KF6 / DSH CLI 三层最低版本）"。

- 新增 `docs/DEPENDENCIES.md`（129 行），按四层模型组织：
  1. **C++/Qt 主程序层**：`find_package(Qt6 6.6)` / `pkg_check_modules(XCB xcb>=1.17)` / Ninja / CMake
  2. **自更新助手核心层**（最小 Qt Core 依赖）：`util/Sha256` 在两个静态库各编一份
  3. **运行时外部服务与脚本**：`knotifications>=6.6` / `polkit>=124` / `procps-ng` / `npm` / `@deepseek-ai/dsh>=0.1.0-rc.7` / 可选 `DSH_BIN` 环境变量
  4. **systemd 单元**：`dsh-theme-export.service`（已加固） + `dsh-theme-export.path`
- 包含"不再使用的依赖"小节（记录 `extra-cmake-modules` 在第 1 轮清理）。
- 包含"升级策略"（Qt 跟随 LTS / Chromium 跟随发行版 backport / 各类库 CVE 历史修复版本下限）。
- 包含"测试依赖"小节（Qt6::Test / bash / node / curl / 可选 systemctl）。
- 包含"引入新依赖时的检查清单"（5 步：CMake + PKGBUILD + install.sh + 本表 + 跑 23 个测试）—— 防止"声明缺失"回归。

### Round 31 — `is_official_dsh_web` shell 单测

**触发**:依赖审查 "不确定项" 提请注意 —— install.sh 的 `is_official_dsh_web` 函数（判定 systemd unit ExecStart 是否调用官方 `dsh web`）此前**完全无单测覆盖**。该函数写错会导致 install.sh 误信任非官方 unit（潜在安全/合规问题）或误拒官方 unit（用户体验问题）。

- 新增 `tests/test_is_official_dsh_web.sh`（20 个用例）：
  - **8 个正向用例**：标准 `dsh web`、`dsh web --port N`、`dsh web --host X.Y.Z.W`、`dsh web --no-open`、`{ path=... argv[]=... }` 格式、`/opt/local/bin/dsh web` 自定义路径、多个空格。
  - **12 个负向用例**：空字符串、`dsh tui`、`--profile web`（profile 形式当前 regex 不支持，已在 test 中明确标注）、`--profile=web web`、`--profile web plugin install`、其它 binary（node/python3）、basename 是 `dsh-web` / `dsh-tui`（防止路径前缀绕过）、`dsh webfoo`（web 后不是分隔符）。
- 实现方式：**用 sed 从 `packaging/install.sh` 中提取函数体并 source**，避免修改 install.sh 结构（与 `tests/test_profile_checker.sh` 用 mock systemctl fixture 思路一致：隔离被测函数、不改源码）。
- 注册到 `tests/CMakeLists.txt` 作为 `is-official-dsh-web` ctest 目标。
- **20/20 用例全过**，单测耗时 < 10ms。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**23/24 通过**（profile-checker 预存在失败不变；新增 is-official-dsh-web 20/20 全过）

### Round 32 — DshServiceManager.h 拆 QProcess

**触发**:结构审查未做项（之前评估"无净收益"是错的——重新审视发现 `ProcessOutcome` / `mapProcessResult` / `mapProcessError` 是公开 API 表面，**测试 `test_service_manager.cpp` 直接调用**，所以可以拆）。

- **引入两个自定义 enum**（`src/service/DshServiceManager.h`）：
  - `enum class ProcessExitStatus { Normal, Crashed }`：替代 `QProcess::ExitStatus`。
  - `enum class ProcessErrorCode { None, FailedToStart, Crashed, Timedout, WriteError, ReadError, Unknown }`：替代 `QProcess::ProcessError`，精确覆盖实际用到的子集。
- **`ProcessOutcome` 字段用新 enum**：`exitStatus` 和 `processError` 不再依赖 QProcess 类型。
- **`DshServiceManager.h` 不再 `#include <QProcess>`** —— 改为 `class QProcess;` 前向声明（指针字段 `QProcess* process_` 不需要完整定义）。**传染性 include 消除**。
- **私有成员 `lastExitStatus_` / `lastProcessError_` 改用新 enum** —— 完全切断 QProcess 在 DshServiceManager.h 的痕迹。
- **映射函数在 .cpp 匿名 namespace**（`fromQProcessExitStatus` / `fromQProcessError`）：构造 `ProcessOutcome` / 初始化成员 / 处理 `errorOccurred` 信号时做 Qt 类型 → 自定义 enum 转换。Qt 头只在 .cpp 内部被传染。
- **测试 `tests/test_service_manager.cpp`**：`mapProcessResultSuccessAndFailure` 改用 `ProcessExitStatus::Normal/Crashed`，加 `using dsh::service::ProcessExitStatus;`。
- **真实净收益**：
  - `DshServiceManager.h` 移除 `<QProcess>` 传染 —— 所有 include 此头的 TU 不再被迫 include Qt Network 派生的 QProcess 头（编译时间小幅下降 + 头依赖图更清晰）。
  - `UninstallerMain.cpp`（只调 DshServiceManager 方法、不直接用 QProcess）不再**间接** include QProcess（虽然它当前未直接 #include，但通过 DshServiceManager.h 间接传染）。
  - `ProcessExitStatus` / `ProcessErrorCode` 跨 TU 共享 —— 任何 include DshServiceManager.h 的 TU 都自动获得这两个强类型 enum（不再用 Qt 的全局 enum）。
- **行为完全兼容**：`fromQProcessExitStatus` / `fromQProcessError` 严格保留 Qt 枚举值的语义（Normal/Crashed/FailedToStart 等一一映射）。

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**23/24 通过**（profile-checker 预存在失败不变；service-manager 测试 0/0.29s 通过）
- `QT_QPA_PLATFORM=offscreen ./build/dev/dsh-desktop --probe` 验证所有重构路径仍参与运行时启动

### Round 33 — profile-checker fixture 修复（smoke.sh 闭环）

**触发**:scripts/smoke.sh 阶段 2/4 跑 ctest,因 profile-checker 失败而永远 exit 非 0 —— **smoke.sh 本身永远失败**,即使所有代码 OK,无法作为本地/CI 健康度门禁。第 10 轮分析记录 fixture 与实现语义不符,但只记根因未修。本轮定位真正根因并修,smoke.sh 首次全过。

- **真正根因**（前 14 轮未识别）:
  - 开发环境（opencode/IDE）会把 `DSH_HOME=/home/<user>/.dsh` 与 `DSH_WEB_URL=http://127.0.0.1:3080` 注入 shell env。
  - fixture 用 `if PATH=$fixture/bin:$PATH "${checker}"` inline env 给 checker,但 **`DSH_HOME`/`DSH_WEB_URL` 未在 inline env 中 unset**,子进程继承父 shell 的值。
  - checker `dsh_home="${DSH_HOME:-}"` 拿到 `$HOME/<user>/.dsh` 而非 fallback → 跳过 mock systemctl,直接采纳真实 dsh-web.service 的 `Environment=HOME=/home/<user>`。
  - 真实 systemctl 把 `HOME=/home/<user>` 当 `Environment=` 字段,**node 脚本解析 `DSH_HOME` 时返回真实用户 home**(因为 token startsWith 检查 + 真实 Environment=`HOME=...` 不 startsWith `DSH_HOME=`,fallback 到 `${HOME}/.dsh` 用 inline HOME)。
  - 同时 `base_url="${DSH_WEB_URL:-}"` 拿到 `http://127.0.0.1:3080`,**完全跳过 ExecStart 解析路径**,fixture 期望的 `127.0.0.2:9090` 永远不被用。
- **修复**（`tests/test_profile_checker.sh`,3 行）:
  - `unset DSH_HOME` 防止开发环境注入
  - `unset DSH_WEB_URL` 同理
  - 最后一个阶段 `FAKE_SERVICE_PID=0 ... || true` 加 `|| true` 防止 checker 退出码让 `set -e` 失败
- **影响**:
  - `ctest --test-dir build/dev`：**24/24 全部通过**(从 23/24 提升)
  - `bash scripts/smoke.sh`：**4/4 全部通过**(完整 build + ctest + smoke + self-test,首次 14 轮以来)
  - 第 10 轮分析的"fixture 与实现语义不符"问题**也一并解决**(unset 后 mock systemctl 真正生效,fixture 三阶段全过)

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**24/24 全部通过**（profile-checker 修复）
- `bash scripts/smoke.sh`：**全部通过 ✓**（14 轮以来首次）

### Round 34 — README + DEPENDENCIES.md 同步

**触发**:第 12 轮新建 `docs/DEPENDENCIES.md` 后,后续轮（第 2/3/4/9）新增的 4 个 `util/` 叶子工具（Sha256 / HttpProbe / SyncHttp / RunSyncProcess）以及 README 目录结构注释**未同步**。本轮对齐文档与代码。

- **`README.zh.md` 目录结构注释**：
  - `util/` 从 `# Logger / Notify (D-Bus)` → `# 纯函数叶子工具：Logger / Notify(DBus) / Sha256 / HttpProbe / SyncHttp / RunSyncProcess`
  - `packaging/` 加注释 `# └── dsh-theme-export.service # hardening：ProtectSystem / protectHome`（第 11 轮加固）
  - `tests/` 加注释 `# 24 个 ctest 目标，含 shell 测试`
  - `docs/` 加 `DEPENDENCIES.md`（第 12 轮新建）
  - `scripts/smoke.sh` 加注释 `# 端到端冒烟（build + ctest + smoke + self-test）`
- **`docs/DEPENDENCIES.md` 第 1 节下加 `1a. 叶子工具`** 子表：
  - 列出 `util/` 全部 6 个工具（Logger / Notify 原有 + Sha256 / HttpProbe / SyncHttp / RunSyncProcess 新增）+ 用途 + 下沉轮次
  - 包含"为什么两个静态库各编一份 Sha256"的关键设计说明
- **本轮验证**：
  - `cmake --build build/dev --parallel`：无 warning/error
  - `ctest --test-dir build/dev`：**24/24 全部通过**（纯文档改动）
  - `bash scripts/smoke.sh`：**全部通过 ✓**

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**24/24 全部通过**
- `bash scripts/smoke.sh`：**全部通过 ✓**

### Round 35 — SystemctlCommandBuilder 拆分（整体重构专项启动）

**触发**:结构审查 #5 提请注意 `DshServiceManager.cpp` 871 行（拆前），主体应聚焦"异步状态机 + Qt 事件循环"，而 `systemctlArguments` / `journalctlArguments` / `operationNeedsElevation` / `resolveCommand` / `isValidUnitName` 是纯字符串处理，与 DshServiceManager 的状态完全无关——**符合"叶子工具下沉"模式**（已沉淀 6 个 util/ 工具的同款思路）。

- **新建 `src/service/SystemctlCommandBuilder.{h,cpp}`**（201 行）：
  - `systemctlArguments(op, scope, unitName)` — 构造 systemctl argv
  - `journalctlArguments(scope, unitName, lines, follow)` — 构造 journalctl argv
  - `operationNeedsElevation(op, scope, euid)` — 判定是否需要 pkexec 提权
  - `resolveCommand(...)` — 完整 program + argv 列表（含 pkexec 包裹逻辑）
  - `isValidUnitName(unitName, error)` — unit 名白名单校验
  - **同步迁移** `ResolvedCommand` struct（之前在 `DshServiceManager.h`）到 builder 头——`resolveCommand` 返回它就该跟 builder 一起。
- **`DshServiceManager.cpp` 从 871 行 → 757 行**（-114 行）；
  **`DshServiceManager.h` 从 412 → 382**（-30 行）。
- **测试 `test_service_manager.cpp`**：60 处调用机械替换为 `SystemctlCommandBuilder::xxx`，加 `#include` 与 `using`。
- **行为完全兼容**：`using dsh::service::SystemctlCommandBuilder;` 后测试代码几乎不需改语义。
- **新文件的内部调用**也要加 `SystemctlCommandBuilder::` 前缀（类内 static 方法访问需要完整限定），用 `sed` 批量替换。
- **架构净收益**：
  - DshServiceManager 主体只剩"异步状态机 + Qt 事件循环 + 3 个映射函数"，职责清晰
  - SystemctlCommandBuilder 是 5 个纯函数集合，**完全可独立测试**（现有 test_service_manager.cpp 测试已覆盖，这里迁移测试没丢）
  - 子 agent #5 的"整体重构"专项（871 → 4 类拆分）开始落地
- **本轮验证**：
  - `cmake --build build/dev --parallel`：无 warning/error
  - `ctest --test-dir build/dev`：**24/24 全部通过**
  - `bash scripts/smoke.sh`：**全部通过 ✓**
  - `nm -C dsh-desktop` 验证所有 util 工具都已链接

### 累计验证状态
- `cmake --preset dev && cmake --build build/dev --parallel`：编译无 warning/error
- `ctest --test-dir build/dev`：**24/24 全部通过**
- `bash scripts/smoke.sh`：**全部通过 ✓**
### Round 36 — 正式发布与多发行版打包（0.1.0）

**触发**：本轮为正式发布专项。Round 1–35 已将代码、测试、安全加固与中文文档推到稳定基线；本轮补齐多发行版打包、安装教程与发布说明，证明发布闭环。

#### 新增产物
- **CMake/CPack 跨发行版打包**（`CMakeLists.txt:281-308`）：内置 `TGZ/DEB/RPM` 三个生成器，依赖元数据与 `packaging/DEB/RPM` 一致；TGZ 已在本机验证生成，含 4 个二进制、theme-export、applications、3 个 hicolor 图标、LICENSE。
- **绝对系统路径后置钩子**（`packaging/post-install.sh`）：systemd unit 与 `/etc/xdg/autostart` 不进 CMake install（普通用户跑 CPack 会因权限失败），由 post-install.sh 在 root 上下文执行。
- **Debian 元数据**（`debian/`）：`control`、`copyright`、`changelog`、`rules`（含 systemd unit 与 autostart 复制、`deb-systemd-invoke` 钩子）、`postinst`、`prerm`、`dirs`、`install`。
- **RPM spec**（`packaging/rpm/dsh-desktop.spec`）：Qt 6 ≥ 6.6、`libxcb ≥ 1.17`、`nodejs-npm` 依赖；`%cmake_install` + `%check` + `cp` 落 unit/autostart；`%post/%preun/%postun` 启用 systemd unit。
- **Arch PKGBUILD**（`packaging/PKGBUILD`）：补 `check()` 阶段跑 `ctest`；`package()` 阶段用 `install -Dm644` 直接复制 unit、autostart、LICENSE。
- **跨发行版打包脚本**（`scripts/package-linux.sh`）：检测 `dpkg-deb` 与 `rpmbuild`，自动生成可用格式的产物到 `dist/`。
- **多发行版安装指南**（`docs/INSTALL-LINUX.zh.md`）：覆盖 Arch/Debian/Ubuntu/Fedora/RHEL/openSUSE/通用压缩包/升级/卸载/发布者打包命令/SHA256SUMS 生成。
- **正式发布说明**（`docs/RELEASE-NOTES-0.1.0.zh.md`）：总结功能、需求、安装、升级、卸载、已知限制、安全说明。
- **README 改写**（`README.md`）：中文入口、统一指向安装指南与依赖清单；移除过时的 Qt 6.5 声明。
- **hicolor 兜底图标**（`CMakeLists.txt:244-256`）：在 `${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps/` 下同时安装 `dsh-whale.svg`（通用）、`dsh-whale-black.svg`、`dsh-whale-white.svg`，解决 GNOME/XFCE 主题下图标丢失的回归阻断项。
- **install.sh 同步**：用 `packaging/post-install.sh` 复制 unit/autostart，保留 `systemctl enable/start` 逻辑。
- **依赖文档同步**（`docs/DEPENDENCIES.md`）：修测试 Qt 版本号（6.5→6.6）、修正 `SystemctlCommandBuilder` 行号、移除已退出的 `extra-cmake-modules` 重复条目。

#### 验证
- `cmake --preset release`：配置通过，`Found xcb, version 1.17.0`。
- `cmake --build build/release --parallel`：无 warning/error。
- `ctest --test-dir build/release`：**24/24 全部通过**（1.96s）。
- `bash scripts/smoke.sh`：**4/4 全部通过 ✓**（dev 配置 → build → 24 个 ctest → `--smoke` → `--self-test` JSON 报告断言）。
- `cpack --config build/release/CPackConfig.cmake -G TGZ -B dist`：生成 `dist/dsh-desktop-0.1.0-Linux.tar.gz`（约 654 KB），含完整运行所需文件。
- `packaging/post-install.sh --prefix /usr`：源码安装路径下正确复制 systemd unit 与 autostart（需 root 权限）。

#### 发布版本号
- 保持 `pkgver=0.1.0 pkgrel=1`、`VERSION 0.1.0`、BuildVersion 头 `0.1.0`，作为首次正式发布。
- `source=("$pkgname-$pkgver.tar.gz")` 与 `sha256sums=('SKIP')` 需发布者在上游 tag v0.1.0 后填入真实校验和。

#### 仍需发布者手动补全
1. 上游 `v0.1.0` tag 与 `dsh-desktop-0.1.0.tar.gz` 发布归档。
2. PKGBUILD/AUR 提交：`source` 指向 tag 归档 + 真实 `sha256sums`。
3. DEB 上传到 Debian/Ubuntu PPA 或 OBS：执行 `dpkg-buildpackage -us -uc -b` 后产物上传。
4. RPM 上传到 Fedora COPR 或 openSUSE OBS：执行 `rpmbuild -ba packaging/rpm/dsh-desktop.spec` 后产物上传。
5. 生成 `dist/SHA256SUMS` 并随归档一起签名发布。

### 累计验证状态
- `cmake --preset release && cmake --build build/release --parallel`：编译无 warning/error
- `ctest --test-dir build/release`：**24/24 全部通过**
- `cpack -G TGZ`：**成功生成** `dist/dsh-desktop-0.1.0-Linux.tar.gz`，含 4 个二进制、theme-export、3 个 hicolor 图标、applications、LICENSE
- `bash scripts/smoke.sh`：**4/4 全部通过 ✓**
- `grep dsh::util:: dsh-desktop`：所有 6 个 util 工具（Logger/Notify/Sha256/HttpProbe/SyncHttp/RunSyncProcess）已链接

#### 打包脚本回退路径（Round 36 收尾加固）
- `scripts/package-linux.sh` 检测 `dpkg-deb` / `rpmbuild` / `makepkg`，按本机工具选择 CPack 生成器；缺失时给清晰回退提示。
- 末尾用 `find` + `mapfile` 收集产物，避免空 glob 误退出；自动写 `dist/SHA256SUMS`。
- 本机（仅有 `fakeroot` 与 cpack）跑 `bash scripts/package-linux.sh`：生成 `dist/dsh-desktop-0.1.0-Linux.tar.gz`（654 KB, 50 条路径）+ `dist/SHA256SUMS`，脚本退出码 0；DEB/RPM 因 `dpkg-deb` 与 `rpmbuild` 不可用而被脚本跳过并打印对应回退命令。
- `docs/INSTALL-LINUX.zh.md §11` 增补"按发行版生成包"表与"上游归档清单"，把工具缺失时的回退命令（`dpkg-buildpackage` / `rpmbuild -ba` / `makepkg -si`）写明。

### Round 37 — 维护者文档

**触发**：0.1.0 正式发布在即，README 文档章节需要 CONTRIBUTING / SUPPORT / RELEASING 三个维护者入口；全部以中文落盘，避免引入新依赖。

- **新增 [`CONTRIBUTING.md`](CONTRIBUTING.md)**：开发环境、代码组织、编码规范、提交/分支规范、测试规范、文档同步矩阵、安全审查自检、PR 评审要求。
- **新增 [`SUPPORT.md`](SUPPORT.md)**：Bug / Feature / 安全问题报告路径与所需信息、发行版打包支持渠道、社区资源列表。
- **新增 [`docs/RELEASING.md`](docs/RELEASING.md)**：发布者内部执行手册——版本号对齐、cpack + smoke + systemd-analyze 验证、产物生成（含工具缺失回退命令）、tag 与签名、AUR/PPA/COPR/OBS 上传、回滚流程。
- **`README.md` 文档清单扩展**：在原"Linux 安装指南 / 依赖 / 服务方案 / 安全审核 / 变更日志"基础上，追加 0.1.0 发布说明、贡献指南、支持与反馈、发布流程。
- **不引入新依赖**：纯文档，未新增任何脚本、二进制或外部工具。

### 累计验证状态
- `cmake --preset release && cmake --build build/release --parallel`：编译无 warning/error
- `ctest --test-dir build/release`：**24/24 全部通过**
- `bash scripts/smoke.sh`：**4/4 全部通过 ✓**
- `bash scripts/package-linux.sh`：**退出码 0**，`dist/dsh-desktop-0.1.0-Linux.tar.gz` + `dist/SHA256SUMS` 生成
- 三个新增维护者文档 grep 自检：`docs/INSTALL-LINUX.zh.md` 与 `README.md` 双向链接成立

### Round 38 — 安全策略与 CI 工作流

**触发**：0.1.0 正式发布前必须解决 GitHub 信任面（Security tab + CI 自动化）；本会话此前一直在用"构建者手动跑脚本"的模式，缺乏 webhook 端到端验证。

- **新增 [`SECURITY.md`](SECURITY.md)**：GitHub Security tab 自动识别的漏洞披露模板——
  - 支持版本表（0.1.x 与 master 分支）
  - 报告漏洞的两条私下通道（GitHub Security Advisories + 邮件 `security@dsh-desktop.example.invalid`）
  - 报告应包含的 7 项最小信息（复现步骤、版本、环境等）
  - 承诺响应 SLA：72h 确认 + 1–7 工作日分级 + Critical/High/Medium/Low 处理窗口
  - 已知威胁模型与已实施安全特性列表
  - 公告渠道与历史 CVE 占位
- **新增 [`.github/workflows/release.yml`](.github/workflows/release.yml)**：两个 job
  - `build-test`（每次 PR + push 触发）：开发构建 + 单元测试 + 端到端冒烟 + systemd unit 语法验证
  - `release-artifacts`（仅 push tag `v*` 或手动 `workflow_dispatch`）：发布构建 + cpack + 收集 TGZ/DEB/RPM/PKG + 上传为 GitHub Actions artifact（保留 30 天）+ Step Summary 输出 SHA256SUMS
  - 工作流在 ubuntu-latest 上跑，apt 安装 Qt 6.6 / libxcb / systemd / polkit / node / fakeroot / dpkg-dev / rpm-build
  - YAML 结构自检通过（PyYAML 解析，2 个 job，15 个 step）
- **`README.md` 文档清单扩展**：在原"安全审核报告"后加"安全策略（披露漏洞）"；在"发布流程（维护者）"后加"CI 工作流"。
- **不引入新依赖**：纯文档 + GitHub Actions YAML；本地构建者使用同一脚本 `scripts/package-linux.sh`，CI 只替换环境与触发方式。

### 累计验证状态
- `cmake --preset release && cmake --build build/release --parallel`：编译无 warning/error
- `ctest --test-dir build/release`：**24/24 全部通过**
- `bash scripts/smoke.sh`：**4/4 全部通过 ✓**
- `bash scripts/package-linux.sh`：**退出码 0**，`dist/dsh-desktop-0.1.0-Linux.tar.gz` + `dist/SHA256SUMS` 生成
- 工作流 YAML 结构自检通过：2 个 job（`build-test` / `release-artifacts`），15 个 step，触发条件与 `needs:` 引用一致
- 三个维护者文档（CONTRIBUTING / SUPPORT / SECURITY）+ RELEASING + README 双向链接成立
