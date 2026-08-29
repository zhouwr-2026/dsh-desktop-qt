# DSH-Desktop 安全静态审查报告（只读）

- 审查范围：`/home/zhouwr/Project/CodeWorkspace/DSH-Desktop`（除 `build/`、`docs/` 中仅文档外）全部 `src/`、`scripts/`、`packaging/`、`tests/`、根级元数据文件（`CMakeLists.txt`、`README*.md`、`CHANGELOG.md`）。
- 模式：只读，不修改、不构建、不联网；用 `grep` + `read` 上下文确认；所有发现给 `文件:行号` + 可控输入路径 + 风险等级 + 建议。
- 重点复核范围（用户指定）：`RunSyncProcess`、`SystemctlCommandBuilder`、`ShowFailure`、`SyncHttp`、`SupervisedBackend` 的 `DSH_BIN` 与子进程参数；`dsh-theme-export.service` 沙箱硬化；`tests/test_profile_checker.sh` fixture 修复；以及本次未暂存 +30 文件的回归面。
- 已声明覆盖的类别（即使未发现高危项也列出，避免盲区）：
  - 注入（shell / SQL / systemd / 配置 / JSON / D-Bus）
  - 路径穿越 / 符号链接 / TOCTOU
  - 凭据泄漏 / 令牌 / 密钥 / 日志写敏感数据
  - QProcess 派生 + 提权（sudo / pkexec / systemctl / polkit）
  - systemd unit 注入（unit 名 / ExecStart / Environment）
  - URL / SSRF（同步 HTTP、桌面更新源、桌面下载资产、WebEngine）
  - 脚本危险命令（`rm -rf`、`eval`、`popen`、`<()`、here-doc）
  - C++ 内存安全（UAF / 双重释放 / 空指针 / 整数溢出 / 字符串截断 / 类型转换）

---

## 0. 总览与结论

| 项 | 数量 | 说明 |
| --- | --- | --- |
| 严重（Critical） | 0 | — |
| 高（High） | 1 | `SupervisedBackend::resolveDshBin` 未对 `DSH_BIN` 校验可信 owner/权限（见 §3） |
| 中（Medium） | 5 | 详见 §3/§5/§6/§7/§11 |
| 低（Low / 建议） | 7 | 详见 §4/§5/§6/§7/§8/§11 |
| 已硬化、无问题 | 多 | `RunSyncProcess`、`SystemctlCommandBuilder`、`ShowFailure`、`SyncHttp`、`ServiceUnitBuilder`、`DesktopUpdateHelper::atomicReplace`、`dsh-theme-export.service`、profile-checker fixture 等 |

整体设计方向良好（始终 `QProcess::setProgram/setArguments` 显式 argv，不经 shell；service unit 字符串化全部 `escapeInsideQuotes` 并白名单校验；用户输入 `--theme` 强约束；提权必经 `pkexec --disable-internal-agent` 或 `sudo -n NOPASSWD`；root+WebEngine 默认拒绝）。下面按主题列证据。

---

## 1. 注入面（Shell / systemd / JSON / D-Bus）

### 1.1 覆盖声明

