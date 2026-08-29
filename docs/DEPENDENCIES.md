# 依赖清单

DSH Desktop 的运行时与构建期依赖，以及它们在 4 个声明文件中的来源、版本下限与升级策略。

> 维护说明：当引入新的第三方依赖时，请同步更新本表对应行 + `CMakeLists.txt` + `packaging/PKGBUILD` + `packaging/install.sh`，避免"声明缺失"回归。
> （触发：依赖审查建议 #5 —— "在 docs/DEPENDENCIES.md 维护一份人读依赖清单"）

## 三层依赖模型

### 1. C++/Qt 主程序（`dsh_desktop_core` 静态库 + 3 个可执行）

| 名称 | 最低版本 | 来源 | 用途 |
| --- | --- | --- | --- |
| **Qt 6 — Core / Gui / Widgets / Network / DBus / Svg / WebEngineCore / WebEngineWidgets** | 6.6 LTS | `find_package(Qt6 6.6 REQUIRED COMPONENTS ...)`（`CMakeLists.txt:63`）；Arch 包 `qt6-base` + `qt6-webengine` | Qt 主框架：UI、对象系统、网络、DBus、Svg、WebEngine（Chromium 内核渲染官方 DSH Web UI） |
| **libxcb** | 1.17 | `pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb>=1.17)`（`CMakeLists.txt:78`）；Arch 包 `libxcb` | X11 C-binding，用于 `RenderingPolicy` 探测 GLX/DRI3 是否可用 |
| **PkgConfig** | — | `find_package(PkgConfig REQUIRED)`（`CMakeLists.txt:74`） | 构建期工具，解析 `.pc` 文件 |
| **Ninja** | — | `CMakePresets.json` generator；Arch 包 `ninja`（PKGBUILD makedepends） | 构建生成器 |
| **CMake** | 3.19 | `cmake_minimum_required(VERSION 3.19)`（`CMakeLists.txt:18`） | 构建系统 |

#### 1a. 叶子工具（`util/` 子目录，与 `dsh_desktop_core` 集成）

| 工具 | 用途 | 备注 |
| --- | --- | --- |
| `util::Logger` | 线程安全的日志通道 | 原有 |
| `util::Notify` | 经 Qt6::DBus 调用 `org.freedesktop.Notifications` | 原有 |
| `util::computeFileSha256` | 流式 1 MiB chunk 文件 SHA-256（小写十六进制） | 第 2 轮下沉；同时编入 `dsh_desktop_core` 与 `dsh_desktop_updater_helper` |
| `util::httpProbe` | curl HTTP 健康探测（200-499 可达，5xx/超时不可达） | 第 3 轮下沉；统一采用 SystemdBackend 原版的 `exitStatus != NormalExit` 硬化 |
| `util::syncHttpGet` | 同步 HTTP GET（QNetworkAccessManager + QEventLoop + 超时） | 第 4 轮下沉；用于版本检查器 |
| `util::runSyncProcess` | 同步 QProcess 包装（启动 + 完成 + 超时 kill + stdout/stderr 分离） | 第 9 轮下沉；用于 systemctl/journalctl 等 |

### 2. 自更新助手核心（`dsh_desktop_updater_helper` 静态库，仅 Qt Core）

| 名称 | 最低版本 | 来源 | 用途 |
| --- | --- | --- | --- |
| **Qt 6 — Core** | 6.6 | `target_link_libraries(... PUBLIC Qt6::Core)`（`CMakeLists.txt:173`） | 文件哈希、原子替换、JSON 解析（最小化依赖，让 CLI 与测试都只带 Qt Core） |
| **util/Sha256** | — | 同时编入 `dsh_desktop_core` 与 `dsh_desktop_updater_helper`（同一份 .cpp 在两个静态库各编一次，避免反向依赖；保证 SHA-256 单一来源） | 流式 SHA-256 计算 |

### 3. 运行时外部服务与脚本（PKGBUILD / install.sh）

