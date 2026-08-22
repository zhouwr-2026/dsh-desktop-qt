# 会话总结 —— DSH Desktop 开发：Arch Linux KDE6 原生桌面端（logo 主题统一与动态切换修复）

> **会话命名（建议）**：`DSH-Desktop-KDE6 原生桌面端 / logo 主题统一与动态切换修复`
> **创建日期**：2026-08-21
> **目标工作区**：`/home/zhouwr/Project/CodeWorkspace/DSH-Desktop`
> **作者**：zhouwr（用户）+ 开发 agent

---

## 1. 项目概述

为官方 DeepSeek Harness（`deepseek-ai/deepseek-harness`）在 **Arch Linux + KDE Plasma 6** 上构建**原生桌面端包装器**，替代仅 Windows/macOS 的参考实现（`anywhere-labs/deepseek-harness-desktop`，Electron）。

| 项 | 值 |
| --- | --- |
| 技术栈 | C++17 + Qt 6.11（Core/Gui/Widgets/Network/DBus/Svg/WebEngine*）+ CMake/Ninja |
| 渲染 | `QWebEngineView` 内嵌官方 DSH Web UI（`http://127.0.0.1:3080`） |
| 后端 | systemd `dsh-web.service` 优先，无则 `SupervisedBackend` 子进程兜底 |
| 打包 | `packaging/install.sh` + `PKGBUILD` |
| 测试 | QtTest 3 套件 + `scripts/smoke.sh` 4 阶段 |

## 2. 核心需求（用户原话要点）

1. 原生 Linux 技术栈，尽量接近原生（选定 C++/Qt6；用户曾要求"不使用 Python"全面重写）。
2. 常驻托盘 + 菜单：显示桌面 / 隐藏桌面 / 检查更新 / 更新到最新版（仅新版本时显示）/ 重启桌面 / 退出。
3. 托盘退出 → 原生对话框（确认/取消 + 后台任务高亮 + "退出后台服务"复选框）。
4. Logo 用官方黑鲸鱼 SVG，派生白鲸鱼；**按系统明暗主题切换：暗色→白鲸鱼、亮色→黑鲸鱼；托盘/窗口/任务栏/菜单/About 全部统一**。
5. "session logs" 按钮在桌面端 → 弹原生保存路径 + 进度条 + 完成通知。
6. 任务会话内 http 链接 → 默认浏览器打开（`acceptNavigationRequest` 拦截）。
7. 取长补短、有疑问要挨个问直到 95% 理解。

## 3. 最终交付物清单（Round 1-18）

```
DSH-Desktop/
├── CMakeLists.txt                  # Qt6::Svg；.qrc 加入 add_executable + AUTORCC
├── src/
│   ├── main.cpp                    # qInitResources_icons() 前置；--theme dark 参数
│   ├── app/    DshDesktopApp / DshWindow / TrayController / AboutDialog /
│   │           LogViewer / ExitDialog / UpdateDialog
│   ├── backend/ Backend / SystemdBackend / SupervisedBackend
│   ├── icon/   IconLoader（QSvgRenderer 渲染黑白 2 SVG）
│   ├── theme/  ThemeWatcher（4 秒轮询 + schemeChanged）
│   ├── updater/ Updater（npm registry + pkexec）
│   ├── util/   Logger / Notify（D-Bus）
│   └── web/    LoopbackWebPage / DownloadInterceptor / DownloadWorker
├── assets/
│   ├── dsh-whale-black.svg         # 唯一 logo 源（fill #000000）
│   ├── dsh-whale-white.svg         # 唯一 logo 源（fill #FFFFFF）
│   └── icons.qrc                   # 只含这 2 个 SVG
├── packaging/  install.sh / PKGBUILD / dsh-desktop.desktop（Icon=dsh-whale-black 静态）
├── scripts/smoke.sh
├── tests/     test_semver / test_loopback_page / session_export_test
├── legacy_py/                      # 早期 Python 原型（保留参考，不参与构建）
├── README.zh.md / README.md / CHANGELOG.md
```

## 4. 环境特殊性（所有坑的根源）

- **xrdp 远程会话**：KDE Plasma 6 跑在 **root user (UID 0)**，`dsh-desktop` 通常以 **arch user (UID 1000)** 启动。
- arch 进程**无法读 `/root/.config`**（权限 700）、**无法连 root D-Bus**（`/run/user/0/bus` Permission denied）。
- 已实测 Qt 6.11 原生主题 API 在此环境**全部失效**：
  - `QStyleHints::colorScheme()` → 恒 `Unknown`
  - `QPalette` Window → 恒亮色 `rgb(239,239,239)`（即便 KDE 是 Breeze Dark）
  - `org.freedesktop.portal.Settings` color-scheme → 0
  - **唯一可靠来源 = KDE 配置文件**。