| 类别 | 状态 | 关键证据 |
| --- | --- | --- |
| Shell 命令拼接 | 无 | 全仓仅 `LogViewer.cpp:96`、`DshWindow.cpp:41`、`DshDesktopApp.cpp:514/849` 使用 `QProcess::startDetached` 显式 argv；install.sh 用 `"$@"`；脚本无 `eval`/`popen`/`bash -c "$user"`。 |
| systemd unit 字符串 | 无 | `ServiceUnitBuilder.cpp` 严格 `escapeInsideQuotes` + 拒绝 `\n`/`\r`/`\0`，所有 `token()` 都按 `expandDollar` 处理。 |
| systemd unit 名 | 无 | `SystemctlCommandBuilder::isValidUnitName`（`SystemctlCommandBuilder.cpp:92-127`）白名单字符 + 后缀白名单 + 拒绝 `/`、`.` 开头、空白、控制字符、`"`/`'`/`\\`/`` ` ``/`;`/`&`/`|`/`$`/`(`/`)`。 |
| D-Bus 调用 | 低风险 | `util/Notify.cpp:46-72` 走标准 `org.freedesktop.Notifications.Notify`，所有参数类型显式 `uint`/`quint8`，无字符串拼接。`ThemeWatcher.cpp:163-178` 仅 `Read` 一个 read-only 属性。 |
| JSON 解析 | 无 | `Updater.cpp`/`ServiceOwnership.cpp`/`Updater.cpp:171`/`Updater.cpp:166` 全部走 `QJsonDocument::fromJson` + 错误判定，无手动拼字符串注入。 |
| 配置文件 | 无 | `Logger.cpp:42-46` 写日志用 `QTextStream`，`Updater::readLocalVersion` 读取固定候选路径（`/usr/lib/node_modules/...`），无任意写。 |
| npm 安装参数 | 中（见 §6） | `Updater::performUpdate`（`Updater.cpp:192-252`）版本号先经 `parseSemVer` 严格校验（`Updater.cpp:65-79`）后才拼 `@deepseek-ai/dsh@<version>`，可信。 |

### 1.2 复核用户指定文件

- `RunSyncProcess.cpp`：API 强制 `program + QStringList args`，内部仅 `QProcess::start(program, args)`，**无 shell**，超时显式 `kill()` + `waitForFinished` 收尸，已替代旧版 `is-active` 进程泄漏 bug。✅
- `SystemctlCommandBuilder.cpp`：纯字符串构造函数，`operationNeedsElevation` 与 `resolveCommand` 不持有进程；`isValidUnitName` 通过单测 `test_service_manager.cpp:288-307` 覆盖 `path/inject/command substitution` 等负向用例。✅
- `ShowFailure.cpp`：纯字符串分类，按 `contains` 子串匹配 `could not be found`/`bus`/`connect`/`No such file or directory`，**没有**执行任何外部命令。`stderrText` 来自 `systemctl show` 的 stderr（系统域半可信，恶意用户域服务可注入）— 但下游只用作分类与展示（`ServiceDiscovery.cpp:58`、`DshServiceManager.cpp:534`），不拼接命令也不直接信任为可执行文件名。✅
- `SyncHttp.cpp`：URL 仅来自调用方传入（`Updater.cpp:164` 写死常量；`DesktopVersionChecker.cpp:134` 同），无 SSRF 注入面（见 §5）。✅

---

## 2. 路径穿越 / 符号链接 / TOCTOU

### 2.1 覆盖声明

| 类别 | 状态 | 证据 |
| --- | --- | --- |
| 写文件目标校验 | 已硬化 | `DesktopUpdateHelper::atomicReplace` (`DesktopUpdateHelper.cpp:363-458`)：源用 `validateSource`、目标用 `validateInstallDestination`（必须等于已安装二进制或位于 `isPathWithinPrefix`），并使用 `QSaveFile`/原子 rename + `fsync`。 |
| 缓存路径校验 | 已硬化 | `DesktopReleaseDownloader::safeCachePath` (`DesktopReleaseDownloader.cpp:267-297`)：`QFileInfo::absoluteFilePath` 后必须 `startsWith(absRoot + '/')` 且 `fileName == fileName` 单层。 |
| 文件名净化 | 已硬化 | `sanitizeFileName` (`DesktopReleaseDownloader.cpp:154-191`)：拒 NUL、`/`、`\`、首尾空白、`..`/`cleanPath != assetName`、目录名。 |
| `mktemp` 原子写 | 已硬化 | `packaging/dsh-theme-export:37-41`：用 `mktemp "$output_directory/.theme.XXXXXX"` + `trap rm -f` + 原子 `mv -f`，避免半写。✅ |
| 软链接解析 | 部分 | 见 §2.3 |
| TOCTOU（stat → open） | 中（见 §2.4） |

### 2.2 卸载器删除自身保护（已硬化）

`Uninstaller.cpp:42-55`：先用 `QFileInfo(selfExe).absoluteFilePath()` 解析绝对路径，再对每个待删除项 `QFileInfo(path).absoluteFilePath() == selfExe` 才跳过 — 但**没有**额外校验目标是否被指向 `selfExe` 的硬链接 / `bind mount` 覆盖；如果攻击者在卸载器同一目录放置 `dsh-desktop` 的硬链接/绑定挂载，路径比较仍然相等但实际删除的是另一个 inode。建议增加 `info.isSymLink() == false` + 比较 `info.canonicalFilePath()`（解析所有硬链接/挂载）。**等级：低**，影响：本地攻击者对抗自己可控的卸载器，使用面有限。

### 2.3 软链接

- `Uninstaller::removePath` (`Uninstaller.cpp:107-133`)：明确 `if (!info.exists() && !info.isSymLink())` 视为不存在，安全地处理 dangling symlink；目录用 `removeRecursively`，对软链接目录 Qt 会只移除链接。✅
- `DesktopUpdateHelper::recoverOrphanedDshUpdateFiles` (`DesktopUpdateHelper.cpp:301-361`)：硬匹配 `^\\.dsh-update-(.+)\\.(bak|tmp)-(\\d+)-(\\d+)$`，**不**操作任何其它名字；`bak` 用 `std::rename` 覆盖原文件、`tmp` 直接 `QFile::remove`。但 `QFileInfo(originalPath).exists()` 与 `std::rename` 之间是 TOCTOU 窗口（攻击者可在间隙替换 `originalPath` 为指向其它文件的 symlink），可能导致回滚时覆盖未授权文件。**等级：低**（需要本地 race 权限，且 `originalPath` 来自固定命名规范）。
- `DshDesktopApp::onClearDownloads` (`DshDesktopApp.cpp:1018-1024`)：枚举 `QDir::Files | NoDotAndDotDot`，逐个 `d.remove(name)`；若 `name` 在枚举与删除之间被替换为 symlink，可能删除链接目标。建议改用 `QDir::removeRecursively`（已用于其它目录删除）配合大小限制，或 `QFile::moveToTrash`。**等级：低**，需要本地 race。
- `DshDesktopApp::singleInstanceSocketPath` (`DshDesktopApp.cpp:958-968`)：`XDG_RUNTIME_DIR` 下创建 `dsh-desktop.sock`，`DshDesktopApp.cpp:251-264` 监听失败且 `AddressInUseError` 时 `QLocalServer::removeServer(sockPath)`。`removeServer` 在 socket 文件是 symlink 时会跟随并删除目标，攻击者如果在 `XDG_RUNTIME_DIR` 中放一个 `dsh-desktop.sock -> /etc/passwd` 的 symlink，可能被 `removeServer` 实际删除目标。**等级：中（取决于 XDG_RUNTIME_DIR 是否可写 — 标准上是 `0700`，但某些发行版未严格设置）。建议**：先 `QFileInfo(sockPath).isSymLink()` 检查，再决定是否 `removeServer`。

### 2.4 TOCTOU

- `DshDesktopApp.cpp:251-264` 单实例 socket：`QLocalServer::listen` → `AddressInUseError` → `QLocalServer::removeServer`。两步之间可被本地用户替换。**等级：低**。
- `SupervisedBackend::start`：`url_` 从构造参数传入（loopback hard-coded），但 `host` / `port` 经 `qgetenv("DSH_DESKTOP_PORT")` 覆盖（`SupervisedBackend.cpp:94-99`）。`DSH_DESKTOP_PORT` 仅作 `int` 转换使用，没有限上界；恶意值（如 `"999999999999"`，或 `"0x7f"`）可能让 `QString::number` 产生异常端口。**等级：低**，但仍应校验 `port.toInt(&ok, 10) && 1..65535`。
- `Updater::readLocalVersion` (`Updater.cpp:133-161`)：硬编码三个候选路径 `QFile::exists` → `open` → `readAll`，无跨用户竞争面。✅

---

## 3. QProcess / 提权 / 凭据泄漏

### 3.1 覆盖声明

| 类别 | 状态 |
| --- | --- |
| shell 派生 | 无（见 §1） |
| sudo 路径 | 唯一点：`SystemdBackend::systemctl` (`SystemdBackend.cpp:204-263`)。 |
| pkexec 路径 | `SystemdBackend.cpp:243/258`、`Updater.cpp:200-225`、`DshDesktopApp.cpp:677-746`。 |
| 提权前 NOPASSWD 探测 | `SystemdBackend.cpp:233-238` 使用 `sudo -n -l <exe>` 检测，**无** `-S`（从 stdin 读密码）。✅ |
| 凭据泄漏 | 无泄漏路径（详见 §3.5） |
| `geteuid` 检查 | `main.cpp:81-95` 拒绝 root 跑 GUI；`Updater.cpp:201-209` 用 `isTrustedRootExecutable` 校验 `pkexec`/`npm`；`SystemdBackend.cpp:230` 提权前判定 EUID。 |

### 3.2 `SystemdBackend::systemctl` 提权（`SystemdBackend.cpp:204-263`）

- 步骤：root 直调 → sudo NOPASSWD 探测（`sudo -n -l <systemctl-exe>`） → `pkexec --disable-internal-agent` → 降级为当前用户跑。
- 提权命令都用 `QProcess::setProgram/setArguments` 显式 argv，**无 shell**。✅
- 漏洞/问题：
  - **`sudo -n -l <exe>` 的 `<exe>` 用的是 `QStandardPaths::findExecutable("systemctl")`（`SystemdBackend.cpp:205`）**；如果攻击者篡改 `PATH`，可能探测到伪 `systemctl` 但实际 sudo 调用是真 `systemctl`（因 `sudo -n -l` 使用绝对路径），结果是 `sudo -n` 不 NOPASSWD 但继续探测 pkexec — 不构成提权绕过。**等级：无**。
  - `sudo` 探测 `program.start(sudo, {"-n", "-l", exe})` 中 `-l` 在 sudoers 中可能要求 tty 或不允许；`-n -l` 组合在新版 sudo 上能返回非 0（"a password is required"），导致走 pkexec。✅
  - `args.append(systemctlArgs)` 时 `systemctlArgs` 是 `{"<verb>", "<unitName>"}`，unit 名经 `SystemctlCommandBuilder::isValidUnitName` 过滤（`DshServiceManager.cpp:307-311`）。✅
- `lastOperationError_`（`SystemdBackend.cpp:62`）通过 `log` 信号 → `Logger` → 文件日志 + stderr。`detail` 含 `output` 拼接（`SystemdBackend.cpp:294-297`），**没有**写入密钥 / token 的路径（systemctl 输出只含 unit 名、PID、状态码）。✅

### 3.3 `Updater::performUpdate`（`Updater.cpp:192-252`）

- 版本号经 `parseSemVer`（`Updater.cpp:65-79`）严格正则 + 数字前导零拒绝；构造的包名形如 `@deepseek-ai/dsh@0.1.0-rc.7` 经显式 argv。✅
- `pkexec --disable-internal-agent` + `/usr/bin/npm` 都是固定绝对路径 + `isTrustedRootExecutable`（`Updater.cpp:121-127`：必须 `ownerId == 0 && !(perms & (WriteGroup|WriteOther))`），防 PATH 劫持与其它用户可写覆盖。✅
- pkexec 长等待用 `QElapsedTimer` 轮询 `waitForFinished(500)`，避免同步阻塞 GUI；超时先 `terminate()` 再 `kill()`，最后 `deleteLater`。✅
- `polkit action` 缺失风险：调用方未注入 polkit policy 文件路径；如果系统 polkit 配置缺失，`pkexec` 会失败 — 当前已通过 `log` 上报并返回 false。✅

### 3.4 `DshDesktopApp::ensureBackendStarted`（`DshDesktopApp.cpp:595-777`）

- `pkexec`/`npm` 通过 `QStandardPaths::findExecutable` 解析（**未**用 `isTrustedRootExecutable`，与 `Updater::performUpdate` 不一致 — `Updater` 走的是固定 `/usr/bin/pkexec`，这里是 PATH 解析）。如果 `PATH` 被恶意插入（CI 或开发环境可能），可能调用伪 `pkexec`。**等级：中**。建议统一改用 `/usr/bin/pkexec` + `isTrustedRootExecutable` 校验。
- `install` 子进程用 `QProcess` + `QEventLoop` + 取消逻辑，超时 10 分钟。✅
- 包名硬编码 `@deepseek-ai/dsh`，无拼接。✅

### 3.5 `SupervisedBackend` 的 `DSH_BIN`（用户指定复核点）

`src/backend/SupervisedBackend.cpp:20-34`：

```cpp
const QByteArray env = qgetenv("DSH_BIN");
if (!env.isEmpty() && QFile::exists(QString::fromLocal8Bit(env)))
    return QString::fromLocal8Bit(env);
