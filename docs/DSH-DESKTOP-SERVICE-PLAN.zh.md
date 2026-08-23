# DSH Desktop 与官方 DSH 后台服务统一方案

- 文档状态：待用户确认
- 适用项目：`DSH-Desktop`
- 目标平台：Linux / KDE Plasma 6（保留无 systemd 环境兜底）
- 本文性质：需求理解、架构设计、实施边界与验收标准
- 重要约束：用户确认前只审核和修订本文，不修改业务代码、不执行安装器、不改变系统服务

## 0. 实现状态与范围（重要）

本文是**设计方案**，不是现状文档；其描述的"统一服务管理"大多数仍是目标状态，
尚未全部落地到当前代码。请据此区分"已实现"与"规划中"：

**已实现（当前运行时代码）：**

- 只读服务发现与选择（`ServiceDiscovery` + `SystemctlShowParser`）：`LoadState=loaded`
  且 `ExecStart` 调用官方 `dsh web` 才复用；两者都有效时优先当前用户的用户级 unit；
  `ServiceOwnership` 记录由桌面端补齐的 unit 归属；`applyServiceMetadata` 派生
  scope/state/owner/failureReason。
- 对 inactive/failed 既有官方服务的**运行时就地授权**（`requiresStartConfirmation`）。
- 后端三种形态：`SystemdBackend` / `SupervisedBackend` / 外部远程模式。
- 统一后端 + 桌面更新（`UpdatePlan` / `UpdateDialog` / `DesktopVersionChecker` /
  `DesktopReleaseDownloader` / `dsh-desktop-updater`）。
- 托盘 `DSH 后台服务` 分组（启动 / 重启 / 停止，按可管理性启用）。

**规划中（模型/测试已就绪，或仍停留在设计）：**

- `InstallationPlan`（检测—复用—补齐的确定性决策模型）与其单元测试：决策模型已存在，
  尚未驱动安装器或运行 UI。
- `ServiceUnitBuilder`（生成标准 `dsh-web.service` 单元文本）与其单元测试：尚未用于真实补齐。
- 安装阶段真正"补齐"一个新的 `dsh-web.service`（由桌面端创建并记录归属）：
  `install.sh` 目前仍是基础检测 + `enable --now`，不创建新 unit。
- 更完整的统一服务管理界面（服务日志、配置摘要、安装期授权流程、卸载"同时移除后台
  服务"复选框）。
- 第 2–10 节的安装、退出、卸载、更新编排细节，大部分属于目标设计，需用户确认后实施。

因此下文既有"当前已实现"的描述，也有"目标应实现"的要求，请结合本节判断。

## 1. 产品定位

DSH Desktop 不是把 DSH Web 页面重新打包成一个独立应用，也不维护第二套 DSH 后台。它是官方 DSH 后台服务的原生桌面管理端：

```text
DSH Desktop（Qt/KDE 原生界面）
        |
        v
统一的 dsh-web.service（官方 DSH 服务）
        |
        v
官方 /usr/bin/dsh web
        |
        v
官方 @deepseek-ai/dsh + 原有配置和数据
```

桌面端负责窗口、托盘、服务生命周期、版本检查、升级编排和用户反馈；业务运行时、会话数据和 Web API 继续来自官方 DSH。

## 2. 安装场景与统一目标

### 2.1 已安装官方 DSH，服务正在运行

安装桌面端时检测官方 `dsh` 命令、npm 包版本和实际路径，检测 systemd 系统级或用户级 `dsh-web.service`，读取真实运行用户、工作目录、环境文件、端口和 `DSH_HOME`。不重新安装 DSH，不覆盖服务文件，不迁移数据，不覆盖环境配置；只安装桌面端自身的二进制、菜单、图标、自启动和辅助组件。

### 2.2 已安装官方 DSH，但服务没有启动

这是独立于“未安装 DSH”的状态：`已安装 CLI + 已存在 service + 服务 inactive/failed`。

