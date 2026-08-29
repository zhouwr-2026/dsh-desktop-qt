# DSH Desktop 与原生 DSH 兼容性审核报告

## 核心结论

**✅ 兼容设计完整**：本项目的包装机制在设计上**完全尊重原生 DSH 服务**，遵循"只读检测、不覆盖、需授权"原则。

---

## 一、服务发现与归属判定机制

### 1.1 官方入口校验 (`SystemctlShowParser.cpp:185-194`)

```cpp
bool invokesOfficialDshWeb(const QString& execStart) {
    const QStringList argv = parseExecStartArgv(execStart);
    if (argv.size() < 2) return false;
    const QString executable = QFileInfo(argv.at(0)).fileName();
    if (executable != QLatin1String("dsh")) return false;
    if (argv.at(1) == QLatin1String("web")) return true;
    return argv.at(1) == QLatin1String("--profile")
        && argv.size() >= 3
        && argv.at(2) == QLatin1String("web");
}
```

**判定逻辑**：
- `ExecStart` 必须以 `dsh` 为命令名（basename）
- 子命令必须是 `web` 或 `--profile web`
- **拒绝**非官方入口（如自定义脚本、错误路径）

### 1.2 服务发现流程 (`ServiceDiscovery.cpp:129-157`)

```
discoverDshWebService("dsh-web.service")
  ├── systemctl --system show dsh-web.service  (系统域)
  └── systemctl --user show dsh-web.service    (用户域)
      └── validateCandidate(ServiceInfo)
          ├── LoadState != "loaded" → 拒绝
          ├── invokesOfficialDshWeb == false → 拒绝 (ForeignExecStart)
          └── 通过 → 有效候选
```

**优先级**：用户级优先于系统级（文档化偏好）

### 1.3 归属判定 (`ServiceOwnership.cpp`)

| 来源 | 判定条件 | 行为 |
| --- | --- | --- |
| `ExistingOfficial` | ExecStart 官方 + 无归属记录 | 只读复用，需用户确认启动 |
| `ProvisionedByDesktop` | ExecStart 官方 + 有归属记录 | 桌面端可管理 |
| `Foreign` | ExecStart 非官方 | 拒绝管理，原样保留 |
| `Unmanaged` | 用户级但用户不匹配 | 拒绝管理 |

---

## 二、安装脚本行为分析

### 2.1 `packaging/install.sh` (仅只读探测)

```bash
configure_dsh_service() {
  # 只读探测 dsh-web.service
  loadstate=$(systemctl show --property=LoadState --value dsh-web.service)
  execstart=$(systemctl show --property=ExecStart --value dsh-web.service)
  
  if [[ "$loadstate" == "loaded" ]] && is_official_dsh_web "$execstart"; then
    case "$activestate" in
      active|activating|reloading)
        log "检测到官方 dsh-web.service 正在运行：直接复用，不再重复启动"
        ;;
      *)
        warn "检测到官方 dsh-web.service 存在但当前未运行；本脚本不擅自启动"
        ;;
    esac
  else
    warn "检测到 dsh-web.service 存在，但其配置并非官方 'dsh web'；视为外部/不受管单元"
  fi
}
```

**关键声明**（install.sh:18）：
> "本脚本绝不启停/启用/禁用任何已存在的 DSH 服务。"

### 2.2 `packaging/post-install.sh` (仅安装桌面端组件)

| 文件 | 路径 | 说明 |
| --- | --- | --- |
| `dsh-theme-export.service` | `/usr/lib/systemd/system/` | 主题同步辅助服务 |
| `dsh-theme-export.path` | `/usr/lib/systemd/system/` | 主题变化触发路径 |
| `dsh-desktop.desktop` | `/etc/xdg/autostart/` | 桌面自启动条目 |

**不安装**：`dsh-web.service`（完全尊重已有配置）

---

## 三、运行时服务管理逻辑

### 3.1 启动前确认机制 (`DshServiceManager.cpp:312-318`)

```cpp
if (!validated_) {
    rejectOperation(..., ServiceResult::NotValidated,
        "目标未通过官方验证与归属校验；请先 requestDiscovery()");
    return -1;
}
```

**只有 discovery 验证通过的服务才能被操作**。

### 3.2 安装决策模型 (`InstallationPlan.cpp:56-109`)

| 场景 | 行为 |
| --- | --- |
| `Active` 官方服务 | `ReuseExistingService` — 直接复用，不启动 |
| `Inactive/Failed` 官方服务 | `StartExistingServiceWithConsent` — 需用户确认 |
| `Foreign/Unmanaged` 服务 | `DoNothing` + `blocked=true` — 只读，不可管 |
| `Missing` 服务 | `InstallOfficialDsh` + `ProvisionService` — 才创建新 unit |
| systemd 不可用 | `UseSupervisedFallback` — 子进程兜底 |

---

## 四、卸载安全分析

### 4.1 卸载决策 (`Uninstaller.cpp:135-139`)

```cpp
bool Uninstaller::backendOwnedByDesktop(const UninstallContext& context) {
    return context.backendDetected
        && context.origin == ServiceOrigin::ProvisionedByDesktop
        && context.ownershipConsistency == ConsistencyResult::Match;
}
```

**只有 `ProvisionedByDesktop` 的服务才会被卸载**。

### 4.2 卸载范围 (`UninstallPlan.cpp:86`)