auto found = QStandardPaths::findExecutable("dsh");
if (!found.isEmpty()) return found;
const QStringList candidates = {
    "/usr/bin/dsh", "/usr/local/bin/dsh",
    QDir::homePath() + "/.local/bin/dsh",
};
```

- 风险：
  1. **`DSH_BIN` 路径仅校验存在（`QFile::exists`），未校验 owner / 可写位**。如果本地普通用户伪造一个 `~/.local/share/.../my-dsh`（root-owned setuid 二进制），本程序会以非 root 拉起并直接执行它。**等级：高**。
  2. `QStandardPaths::findExecutable("dsh")` 走 `PATH`，受本地环境变量影响（`secure_path` 之外的用户 `PATH`），同样面临 PATH 劫持。
- 与 `Updater::performUpdate` 对比：`Updater` 用 `/usr/bin/pkexec` 硬编码 + `isTrustedRootExecutable`，更严格。**建议**：`SupervisedBackend` 的 `DSH_BIN` 与 `QStandardPaths::findExecutable` 都改走 `isTrustedRootExecutable` + 优先 `/usr/bin/dsh`；或仿 `Updater` 在 `Production` 构建里把 `DSH_BIN` 视为不可信。
- 已在 `README.zh.md:183` + `CHANGELOG.md:393-400` 标注"仅供开发/CI 覆盖"，**但未实现硬性拒绝**（只是文档化）。建议在 `DshDesktopApp` 启动期检查 `geteuid == 0` 时显式拒绝 `DSH_BIN`；或在 `SupervisedBackend::start` 中对 `DSH_BIN` 调用 `isTrustedRootExecutable`，不通过则 fallback 到 `/usr/bin/dsh`。
- 子进程 `start()` 时用 `setProgram(dshBin_)` + `setArguments({"web", "--host", host, "--port", port})`，argv 是拼接构造的，但 `host`/`port` 来源：`url_.host()`（来自构造参数，且本地代码已 `localhost → 127.0.0.1` 替换）或 `qgetenv("DSH_DESKTOP_PORT")`（仅做 `QString::number`，未校验范围）。**子进程参数本身**不会触发 shell 注入（QProcess 不经 shell），但端口非数字（如 `DSH_DESKTOP_PORT=abc`）会让 `port = configuredUrl.port(3080)` 走到默认 3080 — `QString::number(configuredUrl.port(3080))` 在 url 无端口时返回 "3080"，OK；但 `port` 变量最终可能为空字符串（如果 `DSH_DESKTOP_PORT=""` 且 url 也没有端口），那 `p->setArguments({"web","--host",host,"--port",""})` 会把空串当参数传给 dsh — **等级：低**，建议 `toInt(&ok, 10) && port > 0` 校验。
- `setProcessEnvironment` 注入 `DSH_HOME`（`SupervisedBackend.cpp:107-110`）：从父进程继承环境，并显式补默认 `${HOME}/.dsh`。如果调用方已设置 `DSH_HOME`，保留调用方值 — 这是合理的优先链。✅

### 3.6 凭据 / Token 泄漏

- 全仓 `grep` 搜索 `token|password|passwd|API_KEY|SECRET|AUTHORIZATION|Bearer`：
  - `dsh-theme-export` 解析 `kdeglobals`，**不**含任何密码；
  - `Updater.cpp` / `Updater.h` 仅 `token` 作为 SemVer 标识符用；
  - `ServiceUnitBuilder.cpp` / `SystemctlShowParser.cpp` 中 `token` 是字符串切分单元；
  - `scripts/check-dsh-profile.sh` 解析 systemd `Environment=` 但只读 `DSH_HOME`；
  - 无 OAuth、API key、JWT、SSH key 处理代码；
  - 日志（`Logger.cpp:31-48`）会写入 `lastOperationError_`、`backend_->status().detail`（含 system unit 名称、命令、错误输出），但 systemd 输出不含密钥。
- `Updater::performUpdate` `QProcess::setProcessChannelMode(MergedChannels)` 把 npm 全部输出吸到 stdout（`Updater.cpp:225`），写入日志；如果用户的环境里有 `NPM_CONFIG_TOKEN` 等敏感 npm 配置，会随 npm stderr 输出到日志（npm install 在某些错误下会回显 token）。**等级：低**，建议在日志记录前对 output 做 redaction（匹配 `Bearer\s+\S+`、`Authorization:\s*\S+`）。
- `DshDesktopApp.cpp:677-746` 自动安装同样 MergedChannels + `QString::fromLocal8Bit(install->readAll())` 写入 QMessageBox，可能泄漏 npm 凭据。**等级：低**。

---

## 4. systemd unit 注入

### 4.1 `ServiceUnitBuilder`（`ServiceUnitBuilder.cpp`）

- 输入校验（行 84-112）：拒空、非绝对路径、含 `\n`/`\r`/`\0` 的 `dshExecutable`/`workingDirectory`/`host`/`dshHome`/`user`；端口 `1..65535`；system scope 必填 `user`。
- 转义（`escapeInsideQuotes`，行 29-47）：`\\` → `\\\\`、`"` → `\\"`、`%` → `%%`（specifier 转义）；ExecStart/Exec* 额外把 `$` 转 `$$`。✅
- `needsQuotes`（行 51-60）：只要含空白/`"`/`'`/`\\`/`%`/`$` 就用双引号包裹。
- `token()`（行 63-67）：按 `expandDollar` 选择包裹。
- 用户输入 unit 名 → `SystemctlCommandBuilder::isValidUnitName` 严格白名单（见 §1.1）。✅
- 单测 `test_service_unit_builder.cpp` 覆盖 `$VAR`、`--port`、`$(id)`、含空格、`%` 等注入向量。✅
- **剩余问题**：`spec.host` 仅校验非空 + 不含控制字符，**没有**白名单 IPv4/IPv6/loopback。攻击者（或恶意上游配置）传入 `"host=--profile\x00foo"` 这样的字符串会被 `\0` 校验拦下；但传入 `"host=foo;rm -rf /"` 时，`escapeInsideQuotes` 会把 `;` 原样保留 → 包裹后是 `"foo;rm -rf /"`，systemd 不会把它当多 token（因为有引号），但语义上仍是 host=literal `foo;rm -rf /`。建议 `host` 白名单 `^[\w.\-:[]+/$`。**等级：低**。

### 4.2 `InstallationPlan` / `ServiceProvisioner`

- `ServiceProvisioner::plan` (`ServiceProvisioner.cpp:88-128`)：先调 `ServiceUnitBuilder::build` 校验 + 拼文本；目标已存在则 `ExistingUnitUnchanged`（不覆盖）；系统级必须显式注入 `systemWriter`（`ServiceProvisioner.cpp:119-124`）。✅
- `writeUnitFile` (`ServiceProvisioner.cpp:224-269`)：用户级 `QSaveFile` 原子；系统级委托 `ISystemUnitWriter`。✅
- 归属记录（`ServiceOwnership`）写入 `~/.local/state/.../services-owned.json`（`ServiceOwnership::defaultStateFilePath`，行 64-74），用 `QSaveFile` 原子，SHA-256 指纹校验 ExecStart。✅
- `ServiceOwnership::load` 在解析失败时**保持内存不变**（`ServiceOwnership.cpp:131-136`），不会被恶意 json 文件清空。✅