安装阶段和首次启动阶段必须：

1. 保留已有安装和服务配置，不因服务未启动而重新安装 npm 包。
2. 原生显示“已找到官方 DSH，但后台服务当前未运行”，并明确询问用户是否启动；用户拒绝时保持停止状态，只读打开服务管理界面。
3. 只有 unit 已通过第 3 节的官方入口、加载状态和归属验证，并且用户明确授权后，才通过服务管理器启动已有 unit；不得直接执行第二个 `dsh web`。
4. 服务启动、重启和健康检查统一使用忙碌（不定量）进度条；不得伪造百分比。
5. 启动成功后连接已有服务。
6. 启动失败时不创建重复进程、不覆盖配置，并提供状态原因、日志、重试和修复建议。

失败原因必须来自 `systemctl show` 的 `LoadState`、`ActiveState`、`SubState`、退出码等字段，并在可用时提供 `journalctl -u dsh-web.service` 的最近日志摘要。

桌面端首次启动时的启动页显示“正在启动 DSH 后台服务”，完成端口和服务健康检查后才进入页面；失败时显示“后台服务未能启动”，提供“重试”“查看日志”“继续打开服务地址”等操作。

### 2.3 未安装官方 DSH

安装桌面端时从官方 npm 源安装 `@deepseek-ai/dsh`，使用官方 `dsh web` 命令创建标准化 `dsh-web.service`。默认优先创建当前用户的用户级 unit：`~/.config/systemd/user/dsh-web.service`，通过 `systemctl --user enable --now` 启用并由普通用户运行，无需让 DSH 以 root 运行。只有用户明确选择“共享给本机其它用户”时，才创建系统级 unit，并在安装阶段经 root/polkit 写入 `User=<当前用户>`。

初始化默认工作目录、`DSH_HOME` 和端口时不得覆盖已有目录。服务启动并健康检查成功后启动桌面端。安装完成后必须与 2.1 完全使用同一套服务管理接口。`SupervisedBackend` 只作为 systemd 或用户总线不可用时的兼容兜底，不作为正常安装路径。

### 2.4 已安装 CLI 但没有 service

保留官方 CLI 和版本，不重复下载相同或更低版本；明确提示将为现有 CLI 创建标准 `dsh-web.service`。创建前备份将生成的配置，避免覆盖已有同名非 DSH 文件；创建 scope 沿用 2.3：默认当前用户的用户级 unit，只有明确选择共享时才创建系统级 unit。启动并健康检查后记录服务来源和创建时间。

### 2.5 每次启动的版本检查

每次 DSH Desktop 启动时，在后台异步检查两个组件：

1. **官方 DSH 后台服务版本**：读取实际被 service 使用的 `dsh` CLI/npm 包版本，并与官方 npm 注册表的稳定版本比较。
2. **DSH Desktop 版本**：读取当前桌面端版本，并以项目 Gitee 仓库为唯一主要来源：
   `https://gitee.com/eruditeLoong/dsh-desktop-qt`

版本检查不能阻塞桌面端进入已可用的 DSH 页面。启动顺序为：

```text
启动桌面端
    → 先探测并连接 DSH 后台服务
    → 后台异步检查官方 DSH 版本
    → 后台异步检查 Gitee 上的桌面端版本
    → 合并显示两个组件的更新状态
```

如果后台服务未启动，必须先按 2.2 的授权和启动流程处理；版本检查不能绕过服务归属验证，也不能因为版本检查失败而覆盖或重新安装已有 DSH。

版本检查结果应分别显示：

```text
DSH Desktop：当前版本 / Gitee 最新版本 / 是否可更新
DSH 后台服务：当前版本 / npm 最新版本 / 是否可更新
```

网络失败时保留当前服务和桌面端正常运行，只显示“暂时无法检查更新”，不得把网络错误误报为“没有更新”。

## 3. 服务发现与归属模型