> "规则 4：勾选 + 确认，但后台为 ExistingOfficial / Foreign / Unmanaged / 
> 未通过一致性检查时，禁止移除后台 unit。"

---

## 五、配置文件与数据隔离

### 5.1 数据目录隔离

| 组件 | 路径 | 归属 |
| --- | --- | --- |
| 桌面端配置 | `~/.config/anywhere-labs/dsh-desktop.ini` | 桌面端私有 |
| 桌面端下载缓存 | `~/.local/share/dsh-desktop/downloads/` | 桌面端私有 |
| 桌面端状态文件 | `~/.local/state/dsh-desktop/services-owned.json` | 桌面端私有 |
| **DSH 后端数据** | **`$DSH_HOME`** | **原生 DSH 所有** |

### 5.2 服务单元隔离

| 单元文件 | 路径 | 来源 |
| --- | --- | --- |
| `dsh-web.service` | 由原生 DSH 安装（不在本项目控制范围） | 原生 DSH |
| `dsh-theme-export.service` | `/usr/lib/systemd/system/` | 桌面端安装 |
| `dsh-theme-export.path` | `/usr/lib/systemd/system/` | 桌面端安装 |
| `dsh-desktop.desktop` | `/etc/xdg/autostart/` | 桌面端安装 |

---

## 六、兼容性保障清单

### ✅ 已保障的兼容性

| 场景 | 保障机制 | 结果 |
| --- | --- | --- |
| 用户已有 Active 官方服务 | `InstallationPlan::make()` 返回 `ReuseExistingService` | 不修改、不重启 |
| 用户已有 Inactive 官方服务 | 需用户确认才启动 | 不强制启动 |
| 用户已有 Failed 官方服务 | 需用户确认才启动 | 不强制修复 |
| 用户已有非官方 service | `Foreign` 标记，拒绝管理 | 原样保留 |
| 用户无 DSH 服务 | 才创建新的 `dsh-web.service` | 全新创建 |
| 卸载桌面端 | 只删除 `ProvisionedByDesktop` 的服务 | 原生服务保留 |

### ⚠️ 需要注意的场景

| 场景 | 风险 | 缓解措施 |
| --- | --- | --- |
| 原生 DSH 更新 ExecStart | 归属指纹不匹配 | `ConsistencyResult::Mismatch` 标记，不自动更新 |
| 多用户环境 | 系统级 unit 可能冲突 | 默认用户级优先，系统级需显式选择 |
| 手动修改 service 文件 | 可能被桌面端误判为 Foreign | 严格校验 ExecStart 格式 |

---

## 七、测试验证建议

### 7.1 兼容性测试用例

```bash
# 场景 1：已有 Active 官方服务
sudo systemctl status dsh-web.service
sudo packaging/install.sh
# 期望：日志显示 "直接复用，不再重复启动"

# 场景 2：已有 Inactive 官方服务
sudo systemctl disable --now dsh-web.service
sudo packaging/install.sh
# 期望：日志显示 "本脚本不擅自启动"

# 场景 3：非官方 service
echo '[Unit]
Description=Custom DSH
[Service]
ExecStart=/usr/bin/echo hello' | sudo tee /etc/systemd/system/dsh-web.service
sudo systemctl daemon-reload
sudo packaging/install.sh
# 期望：日志显示 "视为外部/不受管单元"

# 场景 4：无 DSH 服务（全新安装）
sudo rm -f /etc/systemd/system/dsh-web.service
sudo packaging/install.sh
# 期望：创建新的 dsh-web.service
```

### 7.2 关键代码路径

| 文件 | 关键函数 | 测试要点 |
| --- | --- | --- |
| `ServiceDiscovery.cpp` | `validateCandidate()` | ExecStart 格式校验 |
| `InstallationPlan.cpp` | `make()` | 各 ServiceState 分支 |
| `Uninstaller.cpp` | `backendOwnedByDesktop()` | 归属判定 |
| `install.sh` | `configure_dsh_service()` | 只读探测 |

---

## 八、总结

### 架构设计评价

| 维度 | 评分 | 说明 |
| --- | --- | --- |
| 兼容性保障 | ⭐⭐⭐⭐⭐ | 严格的归属判定和只读原则 |
| 数据安全 | ⭐⭐⭐⭐⭐ | DSH_HOME 与桌面数据完全隔离 |
| 卸载安全 | ⭐⭐⭐⭐⭐ | 只删除 Own 服务，不误删原生 |
| 用户透明 | ⭐⭐⭐⭐ | 需要确认时明确告知 |
| 边界处理 | ⭐⭐⭐⭐ | Foreign/Unmanaged 正确处理 |

### 最终结论

**本项目包装机制与原生 DSH 配置完全兼容**。核心设计原则：

1. **只读检测**：安装阶段只探测，不修改
2. **官方校验**：只有 `ExecStart` 调用 `dsh web` 才认为是官方服务
3. **归属记录**：通过 SHA256 指纹区分"原生"和"桌面端创建"的服务
4. **用户授权**：启动/停止/重启需用户确认
5. **安全卸载**：只删除 Own 服务，保留原生配置和数据

**建议**：在正式发布前，应在以下环境实测：
- Arch Linux + KDE Plasma 6（目标平台）
- 已有 npm 全局安装 `@deepseek-ai/dsh` 的环境
- 已有 systemd `dsh-web.service` 的环境