### 4.3 `dsh-theme-export.service` hardening（用户指定复核）

`packaging/dsh-theme-export.service:1-22`：

- ✅ `NoNewPrivileges=yes`：禁止 setuid/setgid 提权；
- ✅ `ProtectSystem=strict`：只允许写入 `ReadWritePaths`；
- ✅ `PrivateTmp=yes`；
- ✅ `ProtectHome=yes`：屏蔽 /home, /root, /run/user；
- ✅ `ReadOnlyPaths=/root/.config`：仅放行 kdeglobals 所在目录（只读）；
- ✅ `ReadWritePaths=/run/dsh-desktop`：仅放行主题输出目录；
- ⚠ **未设 `User=`/`Group=`**，systemd 默认 root（脚本注释行 15-16 显式说明：脚本需读 /root/.config/kdeglobals 并写 /run/dsh-desktop/theme），符合预期。
- ⚠ **缺失**：`RestrictAddressFamilies=`、`RestrictNamespaces=`、`SystemCallArchitectures=native`、`LockPersonality`、`ProtectKernelTunables`、`ProtectControlGroups`、`CapabilityBoundingSet=`（限制成空集）。
- ⚠ `ExecStart` 是 `/usr/lib/dsh-desktop/dsh-theme-export`，绝对路径不会被 PATH 劫持。
- ⚠ **未指明 `Type=` 之外的资源限制**（`MemoryMax`、`CPUQuota`）— 但此脚本只跑几毫秒，影响有限。
- **风险**：脚本 `packaging/dsh-theme-export` 启动 root 读 `/root/.config/kdeglobals`；文件若被本地 root 用户植入任意 `LookAndFeelPackage=$(...)` 行，awk 会执行命令替换（`awk -F= '... {look_and_feel=$2}'`，`$2` 在 awk 里是字段引用，**不**触发 shell，但 awk 的 `system()`/`getline` 会）。当前 awk 脚本仅做 `$1 == "..."` 比较 + 简单赋值，**不**触发命令执行。✅ 但若未来扩展引入 `print value | "cmd"`，会立即变成 RCE。**等级：中（防御性）**，建议在 service 文件加 `ProtectKernelModules=yes`、`ProtectControlGroups=yes` 等强化。
- `packaging/dsh-theme-export` 自身：
  - `set -euo pipefail` ✅；
  - 输入路径来自环境变量（`DSH_KDEGLOBALS_SOURCE`、`DSH_THEME_OUTPUT`、`DSH_THEME_REQUIRE_ROOT_PLASMA`），`output_directory="$(dirname "$output_file")"` + `mkdir -p -m 0755`：如果 `DSH_THEME_OUTPUT` 被攻击者设为 `/etc/cron.d/foo`，会创建 `/etc/cron.d/`（mkdir 0755）+ 写 `foo`。**等级：高**（systemd unit 的 root 身份下）。建议 service 内置 hard-coded 默认路径 / 显式拒绝含非预期前缀。
  - `output_file="${DSH_THEME_OUTPUT:-/run/dsh-desktop/theme}"` — `:`-` 形式允许注入；攻击者不能改 service 的 environment（unit 不带 `EnvironmentFile=` / `Environment=`），但若有人改 `dsh-theme-export.path` 加 `Environment=`，就能注入。**等级：低**。
  - `case "${value,,}" in *dark*|light|*breeze*)` 解析 KDE theme 是字符串字面量匹配，无注入。
  - `temporary="$(mktemp "$output_directory/.theme.XXXXXX")"` + `trap 'rm -f "$temporary"' EXIT` + `chmod 0644 "$temporary"` + `mv -f "$temporary" "$output_file"`：原子写 + 清理临时文件。✅

### 4.4 unit 文件不写 /etc

`ServiceProvisioner.cpp:118-124`：系统级必须注入 `systemWriter`，否则 `NoSystemWriter`。✅
`install.sh:153-193` `configure_dsh_service`：纯只读 `systemctl show`，**绝不**写 / 启停任何服务。✅
`install.sh:195-200` `configure_theme_export`：只 `systemctl daemon-reload` + `enable --now dsh-theme-export.path` + `start dsh-theme-export.service`，硬编码 unit 名（来自 `packaging/`）。✅

---

## 5. URL / SSRF

### 5.1 覆盖声明

| 表面 | URL 来源 | 是否校验 | 证据 |
| --- | --- | --- | --- |
| `Updater::fetchLatestVersion` | 写死常量 `https://registry.npmjs.org/@deepseek-ai/dsh/latest` (`Updater.cpp:29-30, 164`) | 无需校验 | ✅ |
| `DesktopVersionChecker::fetchLatestRelease` | 写死 `https://gitee.com/api/v5/repos/eruditeLoong/dsh-desktop-qt/releases/latest` (`DesktopVersionChecker.cpp:22-23`) | 无需校验 | ✅ |
| `DesktopReleaseDownloader::start` | Gitee release JSON 中的 `assets[].browser_download_url` | `isAllowedUrl` 校验 scheme=HTTPS + host=gitee.com | `DesktopReleaseDownloader.cpp:132-147` |
| `HttpProbe::httpProbe` | 调用方传入（`backend_->url()`，loopback 严格白名单） | `Backend::isLoopbackUrl` (`Backend.cpp:18-23`) | ✅ |
| `SyncHttp` (`Updater.cpp:165`、`DesktopVersionChecker.cpp:135`) | 同上两条写死 URL | 无 SSRF 面 | ✅ |
| `Backend::createForHost` + `ExternalBackend` | `--url`/`DSH_DESKTOP_URL` 命令行/环境 | **未**校验 SSRF（任何 URL 都可被设成"远程后端"） | 见 §5.2 |

### 5.2 `ExternalBackend` 接受任意 URL（设计文档化）

`src/backend/Backend.cpp:29-63`：

- 用户用 `--url http://内网:port/` 或 `DSH_DESKTOP_URL=http://...` 可以让桌面端监控任意远端。
- 设计意图：远程 dsh-web 用户场景；显式标注 `manageable = false`，不发起任何进程派生。
- 安全风险：用户或被钓鱼的开发者可能在受控环境里把 URL 写成 `http://169.254.169.254/latest/meta-data/`（云元数据 SSRF）或 `http://localhost:9200/`（暴露 elasticsearch）。本程序没有"任意用户能改 URL"的功能面 — URL 只来自命令行的 `--url` 和 `DSH_DESKTOP_URL`，都是当前用户主动设置。**等级：低（按文档意图是 feature，不是 bug）**。建议：
  - 在 README 中显式说明："DSH Desktop 不会主动做 SSRF 防护，使用 `--url` 时请确认是你信任的远端"；
  - 或增加 host 黑名单（loopback / link-local / metadata IP）。

### 5.3 `HttpProbe` 派生 curl

`src/util/HttpProbe.cpp:17-22`：
```cpp
curl.start(QStringLiteral("curl"),
           {QStringLiteral("-s"), ..., QStringLiteral("--max-time"),
            QString::number(kCurlMaxTimeSec), url + QStringLiteral("/")});
```
- `url` 来自调用方（`SupervisedBackend::isRunning` / `SystemdBackend::isRunning` / `ExternalBackend::isRunning`），全部经过 `Backend::createForHost` 的 `isLoopbackUrl` 白名单 + `DefaultBackend` 写死 loopback。`ExternalBackend::isRunning` 使用 `url_`（用户的远端）—— **会**用 curl 探测任意外部 URL（设计文档如此）。✅
- `url + "/"` 拼接：URL 不含 `"\0"`（`QUrl` 校验），注入面在 shell 拼接上不存在（`QProcess` argv）。但若 `url` 含 `;` 或 `&`，curl 会作为 URL 路径发送，无解释 — 这是 curl 的语义，不是注入。✅
- `curl` 用 `QStandardPaths::findExecutable("curl")` 解析，受 PATH 影响。`HttpProbe.cpp:16` 用 `QProcess curl;` 默认程序名 — 实际等价于 `QProcess::start("curl", ...)`，会走 PATH。建议固定 `/usr/bin/curl`。**等级：低**。

