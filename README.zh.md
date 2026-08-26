# DSH Desktop — Arch Linux / KDE Plasma 6 原生桌面端

> DeepSeek Harness (DSH) 在 Linux 上的 **C++/Qt6 原生**托盘包装器。基于官
> 网源码 <https://github.com/deepseek-ai/deepseek-harness> ，不 fork 任何上
> 游代码，只负责把官方 `dsh web` 进程包成一个 KDE6 风格的托盘 + 窗口应
> 用。

参考实现 <https://github.com/anywhere-labs/deepseek-harness-desktop>
只提供 Windows / macOS 安装包，且依赖 Electron。本项目用 **Qt6 + KDE 原
生接口**提供同等体验，且完全用 C++ 实现，零运行时 Python 依赖。

## 特性（对照需求 1–6）

| 需求 | 实现 |
| --- | --- |
| 1. 原生 Linux 栈 | C++17 + Qt6 6.5+ + KDE Plasma 6 的 `StatusNotifierItem` 协议 + `org.freedesktop.*` D-Bus；不引入 Electron / Tauri。 |
| 2. 常驻托盘 + 菜单 | `QSystemTrayIcon` + `QMenu`；菜单含 `显示桌面 / 隐藏桌面 / 检查更新 / 更新到最新版（仅在发现新版本时显示） / 查看日志 / 清空下载缓存 / 关于 / 重启 / 退出`。 |
| 3. 退出 / 重启原生对话框 + 后台服务勾选 | 独立的 `ExitDialog` 与 `RestartDialog` + `QCheckBox`；检测到活跃任务时显式高亮提示。后端运行中：勾选才调 `systemctl stop|restart dsh-web.service`（polkit 弹框）；后端未运行时勾选框锁定为"同时启动 DSH 后台服务"，未安装则弹原生确认后自动 `pkexec npm i -g @deepseek-ai/dsh`。 |
| 4. 黑/白鲸鱼 SVG 主题自适应 | 图标为**纯色**黑/白小鲸鱼 SVG（单一 path，fill 分别为 `#000000` / `#FFFFFF`），`assets/dsh-whale-{black,white}.svg` 为唯一 canonical 来源，经 `assets/icons.qrc` 内嵌到二进制，**全链路仅 SVG、无任何 PNG 位图变体**；`ThemeWatcher` 优先读 KDE 配置（`LookAndFeelPackage` / `ColorScheme`，含跨用户会话导出的主题标记），辅以 Qt `QStyleHints.colorScheme()`、`org.freedesktop.portal.Settings` 与 QPalette 兜底；托盘 / 窗口 / 任务栏 / 关于 对话框统一跟随；远程 xrdp 场景可用 `--theme dark` 强制暗色。 |
| 5. session-logs 下载 | `QWebEngineProfile::downloadRequested` 拦截 `/api/session.export?sessionId=...`；弹原生保存对话框；由 WebEngine 原生下载保留 Cookie、代理和证书状态，并显示 `QProgressDialog`；完成后弹原生提示和 KDE 通知。 |
| 6. 外部链接走系统浏览器 | 自定义 `LoopbackWebPage::acceptNavigationRequest` 只允许应用精确同源导航；其他 HTTP(S)、`mailto:` 与 `tel:` 交给 `QDesktopServices::openUrl`，并拒绝 `file:`、`javascript:` 等危险 scheme。 |

## 系统要求

* Arch Linux（或其他基于 Arch 的发行版）
* KDE Plasma 6（Breeze 主题；GNOME 也兼容但体验略逊）
* Qt6 ≥ 6.5（base + webengine + dbus + svg）—— `ForceDarkMode`（强制 Chromium 渲染暗色）需 Qt 6.7 起提供，项目以编译期 `QT_VERSION` 宏保护；在 Qt 6.5/6.6 上退化为不做强制反色（见 `applyForceDarkMode`）。
* 已安装的 `dsh` 包（≥ 0.1.0-rc.7）—— 通过 `npm i -g @deepseek-ai/dsh` 安装

## 安装

```sh
sudo packaging/install.sh
```

脚本会：

1. 用 pacman 装齐运行时依赖（包括 `qt6-base`、`qt6-webengine`、`qt6-svg`、`npm`、`polkit`）。
2. 通过 npm 安装 `@deepseek-ai/dsh`（若已装则跳过）。
3. 使用 `release` preset 在 `build/release/` 中构建并安装到 `/usr`。
4. 注册黑/白鲸鱼图标并刷新图标缓存。
5. 安装应用菜单、自启动项和主题导出服务。
6. 检测并复用已有的 `dsh-web.service`（只读校验）；对已存在但 inactive/failed
   的既有官方服务，启用/启动的决策交由桌面端在运行时先询问用户，而不是由
   安装器无条件强制拉起。