服务发现不能只检查文件是否存在，还必须检查 unit 是否可加载，以及 `systemctl show <unit> --property=LoadState,ActiveState,SubState,ExecStart,User,Environment,EnvironmentFiles` 的结果。只有 `LoadState=loaded` 且 `ExecStart` 确认调用官方 `dsh web` 或兼容官方入口时，才能进入“复用”分支；否则记录原因并转入“补齐 unit”或“只读不可管理”分支，绝不盲目启动第二个 `dsh web`。

必须解析或可靠探测：

- systemd 系统级/用户级范围。
- 服务所有者；当前用户级 unit 之外的用户服务标记为 `Unmanaged`，只读展示。
- 运行用户、工作目录、环境文件和 `DSH_HOME`。
- 实际监听地址和端口；解析失败时才回退 `127.0.0.1:3080`，并记录 `fallback-to-default-port`。
- 当前状态：`active`、`inactive`、`failed`、`activating`、`unknown`。
- `ActiveState`、`SubState`、最近退出码和可用 journal 摘要。

服务来源记录为：`ExistingOfficial`、`ProvisionedByDesktop`、`SupervisedFallback`、`External`；范围记录为 `ServiceScope{System,User}`。建议后端模式为：

```text
ExistingSystemService
ExistingUserService
ProvisionedSystemService
ProvisionedUserService
SupervisedFallback
External
Unmanaged
```

“已有”和“由桌面端补齐”用于提示、卸载和权限策略；实际启动、停止、重启和状态查询统一由 `DshServiceManager` 完成。`DSH_HOME` 以已发现 service 的 `EnvironmentFile`/环境为准；无 service 时使用用户显式设置或官方 `dsh web` 默认值，避免 fallback 注入不一致的数据目录。

## 4. 安装器：检测—复用—补齐—验证

### 检测

显示官方 DSH 是否安装、版本、路径、service 是否存在及运行状态、运行用户、端口、数据目录、权限需求和活动任务。

### 复用

已有官方服务时只做只读探测：不执行无必要的 `npm install`，不写入已有 unit 或环境文件，不改变已有端口和数据目录，也不在安装阶段无条件执行 `systemctl enable --now`。只有用户明确授权“启动/启用已有服务”，且 unit 已验证为官方 DSH 时，才执行对应操作。

### 补齐

缺少 CLI 时安装官方包；缺少 service 时只创建由 DSH Desktop 明确拥有的新 unit，并记录归属，卸载时避免误删用户原有服务。

### 验证

安装后依次验证 CLI 可执行、版本可读取、service 可加载、服务状态、HTTP 健康检查、实际 URL 连接和已有数据可读取。

失败时不删除已有 DSH，不停止原本运行中的服务（除非用户明确授权且步骤确实需要），不继续执行依赖失败步骤，并显示失败阶段、日志和恢复建议。

## 5. 后台服务管理

托盘和设置界面明确分组：

### 更新按钮显示规则

每次启动后的双版本检查完成后，只要任一组件存在新版本，托盘统一显示一个按钮：

```text
更新到最新版
```

按钮不根据组件数量改变名称，避免用户记忆多个更新入口。点击后由原生更新对话框根据检查结果自动决定更新范围：

- 只有 DSH 后台有新版本：更新官方 DSH 后台服务。
- 只有 DSH Desktop 有新版本：更新 DSH Desktop。
- 两者都有新版本：先更新 DSH 后台服务并完成健康检查，再更新 DSH Desktop。
- 两者都没有新版本：隐藏按钮，或显示不可点击的“已是最新版本”状态。
- 网络失败、版本解析失败或来源校验失败：不显示可执行更新按钮，只显示“无法检查更新”，避免把未知状态当成可更新。

点击“更新到最新版”后，原生对话框必须明确显示本次更新范围，例如：

```text
本次将更新：
☑ DSH 后台服务       0.1.1 → 0.1.2
☑ DSH Desktop         0.1.0 → 0.1.1
```