### 5.4 `QNetworkRequest::RedirectPolicyAttribute = NoLessSafeRedirectPolicy`

`src/app/DshDesktopApp.cpp:879-880`：
- 仅在 `pollBackendHealth` 用于健康探测；后端是 loopback，理论上不会重定向到外网。
- 但若 `DSH_DESKTOP_URL` 被改成 `https://attacker.example/` 而 `NoLessSafeRedirectPolicy` 允许 `https → https`，攻击者可让重定向把探测包发到外网（不是真正 SSRF 提权，因为仅做 GET）。**等级：低**。
- 同步 HTTP 工具（`SyncHttp.cpp`）**未**设置 redirect policy，Qt 默认 `QNetworkRequest::NoLessSafeRedirectPolicy`（等同 `ManualRedirectPolicy` 在某些版本），`SyncHttpGet` 不处理 redirect 但 Qt 默认仍然只允许同策略。✅

### 5.5 WebEngine 网络

- `view_->setUrl(QUrl(url_))` (`DshWindow.cpp:131, 152, 263`)：URL 来源 `args_.url`（`--url`）→ 写默认 loopback。
- `LoopbackWebPage::acceptNavigationRequest` (`LoopbackWebPage.cpp:126-140`)：拒绝非 loopback 内部导航 → 走 `opener` 调系统浏览器。
- `LoopbackWebPage::isInternal`（行 103-124）：显式 loopback 检查 + blob 协议递归。
- 任何注入 `view_->setUrl` 的代码路径都不存在（`url_` 在构造时一次性设定）。✅
- **保留风险**：`QtWebEngineSettings::JavascriptCanAccessClipboard` 在 `DshWindow.cpp:91` 启用；`permissionRequested` 钩子（`DshWindow.cpp:107-120`）只对**同源 + ClipboardReadWrite** 才授权写入剪贴板 — 满足规范的最小授权。✅

### 5.6 同步 HTTP 重定向与 body 限制

- `SyncHttp.cpp` / `Updater.cpp` / `DesktopVersionChecker.cpp` 没有 `setTransferTimeout` 之外的资源限制；若 npm 注册表 / Gitee API 返回超大 body，Qt 内部缓冲可能耗内存。建议设 `request.setMaximumRedirects(5)`。**等级：低**。

---

## 6. `DSH_BIN` 与 `qgetenv` 受信任输入

### 6.1 受信任的"环境即输入"

| 环境变量 | 用法 | 受信任度 | 风险 |
| --- | --- | --- | --- |
| `DSH_BIN` | `SupervisedBackend::resolveDshBin` 决定 dsh 路径 | 高（执行） | 见 §3.5 |
| `DSH_HOME` | `SupervisedBackend::status` (line 63-66) 透传给子进程 + UI 展示；`ThemeWatcher` 不读 | 中 | 不构成注入面（值只用作环境变量传递），但若用户被诱导设成 `/root/.config`，可能让普通用户的 dsh 子进程把数据写到 root home（dsh 自身的信任边界）。✅ |
| `DSH_DESKTOP_PORT` | `SupervisedBackend::start` line 94 | 中 | 见 §3.5 |
| `DSH_DESKTOP_URL` | `Backend::defaultUrl` line 147-149 | 高（决定后端 URL） | 见 §5.2 |
| `DSH_DESKTOP_ALLOW_ROOT` | `main.cpp:82` 允许 root 跑 GUI | 设计明确 | 跳过 WebEngine sandbox；建议加额外警告（已经在 stderr 输出警告，但没要求确认）。 |
| `DSH_DESKTOP_SOFTWARE_RENDERING` | `RenderingPolicy.cpp:73` | 低 | 仅设置 Qt 软件渲染标志。✅ |
| `DSH_DESKTOP_THEME_FILE` | `ThemeWatcher.cpp:142-150` 主题标记文件路径 | 中 | 文件内容被**严格** `value == "dark" || value == "light"` 匹配，不传播内容（已显式注释行 138-141）。✅ |
| `DBUS_SESSION_BUS_ADDRESS` | `DshDesktopApp::connectToKdeSessionBus`（line 67-101）：枚举 `originalAddress` + `RuntimeLocation/bus` 候选，`unix:path=` 前缀补齐 + `QDBusConnection::connectToBus`（不传 shell） | 中 | 没有 `system()` 拼接；`connectToBus` 内部走 Qt D-Bus 库，无 shell。✅ |
| `XMODIFIERS/QT_IM_MODULE/GTK_IM_MODULE/SDL_IM_MODULE` | `main.cpp:38-49` 默认 fcitx | 低 | `qputenv` 设置 string，无注入。✅ |
| `DSH_PROFILE` / `DSH_WEB_URL` | `scripts/check-dsh-profile.sh:6-8` | 高（决定 profile 路径与 base_url） | 脚本对 `profile`/`base_url` 都做显式校验（profile 存在 + node_modules；base_url 用于 curl）。✅ |

### 6.2 桌面启动期校验

- `main.cpp:104-145`：使用 `QCommandLineParser`，`--theme` 强约束 `dark/light`，否则 `return 4`。✅
- `main.cpp:62-77`：通过原始 argv 判断 `--smoke`/`--probe` 等 no-GUI 标志，决定 `QApplication` vs `QCoreApplication`。✅
- `main.cpp:81-95`：root + GUI 拒绝（`DSH_DESKTOP_ALLOW_ROOT=1` 才允许）。✅

---

## 7. 脚本危险命令 / Shell 静态面

### 7.1 `packaging/install.sh`

- `set -euo pipefail`（line 20）✅
- `run_build_command`（line 36-48）：防御性检查参数非空 + `SUDO_USER` 时降权到 `sudo -u "${SUDO_USER}" -- "$@"`。`"$@"` 是 quoted，**无** shell 注入。✅
- `ensure_pkgs`（line 52-87）：`pacman -S --needed --noconfirm "${missing[@]}"` 用数组展开。`npm install -g @deepseek-ai/dsh` 包名硬编码 + `npm config set ignore-scripts true` 防止 preinstall/postinstall 执行任意 JS（`install.sh:82-83`）。✅
- `install_artifacts`（line 95-98）：`cmake --install ... --prefix /usr`，参数都是字面量。✅
- `install_icons`（line 100-121）：`cp -f`、`install -Dm644`、`gtk-update-icon-cache` 都用字面量。✅
- `configure_dsh_service`（line 153-193）：纯只读 `systemctl show` + `is_official_dsh_web` 正则（line 140-151）。✅
- `configure_theme_export`（line 195-200）：硬编码 unit 名 `dsh-theme-export.path`/`dsh-theme-export.service`。✅
- `uninstall`（line 202-215）：调原生卸载器 `dsh-desktop-uninstaller --prefix /usr`，参数硬编码。✅

### 7.2 `packaging/dsh-theme-export`（bash）

- `set -euo pipefail` ✅
- `awk -F= '... END {...}' "$source_file"`：awk 脚本是单引号字面量，**不**拼 user input。✅
- `mktemp "$output_directory/.theme.XXXXXX"` + `trap 'rm -f "$temporary"' EXIT`：原子临时文件。✅
- `case "${value,,}" in`：bash 5.0+ `${var,,}` 安全；`%dark%` / `%light%` / `%breeze%` 等是 glob 但 case 用 `*…*` 显式匹配。
- **风险**（已述 §4.3）：`output_file` 来自环境变量，可能被 root 用户改成任意路径。

### 7.3 `scripts/check-dsh-profile.sh`