也可以打成 Arch 包：

```sh
# 在仓库根目录外构建
cp -r DSH-Desktop /tmp/dsh-desktop-0.1.0 && cd /tmp
tar czf dsh-desktop-0.1.0.tar.gz dsh-desktop-0.1.0
cd dsh-desktop-0.1.0 && makepkg -si
```

启动：

* 命令行：`dsh-desktop`
* KDE 启动器：搜索 "DSH Desktop"
* KDE 登录自启动：托盘会随 Plasma 会话自动出现（已写入 `/etc/xdg/autostart/`）

## CLI 模式

| 参数 | 用途 |
| --- | --- |
| `--smoke` | 仅探测后端可达性，立即退出（脚本友好，写到 stderr） |
| `--self-test` | 启动完整应用 + 输出 JSON 报告（`DSH_DESKTOP_SELF_TEST_BEGIN/END`） + 自动退出 |
| `--probe` | 环境探测：DISPLAY / D-Bus / KDE 托盘 watcher / 通知服务 / dsh-web 五项 |
| `--log-file <path>` | 把日志同时写入文件（启动时即生效） |
| `--url <url>` | 覆盖默认的 `http://127.0.0.1:3080` |
| `--theme <dark\|light>` | 强制托盘/窗口图标主题颜色（默认自动检测）。**远程 xrdp / VNC 场景下 KDE Plasma 跑在 root user、arch user 看不到 root 配置；必须用 `--theme dark` 显式选暗色才能拿到白色鲸鱼图标** |
| `-h, --help` / `-v, --version` | 标准 Qt CLI |

示例：

```sh
# 安装后第一次自检
dsh-desktop --probe

# 更新 DSH 或 profile 插件后的完整只读自检
dsh-profile-check

# 调试模式：把日志写到文件
dsh-desktop --log-file ~/.local/share/dsh-desktop/dsh-desktop.log

# 调试模式：连到远端 dsh-web
dsh-desktop --url http://my-server:3080

# 远程 xrdp / VNC：KDE Plasma 6 跑 Breeze Dark 主题，强制暗色图标
dsh-desktop --theme dark
```

## 增强功能（"取长补短"）

| 功能 | 说明 |
| --- | --- |
| 启动后 60s 自动后台检查更新 | 静默模式命中更新时只发 KDE 通知，不打断用户 |
| XDG 数据目录下载路径 | 会话日志默认存到 `~/.local/share/dsh-desktop/downloads/`，不污染 `~/Downloads/` |
| KDE 登录自启动 | `packaging/install.sh` 自动写入 `/etc/xdg/autostart/dsh-desktop.desktop` |
| 单实例锁 | `QLocalServer` + `QLocalSocket` 实现；二次启动唤醒原实例并立即退出 |
| 窗口几何持久化 | 窗口大小/位置/状态保存到 `~/.config/anywhere-labs/dsh-desktop.ini`，屏幕校验 |
| 启动失败诊断 | `which dsh` / `systemctl status` / `ss -ltn` 等可执行命令写到错误对话框详细文本 |
| 关于对话框 | 托盘"关于"菜单显示版本、作者、官方仓库、技术栈 |
| 日志查看器 | 托盘"查看日志"打开 Qt `AppDataLocation` 下的 `dsh-desktop.log` 末尾（智能截断 512 KB） |
| 后端健康监控 | 每 30 秒探测 dsh-web 状态；停止/恢复时通过 KDE 通知告知用户 |
| 托盘 Tooltip | 鼠标悬停托盘图标时仅显示应用名称 `DSH Desktop`，不暴露内部实现或服务地址 |
| 清空下载缓存 | 托盘"清空下载缓存"项；双层确认（先看文件数 + 二次弹 QMessageBox） |
| 统一后端 + 桌面更新 | 启动 60 秒后与托盘"检查更新"都在后台线程同时检查后端（npm 上的 `dsh`）与桌面（Gitee 发布）两个来源，经 `UpdatePlan::combine` 合并为唯一"更新到最新版"；`UpdateDialog` 同时展示两个组件、默认勾选全部可更新项（后端优先），用不定量进度条；桌面组件下载选中资产并校验 SHA-256 后交给 `dsh-desktop-updater`，原子替换运行中二进制且不停止后台服务 |
| SVG-only 图标 | `assets/dsh-whale-{black,white}.svg` 为唯一 canonical 来源；`install.sh` 仅安装 SVG（不再用 `rsvg-convert` 生成 PNG 位图）；卸载时顺带清理旧版遗留 PNG |
| systemd unit 自动检测（只读校验） | 对系统域与当前用户域各做一次只读 `systemctl show`，校验 `LoadState=loaded` 且 `ExecStart` 调用官方 `dsh web` 后才复用；两者都有效时优先当前用户的用户级 unit；找不到有效 unit 才回退 `dsh web` 子进程。已存在但 inactive/failed 的既有官方服务先询问用户再启动 |
| polkit 提权 | 升级时通过系统 `pkexec` 默认策略弹出管理员认证，不安装无效的自定义 action |