如果用户只想更新其中一个组件，应在对话框中提供组件选择；默认勾选所有检测到有更新的组件。更新动作必须携带检查时锁定的目标版本，不能点击后重新使用漂移的 `latest`。按钮也可在用户手动执行“检查更新”后动态出现，并通过 KDE 通知提示新版本已发现。

### DSH Desktop

显示/隐藏桌面、重启桌面端、更新桌面端、桌面端日志、退出桌面端。

### DSH 后台服务

显示运行中/已停止/启动中/失败/不可管理状态，提供启动、停止、重启、服务日志、后台版本检查、官方 DSH 更新和配置摘要。

停止或重启已有官方服务时显示：

```text
这是系统中原有的 DSH 后台服务。
停止或重启可能影响其他终端、脚本或远程访问。
```

退出桌面端默认不停止后台服务；退出对话框必须提供明确的服务处理选项：

- “只退出 DSH Desktop，保持 DSH 后台服务运行”（默认）。
- “退出 DSH Desktop，并停止 DSH 后台服务”。

如果服务是已有官网服务，第二项必须显示影响范围并要求二次确认；如果服务由桌面端补齐，则显示服务来源和停止后可再次启动的说明。服务属于其他用户或标记为 `Unmanaged` 时，不显示可执行停止选项，只允许退出桌面端。

卸载桌面端也必须提供明确的后台服务处理复选框，默认不勾选：

```text
[ ] 同时卸载 DSH 后台服务
```

未勾选时：

- 仅卸载 DSH Desktop。
- 保留 DSH 后台 service、CLI、配置和业务数据。
- 如果后台正在运行，保持其继续运行。

勾选时：

- 对 `ProvisionedByDesktop`：停止、禁用并移除由桌面端创建的 DSH 后台 service；是否移除官方 CLI 由后续明确的二级选项决定。
- 对 `ExistingOfficial`：显示高风险提示，要求二次确认后才允许停止/禁用已有 service，并卸载官方 DSH CLI；配置和业务数据默认保留，数据删除必须是另一个独立且再次确认的选项。
- 对 `Unmanaged` 或无权管理的其他用户服务：复选框置灰，并说明只能手动处理，桌面端不会越权操作。

原本由官网安装的 service、CLI、环境文件和数据不得被卸载流程默认移除；只有用户勾选复选框、完成二次确认、确认拥有相应权限并且归属/目标校验通过时，才允许执行。卸载流程结束前应显示桌面端和后台服务的最终状态，避免用户误以为“退出桌面端”已经停止或删除后台 DSH。

## 6. 更新模型

更新分为 `DSH Desktop` 和 `官方 DSH 后台服务（@deepseek-ai/dsh）` 两个组件。检查更新时分别展示当前版本、目标版本、来源和更新内容，不能只显示“更新应用”。

### 后台更新

```text
定位服务实际使用的 dsh 和 npm 前缀
    → 记录旧版本 old
    → 确认活动任务和服务状态
    → 提示维护影响
    → 锁定目标版本 target
    → 通过匹配前缀的权限方式更新官方 npm 包
    → 验证 dsh --version 等于 target
    → 重启 dsh-web.service
    → HTTP/API 健康检查
    → 成功则记录 target 和时间
```

如果任一步失败，只有在 npm 包或服务已经发生变更后，才必须按同一 npm 前缀重新安装 `old`，再次重启并健康检查；变更前失败直接结束并保留原状态。回滚仍失败时保留失败现场，输出日志和人工恢复命令。用户级 npm 前缀不使用 polkit，系统级前缀才使用 `pkexec`。版本验证必须与锁定的 `target` 精确匹配，不能只判断“能读到版本”。桌面端和后台不得并行更新，必须先完成后台更新并恢复健康，再进入桌面端更新阶段。活动任务存在时默认警告，不静默停止服务。后台更新成功而桌面端更新失败时，保留后台新版本并明确显示“组件版本暂时不一致”，不得自动降级健康的后台服务。

