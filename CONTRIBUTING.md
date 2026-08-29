# 贡献指南

本项目欢迎社区贡献。所有修改都应通过同一套构建、测试与冒烟门禁。

## 1. 开发环境

```bash
cmake --preset dev
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
```

诊断命令：

```bash
QT_QPA_PLATFORM=offscreen ./build/dev/dsh-desktop --probe
QT_QPA_PLATFORM=offscreen ./build/dev/dsh-desktop --smoke
QT_QPA_PLATFORM=offscreen ./build/dev/dsh-desktop --self-test
```

## 2. 代码组织

```
src/
├── app/      # 主控制器 + 对话框 + 托盘
├── backend/  # 三种后端实现（systemd / supervised / external）
├── icon/     # 图标加载
├── platform/ # GLX/DRI3 探测
├── service/  # systemd 服务管理、单元构造、show 解析
├── theme/    # KDE 主题监听
├── updater/  # dsh 后端 + 桌面端更新器
├── util/     # 叶子工具：Logger / Notify / Sha256 / HttpProbe / SyncHttp / RunSyncProcess
├── web/      # Qt WebEngine 页面与下载拦截
```

`util/` 全部是纯函数工具，无运行时依赖；新增请保持同样风格（无成员状态）。

## 3. 编码规范

- C++17，配合 `clang-format`（仓库根建议提交 `.clang-format`）。
- 头文件用 `#pragma once`；类成员函数顺序：public → protected → private。
- 私有方法优先使用匿名命名空间而非 `private static`。
- 公开 API 命名：驼峰；强类型枚举（`enum class`）。
- 注释与日志以**中文**为主；API 注释解释"为什么"，不解释"做什么"。
- 涉及外部进程的，统一用 `QProcess::setProgram/setArguments` 显式 argv，不要拼接 shell。
- 路径与 unit 名必须经过白名单校验（`SystemctlCommandBuilder::isValidUnitName`）。
- 文件名净化使用 `DesktopReleaseDownloader::sanitizeFileName`。

## 4. 提交与分支

- 主分支：`master`。
- 主题分支：`feat/<简述>` 或 `fix/<issue 或描述>`。
- 提交信息格式（推荐英文，遵循 Conventional Commits 风格）：
  ```
  feat(service): 抽 SystemctlCommandBuilder
  fix(updater): 修复 dsh --version 进程泄漏
  refactor(util): 用 SHA256 单一来源替换流式实现
  docs(release): 0.1.0 发布说明
  test(installer): 加 is_official_dsh_web 单测
  ```
- 每个提交应保持可编译、可测。

## 5. 测试规范

- 所有新功能必须有单元测试。
- 工具类（`util/`、`service/SystemctlCommandBuilder` 等）保持单测覆盖。
- shell 集成测试放 `tests/test_*.sh`，必须使用 `set -euo pipefail`。
- 测试禁止依赖外部网络；若必须，调 `SyncHttp` 走现有 `--dry-run` 路径。
- 提交前必跑：
  ```bash
  cmake --build build/dev --parallel
  ctest --test-dir build/dev
  bash scripts/smoke.sh
  ```
- 任何超过 100 行的新增文件，应在 CHANGELOG 末尾加一条 Round 记录。

## 6. 文档同步

当改动涉及下列任一项时，必须同步更新对应文档：

| 改动范围 | 必改文档 |
| --- | --- |
| 新增/修改依赖 | `docs/DEPENDENCIES.md` + `CMakeLists.txt` + `packaging/PKGBUILD` + `packaging/install.sh` + `packaging/<debian|rpm>/...` |
| 新增/修改外部进程派生 | `docs/SECURITY-REVIEW-2026-08-28.md`（沙箱与 argv 约束） |
| 改动后端协议 | `docs/DSH-DESKTOP-SERVICE-PLAN.zh.md` + `README.md` 后端管理章节 |
| 用户可见功能 | `README.md` + `docs/INSTALL-LINUX.zh.md` |
| 发布相关 | `docs/RELEASE-NOTES-<version>.zh.md` + `CHANGELOG.md` |

## 7. 安全审查

任何对以下内容的改动，必须自检并补充到 `docs/SECURITY-REVIEW-2026-08-28.md`：

- 外部进程派生（`QProcess::start`、`system`、`popen` 等）
- 文件系统写路径
- systemd unit 字符串构造
- D-Bus 调用参数
- npm / 网络调用 URL

## 8. 评审要求

每个 PR 必须满足：

1. `ctest --test-dir build/dev` 全过（24+ 个用例）
2. `bash scripts/smoke.sh` 4/4 全过
3. `bash scripts/package-linux.sh` 退出码 0
4. 文档同步（同上表）
5. 至少一位维护者 review