| 名称 | 最低版本 | 来源 | 用途 |
| --- | --- | --- | --- |
| **knotifications** | 6.6 | `packaging/PKGBUILD` depends=`'knotifications>=6.6'`；Arch 包 `knotifications` | KDE Frameworks 6 通知（`dsh::util::notify` 经 Qt6::DBus 调用 `org.freedesktop.Notifications`） |
| **polkit** | 124 | `packaging/PKGBUILD` depends=`'polkit>=124'` | `pkexec` 提权：服务启停、`npm i -g @deepseek-ai/dsh` 自动安装 |
| **procps-ng** | — | `packaging/PKGBUILD` depends=`'procps-ng'` | 提供 `pgrep` 等进程工具 |
| **npm** | — | `packaging/PKGBUILD` depends=`'npm'` | 安装/升级 `@deepseek-ai/dsh` |
| **@deepseek-ai/dsh** | 0.1.0-rc.7 | `npm i -g @deepseek-ai/dsh`（`packaging/install.sh`） | 后端 Web 服务器 `dsh web`（loopback 嵌入）；`Updater` 启动期校验下限（`dsh::updater::kMinimumDshVersion`） |
| **DSH_BIN**（环境变量，可选） | — | 仅供开发/CI 覆盖 `dsh` 路径；生产构建请勿设置（`README.zh.md`） | supervised 模式下指定 `dsh` 二进制路径；`QFile::exists` 校验 |

### 4. systemd 单元

| 名称 | 文件 | 说明 |
| --- | --- | --- |
| `dsh-theme-export.service` | `packaging/dsh-theme-export.service` | oneshot service；以 root 身份读 `/root/.config/kdeglobals`，写 `/run/dsh-desktop/theme`（world-readable）。已加固：`NoNewPrivileges=yes` / `ProtectSystem=strict` / `PrivateTmp=yes` / `ProtectHome=yes` + 显式 `ReadOnlyPaths=/root/.config` + `ReadWritePaths=/run/dsh-desktop`。 |
| `dsh-theme-export.path` | `packaging/dsh-theme-export.path` | 监视 `/root/.config/kdeglobals` 变化，触发 service 执行。 |

## 不再使用的依赖（清理历史）

| 名称 | 删除原因 | 删除轮次 |
| --- | --- | --- |
| `extra-cmake-modules` | CMake 中无 `find_package(ECM)` / `include(ECM*)` 调用，纯赘依赖 | 第 1 轮 |

## 升级策略

- **Qt 最低要求跟随 LTS**：当前 6.6 LTS；下一个 LTS（6.8 / 6.10）发布且发行版主流对齐后，抬升 `find_package(Qt6 ... REQUIRED COMPONENTS ...)` 与 PKGBUILD 的 `qt6-base>=...` / `qt6-webengine>=...` / `qt6-svg>=...`。
- **Qt WebEngine（= Chromium 内核）**：跟随发行版 `qt6-webengine` 包的安全 backport；本项目不直接链接 Chromium / BoringSSL。
- **libxcb ≥ 1.17**：2023-2024 CVE 修复合入的版本；Arch 仓库 `libxcb` 包版本应远高于此。
- **knotifications ≥ 6.6**：跟随 KDE Frameworks 6；Arch `knotifications` 与 `kde-frameworks-6` 同步升级。
- **polkit ≥ 124**：CVE-2021-4034（pkexec 本地提权）已在 0.120 系列修复；Arch 当前版本远高于此。
- **@deepseek-ai/dsh ≥ 0.1.0-rc.7**：启动期硬校验（`dsh::updater::checkMinimumDshVersion`），低于时拒绝启动并提示 `sudo npm i -g @deepseek-ai/dsh@latest`。

## 测试依赖

| 名称 | 用途 | 文件 |
| --- | --- | --- |
| Qt6::Test | 单元测试框架（QTest、QSignalSpy） | `tests/CMakeLists.txt:7` |
| `bash` | `test_profile_checker.sh` mock systemctl/curl | `tests/test_profile_checker.sh` |
| `node` | `check-dsh-profile.sh` 解析 systemd `Environment=` 字段 | `scripts/check-dsh-profile.sh:13` |
| `curl` | `check-dsh-profile.sh` 后端可达性探测 | `scripts/check-dsh-profile.sh:46` |
| `systemctl`（可选） | `check-dsh-profile.sh` 只读探测 dsh-web.service | `scripts/check-dsh-profile.sh:49` |

## 引入新依赖时的检查清单

1. 在 `CMakeLists.txt` 的 `find_package` 或 `pkg_check_modules` 加声明 + 版本下限。
2. 在 `packaging/PKGBUILD` 的 `depends=` 或 `makedepends=` 加包名 + 版本下限。
3. 在 `packaging/install.sh` 的 `ensure_pkgs()` 检查列表加包名。
4. 在本表对应行更新版本/来源/用途。
5. 跑 `cmake --preset dev && cmake --build build/dev --parallel && ctest --test-dir build/dev`，确认现有 23 个测试仍通过。