### 桌面端更新

桌面端更新源固定以项目 Gitee 仓库为主：`https://gitee.com/eruditeLoong/dsh-desktop-qt`。发布版本、下载包、校验和及更新说明必须来自该仓库的正式 Release 或项目约定的受信发布目录；不能把 npm 注册表当作桌面端更新源。

不能在运行进程中直接覆盖自身，必须使用独立 updater/helper：下载临时包、校验 HTTPS/版本/校验和、请求桌面端退出、helper 原子替换、失败保留旧版本、重新启动并验证。桌面端与后台不得并行更新，桌面端更新前必须先确认后台服务更新阶段已完成或明确跳过。

## 7. 原生进度和用户反馈

安装 npm 包、服务启停、端口等待、健康检查、桌面端包下载、校验和替换等操作必须异步执行，不能阻塞 Qt GUI 线程。

进度类型按操作固定：npm 安装/更新、服务启动、服务重启和健康检查一律使用忙碌（不定量）进度条；只有存在可靠总量时（例如可从 `Content-Length` 得知的桌面端包下载，或明确的均匀子阶段计数）才使用确定进度，禁止对不定量操作显示百分比。

统一原生操作对话框包含阶段标题、当前说明、`QProgressBar`、完成步骤、日志摘要、可展开详细日志、失败后的重试/日志/关闭按钮；操作期间禁止误关闭。更新对话框提供“开始前中止本次更新”，仅在任何安装或服务变更开始前可用；一旦进入 npm 安装或服务重启，按钮置灰并显示“等待当前阶段完成”，不得静默丢弃后台操作。桌面端进入自替换阶段时，先保存阶段结果并由 helper 接管，桌面端窗口按预期退出；helper 完成后重新启动并恢复结果提示。

## 8. 权限、安全与数据保护

普通用户运行官方 DSH。用户级 unit 一律通过 `systemctl --user` 管理，不提权；系统级 unit 的 `start/stop/restart` 经 `/usr/bin/pkexec`/polkit 授权，`enable/disable/daemon-reload` 只允许在 root 安装阶段执行，桌面端运行时不静默执行。服务所有者必须明确：桌面端只管理当前用户所属的用户级 unit，或经授权管理系统级 unit；属于其他用户的用户级 unit（例如 xrdp 下的其他会话）标记为 `Unmanaged` 并只读展示。

禁止将用户输入直接拼接 shell 命令；校验路径、可执行文件和版本号；已有配置只读探测，除非用户明确执行迁移或修复；更新保留旧版本恢复路径；桌面端退出不默认停止后台；远程 URL 模式不管理远程服务生命周期。

## 9. 卸载规则

桌面端在创建 unit 时，必须在 DSH 自有状态文件（例如 `~/.local/state/dsh-desktop/services.json`）记录 unit 名称、scope、创建时间和 `ExecStart` 指纹。卸载 DSH Desktop 只删除桌面端二进制、桌面条目、图标、自启动和桌面端辅助服务；保留官方 CLI、npm 包、官网原有 `dsh-web.service`、环境文件和数据。

对记录为 `ProvisionedByDesktop` 的 service，卸载时用原生确认让用户选择“保留后台服务”或“同时移除后台服务”；`install.sh --uninstall` 或等价卸载器必须支持这一选择。对原本存在的官网 service 绝不自动删除，只有用户显式勾选并二次确认才允许移除。用户配置、日志、WebEngine 数据和下载文件默认保留。

## 10. 测试与验收标准