- `set -euo pipefail` ✅
- `systemd_environment_value`（line 13-43）：嵌入 Node.js 字符串，无 shell。Node 解析传入的 `Environment=` 文本，提 `DSH_HOME=...`。✅
- `curl` 调用（line 192-197）：`--fail --silent --max-time 5` 等显式选项，`base_url` 来自 systemctl `ExecStart` 解析（用户级可控 — ExecStart 是管理员写的 unit），**可控输入面**：
  - 如果恶意 unit 写 `--host $(rm -rf /)`，bash 解析 `exec_start` 时不会执行（`exec_start =~ ${host_pattern}` 仅 regex 提取）；
  - 提取出的 `detected_host` 经 case 替换 `0.0.0.0 → 127.0.0.1`、`:: → ::1`，但其它任意值（如 `attacker.com:80`）直接进入 `base_url`。
  - 然后 `curl --max-time 5 --output /dev/null "$base_url/"` → curl 会去请求 attacker.com。
  - **等级：中（管理员可控输入，且通常受信任；但如果脚本被非管理员通过 sudo 调用，恶意管理员 unit 可让 curl 探测内部网络）**。建议 `detected_host` 白名单为 RFC1918 / loopback / 已知 host，或加 `--connect-to` 强制只访问 loopback。
- `execFileSync(process.execPath, ['--check', target], ...)`（line 174）：纯 JS `--check`，无 shell，target 是 `path.resolve(packageDir, entry)`，已被 `target.startsWith(packageDir+sep)` 校验（line 162-165）防穿越。✅
- `collectExportPaths`（line 125-136）：深度限制 10 层防恶意嵌套 JSON 触发栈溢出。✅

### 7.4 `scripts/smoke.sh`

- `mktemp` 用 `${TMPDIR:-/tmp}`（line 11, 26-27）— 若 `TMPDIR` 是攻击者控制的目录，可能引发 symlink 攻击（典型如 `/tmp` 是 sticky，本目录是 `0700`）；脚本在结尾 `rm -f` 清理，OK。
- `python3 - "$SELF_TEST_LOG" <<'PY'`（line 44）：heredoc `<<'PY'` quoted，**不**做参数展开；`$SELF_TEST_LOG` 用 `"$SELF_TEST_LOG"` 传递，python 仅以 `sys.argv[1]` 读。无注入。✅

### 7.5 `tests/test_profile_checker.sh`（用户指定复核 fixture 修复）

- 行 6：`${1:?checker path is required}`：参数缺失即 fail。
- 行 12-15：`unset DSH_HOME` + `unset DSH_WEB_URL` 显式重置开发环境注入 — 这正是 CHANGELOG.md:560-571 描述的"59d2ff0 改 checker 后回归根因"。修复方式正确（用 inline env 不可靠时显式 unset），但 **fixture 仍可能受未来环境变量增加影响**；建议 `env -i` 启动 fixture 以彻底隔离。
- 行 38-43：写 mock `systemctl`/`curl` 脚本到 `${fixture_root}/bin`：`#!/usr/bin/env bash`；用 `printf` 输出 `LoadState=`/`ExecStart=`/`Environment=` 等键值对；mock 通过 `${FAKE_SERVICE_PID}` / `${FAKE_EXEC_START}` / `${FAKE_DSH_HOME}` 接收外部变量。
- 行 50-53：`DSH_HOME="${fixture_root}/home" sleep 30 &` — 用环境变量前缀启动后台进程，**仅**为了拿 PID 给 mock 使用。`wait`/`kill` 处理正确（line 84-86）。
- 行 64-65：`FAKE_EXEC_START` 注入到 mock stdout，checker 看到非 web 命令应拒绝 → 测试 line 58-63 验证。
- 行 75-80：用真实文件内容生成 profile 入口，避免依赖模板 fixture。
- 行 87-88：`FAKE_SERVICE_PID=0` 再跑一次验证 `MainPID=0` 退化分支。
- **风险**：
  - mock 脚本里 `printf 'LoadState=loaded\nMainPID=%s\n' "${FAKE_SERVICE_PID}"` — `${FAKE_SERVICE_PID}` 是 test 自身 export 的，可信；但 mock 脚本写在 `${fixture_root}/bin/systemctl`，若 `${fixture_root}` 被攻击者预放同名 symlink，可能改写到 fixture 之外。`mktemp -d` 创建在 `${TMPDIR:-/tmp}`，默认 `0700`，**等级：低**。建议 `chmod 0700 "$fixture_root"` + 检查 `info.isSymLink()`。
  - 行 87 后 fixture 不再 `unset` FAKE_* 变量，下个测试可能受影响 — 当前只有一个测试运行（`add_test` 单一），**等级：低**。
  - `kill "${service_process}"` 在 cleanup 中是 `2>/dev/null || true`，不严；若进程已变 zombie，wait 不会回收，可能泄漏 PID。**等级：低**。

### 7.6 `tests/test_is_official_dsh_web.sh`

- `set -euo pipefail` ✅
- `source <(sed -n '/^is_official_dsh_web()/,/^}/p' "${installer}")`（line 28）：process substitution source，无中间文件。**等级：低** — sed 提取依赖函数体首行 `is_official_dsh_web()` 精确匹配，install.sh 改动会破坏测试。这是合理的脆弱性（fixture = install.sh），不是注入。
- 16 个用例覆盖正负向。✅

---

## 8. C++ 内存 / 整数 / 类型安全

### 8.1 整数

| 位置 | 风险 | 等级 |
| --- | --- | --- |
| `Updater.cpp:233-243` `kUpdateTimeoutMs = 10*60*1000` (qint64) + `elapsed.elapsed()` 返回 qint64 | 无溢出 ✅ | — |
| `ServiceUnitBuilder.cpp:98` `port < 1 || port > 65535` | ✅ | — |
| `SystemctlShowParser.cpp:128-131` `pid.toLongLong(&ok)` + `pid > 0` | ✅ | — |
| `SystemctlShowParser.cpp:151-155` `port.toInt(&ok)` + `parsedPort > 0` | ✅ | — |
| `ServiceOwnership.cpp:128-131` `pid.toLongLong(&ok)` + `pid > 0` | ✅ | — |
| `HttpProbe.cpp:26-27` `curl.readAllStandardOutput().toInt(&ok)` + `code >= 200 && code < 500` | ✅ | — |
| `Updater.cpp:233-243` `elapsed.elapsed() < kUpdateTimeoutMs`：循环内 `p.waitForFinished(500)`，每次 500ms 累加；总循环受 `elapsed.elapsed() < kUpdateTimeoutMs` 限制，**但** elapsed 在 waitForFinished 返回后才会增加，循环条件仍成立 → 不会溢出。✅ | — |
| `DshDesktopApp.cpp:391` `qMin(retryNumber - 1, 3)` (int) + `1 << qMin(...)`：若 retryNumber == 0（不可能，因为 `++retryNumber` 在条件判断之后）… 实际不可能为负。✅ | — |
| `DshDesktopApp.cpp:758-764` `output.size()` 等 length 取值不会溢出。✅ | — |
| `Updater.cpp:88-115` `compareSemVer` 用 `int` 比较 + `QString::compare` 差值；不会溢出。✅ | — |
| `Logger.cpp` `out << stamped << '\n'`：`stamped` 不含 `\0`，QTextStream 不会截断。✅ | — |
| `Uninstaller.cpp:198-200` `quint32 uid = ::geteuid()`：可能为 32-bit 不存负数；后续 `arg(uid)` 拼接 OK。✅ | — |
| `SupervisedBackend.cpp:143-145` `process->processId()` 返回 qint64，与 `pid_t` 强转 `static_cast<pid_t>(pid)`：若 pid > INT_MAX（理论），截断；构造 `kill(-static_cast<pid_t>(pid), ...)` 也截断。**等级：极低**（实际不会触发）。 | 低 |

### 8.2 字符串 / 路径