## 5. 主题探测关键坑（ThemeWatcher.cpp）

KDE 6 `~/.config/kdeglobals` 用 `QSettings::NativeFormat` 写（Unix 即 ini，但 key 位置特殊）：

```
[KDE]
LookAndFeelPackage=org.kde.breezedark.desktop
```

- **`LookAndFeelPackage` 在 `[KDE]` 组**（不在顶层、不在 `[General]`）。
- `ColorScheme` 可能在顶层或 `[General]`；键名大小写不定（`ColorScheme`/`colorscheme`）。
- `plasmarc` 可能不存在。
- **正确策略**：`kdeglobals`+`plasmarc` × `NativeFormat`+`IniFormat`，查 `KDE/LookAndFeelPackage`、`LookAndFeelPackage`、`General/LookAndFeelPackage`、`ColorScheme`、`General/ColorScheme`、`KDE/ColorScheme` 六个路径，含 "dark" 即暗色；arch 用户额外尝试 `/root/.config`（try）。
- `ThemeWatcher::start()` 必须在 current 变化时 **emit schemeChanged**（否则窗口在 start() 前创建、图标永不刷新）。

## 6. 历史 bug 清单（逐一排查是否残留）

1. **AboutDialog 曾写死 `pixmapForScheme("light")`** → About 永远黑鲸鱼。已改：构造函数接收 scheme，`onShowAbout()` 传 `theme_->current()`。
2. **`ThemeWatcher::start()` 曾不 emit** → 图标永不刷新。已修。
3. **`QIcon::setThemeName` 默认 hicolor（空主题）** → `QIcon::fromTheme` 全 null。必须显式设 `breeze`（亮）/`breeze-dark`（暗）+ `setFallbackThemeName("hicolor")`。
4. **`qt6_add_resources` 的 QRC 参数在 Qt 6.11 静默失效** → 必须把 `.qrc` 加进 `add_executable()` 源列表 + `AUTORCC ON`；**且 main() 必须 `qInitResources_icons()`**（rcc 静态初始化在匿名 namespace，不显式引用会被链接器 strip）。重建后务必 `strings 二进制 | grep -c dsh-whale` 验证（应=2）。
5. **单实例锁**（QLocalServer，`/run/user/<uid>/dsh-desktop.sock`）：旧实例（尤其 root 起的）阻止新实例启动与测试。测试前 `pkill -9 -f dsh-desktop` + `rm -f /run/user/*/dsh-desktop.sock`。
6. **cmake build 偶发 SIGKILL**：重跑或 `--parallel 1`。
7. **`.desktop` 文件 `Icon=dsh-whale-black` 静态**（KDE 启动器/固定图标用）；窗口打开后任务栏分组改用窗口 `_NET_WM_ICON`（动态）。静态项暂无法动态，属已知限制。

## 7. 统一 logo 入口（Round 18 已落地，务必保持）

```cpp
// DshDesktopApp
void DshDesktopApp::onThemeChanged(const QString& scheme) { applyLogoTheme(scheme); }

void DshDesktopApp::applyLogoTheme(const QString& scheme) {
    QIcon::setThemeName(scheme == "dark" ? "breeze-dark" : "breeze");
    QIcon::setFallbackThemeName("hicolor");
    if (tray_)   tray_->applyTheme(scheme);      // 托盘鲸鱼
    if (tray_)   tray_->setMenuIcons();          // 菜单语义图标
    if (window_) window_->setWindowIcon(dsh::icon::iconForScheme(scheme)); // 窗口
    if (qtApp_)  qtApp_->setWindowIcon(dsh::icon::iconForScheme(scheme));  // 任务栏兜底
}
```

所有主题变化必须走此入口；新增 logo 表面（如 About）也应在此统一刷新。

## 8. 验证方法（改完必须实测，不能只看代码）

### 8.1 构建 + 单元测试
```bash
cd /home/zhouwr/Project/CodeWorkspace/DSH-Desktop
rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build          # 期望 3/3
strings build/dsh-desktop | grep -c dsh-whale   # 期望 2
```