### 安装与启动
- 检查完成后按后台/桌面端更新状态动态显示或隐藏对应托盘更新按钮。
- 退出对话框默认保持后台运行，并提供明确的“同时停止后台服务”选项。
- 卸载对话框默认不勾选“同时卸载 DSH 后台服务”；勾选后按服务归属、权限和二次确认执行。
- 桌面端版本检查只使用指定 Gitee 仓库，后台版本检查使用官方 npm 源。
- CLI + inactive service：只读识别后，经用户授权启动已有服务并连接。
- CLI + failed service：显示 `ActiveState/SubState` 和 journal 摘要，不创建第二进程。
- CLI + 无 service 且 systemd 可用：创建匹配 scope 的标准 service 并启动。
- systemd 或用户总线不可用：明确显示 fallback 原因后才使用 supervised 子进程。
- 无 CLI：安装官方包、创建 service 并启动。
- 远程 URL：只连接，不管理生命周期。
- 非 3080 端口：读取真实端口并连接。

### 管理

- 启动、停止、重启状态正确。
- 已有服务和桌面端补齐服务提示不同。
- 桌面端退出默认不停止后台。
- 活动任务停止有警告。
- 服务日志可查看。

### 更新

- 后台和桌面端版本分别显示。
- 两者同时更新时分阶段显示。
- npm 失败、权限拒绝、网络超时、服务重启失败均可识别。
- 失败后旧服务和桌面端可恢复。
- 更新期间窗口不能误关闭。
- 不伪造不可计算阶段百分比。

### 回归

- 不改变 Fcitx/KDE 输入法环境。
- 不改变已有 `DSH_HOME` 数据。
- 不覆盖官网服务环境变量。
- 不影响现有服务自动重启和日志。
- xrdp/X11 软件渲染环境下 Qt WebEngine 可启动。

## 11. 现有代码到目标方案

| 当前代码 | 目标调整 |
| --- | --- |
| `src/backend/Backend.cpp` | 完整服务发现结果、scope、来源和 fallback 调度 |
| `src/backend/SystemdBackend.cpp` | `systemctl show`、真实 URL、journal、状态和权限管理 |
| `src/backend/SupervisedBackend.cpp` | systemd 不可用时的兜底 |
| `src/updater/Updater.cpp` | 拆分后台/桌面更新，增加阶段信号 |
| `src/app/UpdateDialog.cpp` | 统一原生更新流程和进度 UI |
| `src/app/TrayController.cpp` | 增加后台服务管理分组 |
| `src/app/ExitDialog.cpp` | 区分桌面端退出和后台停止 |
| `packaging/install.sh` | 检测、补齐 service、启动等待、失败回滚 |
| `tests/` | 服务发现、状态、安装决策、更新编排测试 |

## 12. 实施顺序

用户确认本文后才实施：

1. 只读服务/安装环境探测。
2. 已有 inactive/failed 服务的启动和反馈。
3. 缺少 service 时安装官方 DSH 并补齐 service。
4. 统一后台服务管理界面。
5. 后台更新流程和原生进度对话框。
6. 确认 Gitee 发布包格式、校验和和独立 updater/helper 并实现。
7. 测试、安装验证、回滚验证和文档。

用户确认前不进入业务代码改造；所有已有官网 DSH 的配置和数据必须保持不变。

## 13. 默认决策待确认

如用户没有另行指定，实施时采用：

1. 无官网服务时优先创建当前用户的用户级 `~/.config/systemd/user/dsh-web.service`，由普通用户运行；只有明确选择共享给本机其他用户时才创建系统级 unit，并在安装阶段显式设置 `User=<当前用户>`。
2. 已有服务可以由桌面端启动、停止和重启，但仅限当前用户所属的用户级 unit，或经 polkit 授权的系统级 unit；其他用户的服务标记为只读不可管理，并明确显示影响范围。
3. 桌面端退出默认不停止后台服务。
4. 官方 DSH 后台从 npm 官方源更新。
5. 桌面端自身从指定 Gitee 仓库的正式 Release 或受信发布目录更新，并使用独立 helper。
6. 服务和桌面端分开显示版本、状态、进度和错误。
7. 用户确认本文前不进入业务代码改造。