- `LogViewer.cpp:38-41`：用 `arg()` 拼接，不含 `%` 转义（Qt 富文本标签里 `%` 没特殊含义）。✅
- `DshDesktopApp.cpp:319-321` `QString::arg` 链：传入 `fixes` 是字面量 QStringList。✅
- `Updater.cpp:75` `info.mainPid = (ok && pid > 0) ? pid : -1`：负值 → -1；下游 `info.mainPid` 是 qint64。✅
- `SupervisedBackend.cpp:73-75` `proc_->processId() -`exitCode 已是 int。✅

### 8.3 指针 / 生命周期

- `DshServiceManager::startProcess` 创建 `new QProcess(this)`，结束时 `process_->disconnect(this); process_->deleteLater();`。**没有** 显式 `if (process_ != p)` 比较 — 但 `cleanupCurrent`（`DshServiceManager.cpp:630-644`）先 `disconnect` 后 `deleteLater` + `process_ = nullptr`，**正确**避免 UAF。✅
- `DshServiceManager` 内有 `connect(process_, ...)` 在 `startProcess` 重入时（已有 process_），先 `disconnect(this)` + `deleteLater()`，安全。✅
- `DshServiceManager::~DshServiceManager`（行 79-87）：`process_->kill()` + `deleteLater()` — 但 `process_` 是 `QObject` 子类，`deleteLater` 在 dtor 期间不会派发（事件循环已结束），实际靠 QObject 父对象 `this` 析构时回收（QProcess 是 QObject，parent 是 this）。✅
- `Updater::performUpdate`（`Updater.cpp:222-252`）：局部 `QProcess p;`（栈对象）+ `p.start()`；结束时 `p` 出作用域析构。若 `p.kill()` 后子进程仍在跑，Qt 会发 SIGKILL on parent destruction（实际不会，仍泄漏子进程直到系统重启）。**等级：低**，但终止后主程序就退出（用户触发），子进程被 init 收养为孤儿。✅
- `Uninstaller::removePath`（`Uninstaller.cpp:107-133`）：`QDir::removeRecursively()` 若失败仍 append error，不抛。✅
- `Logger::log`（`Logger.cpp:31-48`）：`QMutexLocker` + `QFile::open(Append)`，若打开失败**静默**（不写文件但仍写 stderr）。建议至少 `qWarning` 一次。**等级：低**。

### 8.4 类型转换

- `SupervisedBackend.cpp:94` `QString::number(configuredUrl.port(3080))`：port 是 int，没有负值问题。
- `Updater.cpp:273-278` `compareVersions(current, kMinimumDshVersion)` 比较 SemVer，OK。✅
- `DshDesktopApp.cpp:126-127` `QStringLiteral("…")`：所有 UI 字面量都 `QStringLiteral` 或 `QString::fromLatin1` 显式编码。✅
- `DshServiceManager.cpp:25-39` `fromQProcessError` 穷尽 switch（无 default 但返回兜底 Unknown）。✅

### 8.5 进程信号 / 共享内存

- `SupervisedBackend::stop`（`SupervisedBackend.cpp:136-166`）：先 `processId() == 0` 短路 → `kill(-pid, SIGTERM/SIGKILL)` 杀进程组 → `waitForFinished`。✅ 但 **`setsid()` 在 `start()` 中（行 104-106）通过 `setChildProcessModifier` 完成，子进程立即脱离父进程组，所以 `-pid` 实际指向子进程自身 PID（即新创建的 session leader）**。Qt 6 的 setChildProcessModifier 在 fork 后立即执行，所以 `setsid` 一定会跑（不依赖 child 是否启动成功）。**等级：低**，验证：可加 `qint64 sid = ::getsid(pid)` 测试。
- `DshServiceManager::cancel`（行 280-287）：`process_->kill()`（不发进程组），如 supervisor 拉了子进程，可能留孙子进程。**等级：低**。

---

## 9. 用户指定重点复核

### 9.1 `RunSyncProcess`（新增 util）

- 强制 `program + QStringList args` + 显式 argv（**无** shell）。
- `waitForStarted` + `waitForFinished` 双向超时 + `kill()` + 再 wait + `qMin(kMinTimeoutMs, …)` 100ms 兜底。
- 已知旧 bug（`is-active` 进程泄漏）已修复：`SystemdBackend.cpp:128-135` 用 runSyncProcess，超时自动 kill。✅
- 单测 `test_run_sync_process.cpp` 覆盖 /bin/echo、/bin/false、不存在程序、超时、MergedChannels。✅
- **唯一顾虑**：`kMinTimeoutMs = 100` 兜底有效，避免 `timeoutMs=0` 时的 `waitForFinished` 立即返回。但 `killGraceMs` 默认 3000（`RunSyncProcess.h:58`），在 timeoutMs 极小（如调用方传 1）时，`waitForFinished(kMinTimeoutMs)` 100ms 后立即 kill → 子进程可能被 kill 而非优雅退出。这是**调用方契约**，不是 bug。**等级：无**。

### 9.2 `SystemctlCommandBuilder`（新增 service）

- 纯函数（static methods），无 QObject，单元测试友好。
- `isValidUnitName` 白名单 + 后缀 + 拒路径分隔 + 拒控制字符 + 拒元字符。
- `operationNeedsElevation` 仅在 system scope + Start/Stop/Restart/DaemonReload/Enable + 非 root 时返回 true。
- `resolveCommand` 决定 pkexec 包裹。
- 单测 `test_service_manager.cpp` 28 处调用覆盖。✅

### 9.3 `ShowFailure`（新增 service）

- 纯函数分类，不接触任何进程/系统调用。
- stderr 来源：`systemctl show` 的 stderr（`ServiceDiscovery.cpp:57`、`DshServiceManager.cpp:534`）。
- **风险**：`err.contains("connect", Qt::CaseInsensitive)` 可能误报 `Disconnect`、`reconnect`、`connector` 等无关 stderr。但下游只影响 UI 文案（`rejectionDetail`），不影响决策路径。**等级：无**。
- 单测 `test_service_discovery.cpp:364-438` 覆盖正负向。✅

### 9.4 `SyncHttp`（新增 util）

- 一次性 NAM + EventLoop + QTimer 超时 + `reply->abort()`。
- 区分 HTTP 错误（4xx/5xx，**保留**为 ok=true + httpStatus 透传）与网络失败（timeout/连接拒绝，ok=false）。
- User-Agent 默认 `dsh-desktop/<DSH_DESKTOP_VERSION> (Qt6)`。
- 调用方（`Updater.cpp:165`、`DesktopVersionChecker.cpp:135`）都传 hard-coded URL。✅
- **唯一顾虑**：未设 `request.setMaximumRedirects()`；Qt 6 默认 `QNetworkRequest::ManualRedirectPolicy`（即不自动跟随），但部分平台默认可能不同。建议显式 `setMaximumRedirects(5)` + `NoLessSafeRedirectPolicy`。**等级：低**。

### 9.5 `SupervisedBackend` 的 `DSH_BIN`

详见 §3.5（**等级：高** — `DSH_BIN` 仅 `QFile::exists` 校验，未校验 owner / write 位 / `setuid` 位）。

### 9.6 `dsh-theme-export.service` hardening

详见 §4.3（沙箱硬化整体良好；建议追加 `RestrictAddressFamilies`、`SystemCallArchitectures` 等；**环境变量覆盖路径**是较高风险）。

### 9.7 profile-checker fixture 修复

详见 §7.5（`unset DSH_HOME` + `unset DSH_WEB_URL` 是正确的修复路径，但 `env -i` 更彻底；fixture 自身没被硬链接保护）。

---

## 10. 其它静态发现

### 10.1 单实例 socket

`DshDesktopApp.cpp:233-265`：socket 落在 `$XDG_RUNTIME_DIR/dsh-desktop.sock`（`UserAccessOption`）。`AddressInUseError` 时 `QLocalServer::removeServer(sockPath)` — 见 §2.3 中级风险。

### 10.2 卸载器外部路径