### 8.2 启动 + 窗口图标颜色（xprop 分析 `_NET_WM_ICON`）
```bash
sudo pkill -9 -f dsh-desktop; sleep 1
rm -f /home/zhouwr/.config/anywhere-labs/dsh-desktop.ini /run/user/*/dsh-desktop.sock
kwriteconfig6 --file kdeglobals --group General --key ColorScheme BreezeClassicDark
DISPLAY=:10 XAUTHORITY=/home/zhouwr/.Xauthority /usr/bin/dsh-desktop \
  --log-file /home/zhouwr/.local/share/dsh-desktop/dsh-desktop.log > /tmp/d.log 2>&1 &
sleep 6
WID=$(DISPLAY=:10 XAUTHORITY=/home/zhouwr/.Xauthority xdotool search --name "DSH Desktop" | tail -1)
DISPLAY=:10 XAUTHORITY=/home/zhouwr/.Xauthority python3 -c "
import subprocess, re
r=subprocess.run(['xprop','-id','$WID','-notype','32c','_NET_WM_ICON'],
  env={'DISPLAY':':10','XAUTHORITY':'/home/zhouwr/.Xauthority'},capture_output=True,text=True)
nums=[int(n) for n in re.findall(r'\d+',r.stdout)]; w,h=nums[0],nums[1]
px=nums[2:2+w*h]
n_a=sum(1 for p in px if (p>>24)&0xff>0)
n_b=sum(1 for p in px if (p>>24)&0xff>0 and ((p>>16)&0xff)>200)
n_d=sum(1 for p in px if (p>>24)&0xff>0 and ((p>>16)&0xff)<50)
ok='白鲸鱼✓' if n_b>n_a//2 and n_d==0 else '黑鲸鱼' if n_d>n_a//2 and n_b==0 else '混合✗'
print(f'{w}x{h} 覆盖={n_a} 亮={n_b} 暗={n_d} -> {ok}')"
```
暗色主题期望：`白鲸鱼✓`（约 362 像素全白）。

### 8.3 动态切换验证（运行中切主题，等 ≥8 秒）
```bash
kwriteconfig6 --file kdeglobals --group General --key ColorScheme BreezeClassicLight; sleep 8
# 重复 8.2 → 期望 黑鲸鱼✓
kwriteconfig6 --file kdeglobals --group General --key ColorScheme BreezeClassicDark; sleep 8
# → 期望 白鲸鱼✓
```

### 8.4 About 对话框
独立测试程序构造 `AboutDialog("dark")` 与 `AboutDialog("light")`，遍历 `findChildren<QLabel*>()` 带 pixmap 的，分析黑白像素：dark→白鲸鱼、light→黑鲸鱼（约 873 像素）。

### 8.5 托盘菜单图标非 null
`QIcon::setThemeName("breeze")` 后 `QIcon::fromTheme("go-home")` 等 9 个语义图标非 null；`TrayController::menuActions()` 每个 QAction icon 非 null。

### 8.6 托盘图标
KDE SNI 托盘无法直接 xprop；可用 D-Bus 查 `org.kde.StatusNotifierWatcher` 注册项，或截屏确认。至少确认 `TrayController::applyTheme` 被 `applyLogoTheme` 调用。

## 9. 最终验收清单

- [ ] 所有 logo 表面（托盘/窗口/任务栏/About/菜单）走**同一个** `applyLogoTheme(scheme)` 入口
- [ ] 暗色主题：托盘/窗口/任务栏/About 全部白鲸鱼；亮色：全部黑鲸鱼
- [ ] 运行中切主题，8 秒内全部表面同步切换
- [ ] About 对话框打开时跟随当前主题
- [ ] 托盘菜单 9 个语义图标非 null
- [ ] `strings 二进制`：2 个 SVG、0 个 PNG 残留、0 个彩虹色
- [ ] 3/3 单元测试通过，`scripts/smoke.sh` 全过
- [ ] 每项都有实测输出证据（贴 xprop 分析结果）

## 10. 注意事项

- 当前机器默认主题配置为 `BreezeClassicDark`（已写回）；测试结束请恢复。
- `dsh-desktop` 通常以 arch user 启动；root 启动需带
  `XDG_RUNTIME_DIR=/run/user/0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus`。
- **不要删 `--theme dark` 参数**（xrdp 场景兜底），但默认应自动探测。
- 所有新代码保持中文注释 + `@author zhouwr`。
- 旧 Python 原型在 `legacy_py/`（不参与构建，仅参考）。

## 11. 给后续 agent（codex 等）的一句话交接

> 核心未竟任务：**确保所有 logo 表面严格统一并随系统明暗主题动态切换**。
> 已落地统一入口 `applyLogoTheme` + 修复 About 写死 + `[KDE]` 组读取 + 资源嵌入。
> 请按第 8 节实测方法逐项验证，把 `_NET_WM_ICON`/About/菜单的像素分析结果作为证据，
> 满足第 9 节验收清单后才算根治。