## 后端管理策略

桌面端按环境选择后端生命周期策略：对 loopback 地址先做一次**只读实况发现**
（对系统域与当前用户域各执行一次 `systemctl show`），仅当 `LoadState=loaded`
且 `ExecStart` 调用官方 `dsh web` 时才复用该 unit；两者都有效时优先当前用户的
用户级 unit；找不到有效 unit 时直接拉起并监管 `dsh web` 子进程；显式远程 URL
则完全不管理其生命周期：

* 启动时若 `http://127.0.0.1:3080` 不通，且已发现的是有效的既有官方 systemd
  服务，桌面端**不会无条件启动**：先弹原生确认框询问用户（inactive/failed 时
  均提示），用户点"是"才调用 `systemctl start`，点"否"则保持停止、继续打开
  桌面端。仍然失败则窗口依然打开但显示空白。
* 托盘 `重启` 用 `QProcess::startDetached` 重新拉起当前可执行文件并退出，
  是否动后端由原生 `RestartDialog` 的勾选决定（运行中勾选=重启后端；未运行
  无论勾选与否都先尝试拉起；未安装则弹原生确认后自动 `pkexec npm i -g
  @deepseek-ai/dsh`）。
* `退出` 对话框的 `同时停止后台 dsh web 服务` 勾选框勾选后才执行
  `systemctl stop dsh-web.service`（polkit 弹框）；后端未运行时勾选框锁定
  为"同时启动 DSH 后台服务"。
* 当 `dsh-web.service` 未安装或未通过校验时，桌面端自动回退到 `dsh web`
  子进程模式，对话框的勾选框变为 `同时停止/重启由桌面端拉起的 dsh web 子进程`。

### 已实现 vs 规划中的服务管理部分

**已实现（运行时代码）：**

* `Backend` 抽象 + 三种形态：`SystemdBackend` / `SupervisedBackend` / 外部远程模式。
* 只读服务发现：`ServiceDiscovery` + `SystemctlShowParser` 校验并选择 unit
  （`LoadState` + 官方入口验证、用户级优先）、`ServiceOwnership` 记录自建 unit
  归属、`applyServiceMetadata` 派生 scope/state/owner/failureReason。
* 对 inactive/failed 既有官方服务的**运行时就地授权**（`requiresStartConfirmation`
  → 原生确认框）。
* 退出 / 重启对话框里的勾选框统一语义（运行中=stop/restart，未运行=自动 start
  + 必要时自动 `npm i -g @deepseek-ai/dsh`）。
* 统一后端 + 桌面更新（`UpdatePlan` / `UpdateDialog` / `DesktopVersionChecker` /
  `DesktopReleaseDownloader` / `dsh-desktop-updater`）。

**仅模型/测试，尚未接入安装器或 UI（规划中）：**

* `InstallationPlan`（检测—复用—补齐 的确定性决策模型）与其单元测试 —— 决策
  模型已就绪，尚未驱动安装器或运行 UI。
* `ServiceUnitBuilder`（生成标准 `dsh-web.service` 单元文本）与其单元测试 ——
  尚未用于真实补齐。
* 安装阶段真正"补齐"一个新的 `dsh-web.service`（由桌面端创建并记录归属）——
  未实现；`install.sh` 目前仍是基础检测 + `enable --now`，不创建新 unit。
* 更完整的统一服务管理界面（服务日志、配置摘要、安装期授权流程、卸载时"同时
  移除后台服务"复选框）。

完整目标设计见 [docs/DSH-DESKTOP-SERVICE-PLAN.zh.md](docs/DSH-DESKTOP-SERVICE-PLAN.zh.md)。

## 构建与运行

