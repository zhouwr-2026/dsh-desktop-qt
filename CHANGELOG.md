# 变更日志

DSH Desktop 所有重要变更都记录在此。版本遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

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
- 修复：`DshWindow` 设置 `QWebEngineSettings::ForceDarkMode`（Qt 6.11 支持）
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