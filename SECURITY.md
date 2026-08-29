# 安全策略

本项目严肃对待安全问题,感谢任何负责任的披露。

## 支持的版本

下表列出本项目接收安全更新与披露的版本范围:

| 版本 | 是否支持 |
| --- | --- |
| 0.1.x | ✅ |
| < 0.1.0 | ❌(已发版前的预发布版本不接收独立 CVE 跟踪;请升级到 0.1.x) |
| Git `master` 分支 | ✅(等同下一个补丁版本) |

## 报告漏洞

**不要**通过 GitHub Issues、Discussions 或 Pull Request 公开报告安全漏洞。

请通过以下任一方式私下联系维护团队:

- **首选**:GitHub Security Advisories 的 "Report a vulnerability" 入口(对应仓库 Security tab)
- **备选**:邮件至 `security@dsh-desktop.example.invalid`(替换为实际邮箱;GPG 公钥见下文)

### 报告请尽量提供

1. 漏洞标题与一句话描述
2. 复现步骤(含最小化的演示命令或测试用例)
3. 受影响版本(commit hash 或 `dsh-desktop --version`)
4. 预期行为 vs 实际行为
5. 环境信息:
   - 发行版与版本
   - Qt 版本(`qmake6 -query QT_VERSION`)
   - 是否 root 运行、是否使用 systemd
6. 概念验证 PoC(可选但有助于加速复现)
7. 是否已公开披露(CVE 编号、博客等)

我们会在 **72 小时内**确认收到并初步分级(1–7 工作日出 CVE 评估)。分级后:

| 级别 | 处理窗口 |
| --- | --- |
| Critical | 7 天内发布补丁 |
| High | 14 天 |
| Medium | 30 天 |
| Low | 下一次常规发布 |

## 已知威胁模型

DSH Desktop 是一个 Qt 6 / C++17 桌面包装器,处理来自 npm 注册表与 Gitee API 的内容,并通过 D-Bus / systemd 与 Linux 桌面交互。当前明确的威胁模型边界:

- **不信任的网络输入**:npm 注册表响应、Gitee release JSON、桌面端用户键入 URL
- **半信任的本地输入**:systemd 单元文件(由包安装器写入)、`~/.config/kdeglobals`
- **可信的本地输入**:从仓库编译出的二进制、`/usr/bin/dsh-desktop` 主程序路径

### 已知不在威胁模型内

- **远程 URL 模式**(用户主动 `--url http://...`):用户已显式选择,不做 SSRF 防护
- **`DSH_BIN` 环境变量**:仅供开发/CI 覆盖,生产构建请勿设置;**未做 owner/world-writable 校验**(Round 35 安全审查 L-5 已知)
- **musl 系发行版**(如 Alpine):不正式支持,需要自行编译 Qt WebEngine

完整威胁模型、注入面、提权路径、systemd 沙箱配置见 [`docs/SECURITY-REVIEW-2026-08-28.md`](docs/SECURITY-REVIEW-2026-08-28.md)。

## 安全特性

DSH Desktop 0.1.x 已实现:

- 外部进程派生全部使用 `QProcess::setProgram/setArguments` 显式 argv,**不拼接 shell**
- 提权路径走 `pkexec --disable-internal-agent` 或固定绝对路径 `/usr/bin/pkexec`(非 PATH 解析)
- systemd `dsh-theme-export.service` 启用 `NoNewPrivileges=yes` / `ProtectSystem=strict` / `ProtectHome=yes` / `PrivateTmp=yes` / `ReadOnlyPaths=/root/.config` / `ReadWritePaths=/run/dsh-desktop`
- npm 安装阶段默认 `ignore-scripts=true`,避免 preinstall 脚本投毒
- 单元名/IP/URL 全部经过白名单校验(详见 [`src/service/SystemctlCommandBuilder.cpp`](src/service/SystemctlCommandBuilder.cpp))
- 全仓未发现 SQL 注入、shell 注入、TOCTOU 路径穿越、strcpy/sprintf 类缓冲溢出调用

## 公告渠道

安全更新会通过以下任一渠道发布:

1. GitHub Release,标题前缀 `[Security]`
2. CHANGELOG 的 `[Unreleased]` / 下一个版本块顶部的 "Security" 小节
3. 项目 README 与 `docs/RELEASE-NOTES-<version>.zh.md`

## 历史漏洞

无已公开披露的 CVE。

(本节会在每次 CVE 编号分配后追加链接。)

## 致谢

负责任披露者将在修复发布后于 `CHANGELOG.md` 致谢(除非要求匿名)。欢迎报告 — 你的披露让所有人都更安全。