```sh
# 日常开发：RelWithDebInfo + 完整测试
cmake --preset dev
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
build/dev/dsh-desktop --smoke

# 发布/部署：Release + /usr 安装前缀
cmake --preset release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

共享 preset 固定只生成 `build/dev/` 和 `build/release/`；个人覆盖写入不提交的
`CMakeUserPresets.json`，不要再创建 `cmake-build-*` 等临时构建树。

图形模式默认拒绝 root 运行，以避免关闭 Chromium 沙箱。仅在已隔离并明确承担风险的环境中设置 `DSH_DESKTOP_ALLOW_ROOT=1`。

## 调试

```sh
DSH_DESKTOP_DEBUG=1 dsh-desktop   # 暂未启用，预留位
# 日志默认写入 XDG AppDataLocation，可用 --log-file 覆盖
env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen \
  build/dev/dsh-desktop --self-test  # 离屏自检
```

更新 `@deepseek-ai/dsh` 或第三方 profile 插件后，建议立即运行
`dsh-profile-check`（源码目录内可运行 `scripts/check-dsh-profile.sh`）。它会检查
profile 依赖声明的服务端/客户端入口、
JavaScript 语法、Web 首页和核心会话 bundle，且不会修改配置、插件或服务。
检查器优先从已发现的 `dsh-web.service` 读取实际 `DSH_HOME`、host 与 port；
也可通过 `DSH_HOME`、`DSH_WEB_URL` 显式覆盖。
若报告构建产物缺失，先运行 `dsh plugin --profile web install`；只有命令明确被
pnpm 的新包发布年龄策略拦截、且已确认包来源可信时，才临时追加
`--config.minimumReleaseAge=0`，不要永久关闭该保护。

## 与参考实现的差异（取长补短）

* 不用 Electron — 体积更小、与 Plasma 6 的 StatusNotifier 协议原生对齐；
  渲染走 Chromium 但经由系统 QtWebEngine，无需自带一份 Chromium。
* 托盘主题切换实时生效。
* session-logs 拦截使用 Qt 信号 `downloadRequested`，比参考实现的
  `webContents.session.on('will-download')` 更原生。
* 外部链接拦截走 `acceptNavigationRequest` 虚函数，能在导航发生前阻断；
  包括 iframe 与 `target=_blank`。
* 全 C++ 实现，零 Python 运行时依赖，启动更快、内存占用更低。
* 单实例锁用本地 socket 而非 fcntl，跨平台更友好。
* 升级通过 npm + pkexec（参考实现用 Cordis 插件机制；本项目按 Linux
  原生习惯走包管理器+polkit）。

## 卸载

```sh
sudo packaging/install.sh --uninstall
```

卸载脚本会移除系统级可执行文件、桌面/自启动条目、图标、polkit 策略和主题
导出服务；用户配置、日志、WebEngine 数据和下载文件默认保留。

如果是 `makepkg -si` 安装的 Arch 包，`sudo pacman -Rns dsh-desktop` 一步搞定。

## 项目结构

```
DSH-Desktop/
├── CMakeLists.txt
├── CMakePresets.json              # dev / release 共享构建入口
├── build/                         # Git 忽略的生成目录
│   ├── dev/                       # RelWithDebInfo + 测试
│   └── release/                   # Release + 部署/打包
├── src/                         # C++ 源码
│   ├── main.cpp                 # 入口
│   ├── app/                     # DshDesktopApp / DshWindow / TrayController / Dialogs
│   ├── backend/                 # Backend 抽象 + SystemdBackend + SupervisedBackend
│   ├── service/                 # 只读服务发现 / 所有权 / 安装决策模型
│   ├── platform/                # RenderingPolicy（X11/xrdp 软件渲染）
│   ├── icon/                    # IconLoader
│   ├── theme/                   # ThemeWatcher
│   ├── updater/                 # Updater + UpdatePlan + 桌面自更新助手
│   ├── util/                    # Logger / Notify (D-Bus)
│   └── web/                     # LoopbackWebPage + DownloadInterceptor
├── assets/                      # 黑白鲸鱼 SVG（嵌入到二进制）
│   └── icons.qrc
├── tests/                       # Qt Test
├── packaging/                   # PKGBUILD + .desktop + install.sh
├── scripts/                    # 开发与端到端验证脚本
│   ├── check-dsh-profile.sh     # DSH profile 只读完整性检查
│   └── smoke.sh                 # 端到端冒烟
├── docs/                       # 设计方案与开发文档
│   └── DSH-DESKTOP-SERVICE-PLAN.zh.md
├── README.md
└── README.zh.md
```

## 许可

MIT