`UninstallerMain.cpp:222-225` 接受 `--prefix` / `--root`，把绝对路径重写到 `${root}/` 之下（line 58-66）。`rewritePath` 不校验 `${root}` 是否真存在或是否是真沙箱；调用方传入任意 `--root /etc` 会让 `Uninstaller` 实际在 `/etc/...` 下做 `removeRecursively`（前提是 `removeRecursively` 通过 selfExe 检查 — `selfExe` 是真实路径，不会被 `rewritePath` 影响，所以 selfExe 路径不同 → 不会被跳过 → 可能删 `/etc` 下文件）。**等级：中**（CLI 是 root 权限运行 + 受信任场景，但仍建议 `--root` 必须解析为与 `--prefix` 同前缀或显式要求 /tmp/sandbox）。

### 10.3 Backend `pollBackendHealth`

`DshDesktopApp.cpp:876-934`：每 30s GET backend URL，2s 超时 abort。`RedirectPolicyAttribute = NoLessSafeRedirectPolicy`（已述 §5.4）。`consecutiveHealthFailures_` 折叠（`kHealthFailureThreshold = 3`）防抖动。✅

### 10.4 Notify D-Bus

`util/Notify.cpp`：所有参数显式类型；`urgency` 仅识别 `"low"/"normal"/"critical"` 三种字面量，其它值退化为 `1`；`timeoutMs` 强制 `qMax(1000, timeoutMs)`。✅

### 10.5 ThemeWatcher 读 kdeglobals

`ThemeWatcher.cpp:137-161`：用 `QSettings(path, NativeFormat|IniFormat)` 读 `KDE/LookAndFeelPackage` 等键。`QSettings` 内部走 INI 解析器，无 shell。✅
- `DSH_DESKTOP_THEME_FILE` 已严格匹配 `dark`/`light`（行 144-150）。✅
- `/root/.config/kdeglobals` 读取：当 `getuid() != 0` 时加入 candidates（行 156-159）。普通用户读 root home 依赖文件权限（KDE 默认 0644 root owned）。**等级：低**。

### 10.6 C++ 异常与 abort

- 全仓无 `throw`、`catch`、`abort()`。Qt 默认异常禁用。✅
- `DshDesktopApp.cpp:401` `std::_Exit(0)` 跳过 QtWebEngine 析构；与 `--self-test` 路径绑定，无误用风险。✅

### 10.7 日志泄漏

`Logger.cpp:37` 镜像到 stderr；`Logger.cpp:43-47` 写文件 `~/.local/share/dsh-desktop/dsh-desktop.log`。文件权限是默认 `0644`，**任何本地用户可读**。如果 `lastOperationError_` 或 `backend_->status().detail` 含敏感数据（system unit 名、PID、执行命令 — 都是系统元数据，**不**含密钥），且 `Updater::performUpdate` 的 `output`（合并的 npm stdout+stderr）会包含 npm 错误信息（潜在泄漏 npm token）。**等级：低**。
- 建议：日志文件 `0600`（`QFile::setPermissions(file_path_, ReadOwner|WriteOwner)`）+ 启动时 umask 022。

---

## 11. 整改建议（汇总）

按风险等级倒序：

1. **【高】** `SupervisedBackend::resolveDshBin`（`SupervisedBackend.cpp:20-34`）：对 `DSH_BIN` 调 `isTrustedRootExecutable`（同 `Updater.cpp:121-127`）或至少 `QFile::ownerId()==0 && !perms&WriteOther`；PATH 解析的 `dsh` 同样要求 root owned + non-world-writable。
2. **【中】** `DshDesktopApp::ensureBackendStarted`（`DshDesktopApp.cpp:677-705`）：把 `pkexec`/`npm` 改硬编码 `/usr/bin/pkexec` + `isTrustedRootExecutable`，与 `Updater` 对齐。
3. **【中】** `packaging/dsh-theme-export.service` 增加 systemd 强化：`RestrictAddressFamilies=AF_UNIX AF_NETLINK`、`SystemCallArchitectures=native`、`LockPersonality=yes`、`ProtectKernelModules=yes`、`ProtectControlGroups=yes`、`CapabilityBoundingSet=`。
4. **【中】** `packaging/dsh-theme-export` 对 `DSH_THEME_OUTPUT` 加白名单：只允许 `/run/dsh-desktop/theme` 或 `${TMPDIR:-/tmp}/...`；或干脆不读 `DSH_THEME_OUTPUT`，直接 hard-code `/run/dsh-desktop/theme`。
5. **【中】** `DshDesktopApp::singleInstanceSocketPath`（`DshDesktopApp.cpp:251-264`）：在 `removeServer` 前先 `QFileInfo(sockPath).isSymLink()` 拒绝 + `info.canonicalFilePath() == sockPath` 一致性。
6. **【中】** `scripts/check-dsh-profile.sh:84-97`：对 `detected_host` 做白名单（loopback / RFC1918 / 已知 host）或加 `--connect-to`，避免恶意 unit ExecStart 把 curl 探测指向内部 SSRF 目标。
7. **【中】** `UninstallerMain.cpp:222-225`：对 `--root` 做"必须存在且是目录" + "必须与 `--prefix` 同前缀或显式标注 SANDBOX 前缀"。
8. **【低】** `SupervisedBackend::start`（`SupervisedBackend.cpp:94-99`）：`DSH_DESKTOP_PORT` 做 `toInt(&ok, 10) && port > 0 && port < 65536` 校验。
9. **【低】** `HttpProbe.cpp:16`：`curl` 路径硬编码 `/usr/bin/curl`（同 `Updater` 风格）。
10. **【低】** `ServiceUnitBuilder.cpp:101-105`：`host` 加正则白名单 `^[A-Za-z0-9.\-:[\]]+$`。
11. **【低】** `DshServiceManager::cancel`（行 280-287）：考虑 `kill(-pid, SIGTERM)` 杀进程组（与 `SupervisedBackend::stop` 一致）。
12. **【低】** `Updater::performUpdate` / `DshDesktopApp::ensureBackendStarted`：合并 npm 输出到日志前对 `Bearer/Authorization/NPM_TOKEN` 等做 redacted replace。
13. **【低】** `Logger.cpp`：文件权限设 `0600`。
14. **【低】** `DesktopUpdateHelper::recoverOrphanedDshUpdateFiles`（`DesktopUpdateHelper.cpp:301-361`）：在 `QFileInfo(originalPath).exists()` 与 `std::rename` 之间用 `O_NOFOLLOW`/`openat` 二次校验（race window 极小，但 C++ 没有原子方案）。
15. **【低】** `DshDesktopApp::onClearDownloads`（行 1018-1024）：用 `QDir::removeRecursively` + 文件名白名单，避免 symlink 攻击。
16. **【低】** `tests/test_profile_checker.sh`：用 `env -i PATH=/usr/bin:/bin bash …` 启动 checker 进程，彻底隔离开发环境注入。

---

## 12. 总体评价

- **架构**良好：所有外部进程派生都走 `QProcess::setProgram/setArguments` 显式 argv，**没有 shell 拼接面**；所有路径/host/port/unit 名都有白名单或白名单式校验；提权路径走 `pkexec --disable-internal-agent` + `sudo -n NOPASSWD`；root+WebEngine 默认拒绝；service unit 拼装全程转义。
- **新拆分**的 `RunSyncProcess`/`SystemctlCommandBuilder`/`ShowFailure`/`SyncHttp`/`HttpProbe`/`Sha256` 都已落地，且都对应单测。
- **主要剩余风险**集中在 `SupervisedBackend::DSH_BIN`（高）与 `dsh-theme-export.service` 的环境变量注入面（中），以及若干 SSRF/路径穿越的低危面（已在 §11 列出建议）。
- 本次未暂存 + 已存在的 30 个改动文件未引入新的注入或权限绕过；强化方向是"显式拒绝 + 提权必经 polkit/root owned"。

---

*报告基于静态阅读 `src/`、`scripts/`、`packaging/`、`tests/`、`CHANGELOG.md`、`README.zh.md`、`docs/DEPENDENCIES.md`；未执行编译、未运行测试、未联网。所有路径与行号对应仓库 HEAD（commit 9ca2d83 + 工作区未提交改动）。*
