#!/usr/bin/env node
/**
 * 修复 DSH 会话归属：把会话 header.cwd 从错误的 /home/arch/... 改为 /home/zhouwr/...
 *
 * 背景：本会话（session-4ae52392-acc2-4d0a-b532-89706442ecb3）创建时 cwd 被设为
 * /home/arch/Project/CodeWorkspace/DSH-Desktop（该目录不存在），导致：
 *   1. 会话无法归属到 DSH-Desktop 工作区（归属 = header.cwd 的 realpath === workspace.path）
 *   2. 所有以会话 cwd 为工作根的工具（bash/glob/grep/custom-fs）全部失效
 *
 * 本脚本用 node:zlib 的 zstd API（与 DSH 后端完全相同的压缩参数：checksum flag），
 * 解压 → 改 header 第一行 cwd → 逐帧重压（header 独立第一帧 + 事件帧），保证帧格式兼容：
 *   - dsh-session-persistence-jsonl 要求第一个 zstd 帧恰好是一行 header（assertZstdHeaderFrame）
 *   - 因此 header 必须单独压缩为一帧，其余事件为另一帧，两帧 concat
 *
 * 用法（建议先停 dsh-web，避免运行中覆盖）：
 *   sudo systemctl stop dsh-web
 *   node /home/zhouwr/Project/CodeWorkspace/DSH-Desktop/fix-session-cwd.mjs
 *   sudo systemctl start dsh-web
 *
 * @author zhouwr
 */
import { zstdCompressSync, zstdDecompressSync, constants } from "node:zlib";
import { readFileSync, writeFileSync, existsSync, mkdirSync, renameSync } from "node:fs";
import { join } from "node:path";

const SESSIONS_ROOT = "/home/zhouwr/.dsh/sessions";
const SESSION_ID = "session-4ae52392-acc2-4d0a-b532-89706442ecb3";
const OLD_CWD = "/home/arch/Project/CodeWorkspace/DSH-Desktop";
const NEW_CWD = "/home/zhouwr/Project/CodeWorkspace/DSH-Desktop";

/** projectKey(cwd)：与 dsh-session-persistence-jsonl 完全一致的目录名编码 */
function projectKey(cwd) {
  let readable = "";
  let separatorRun = false;
  for (let i = 0; i < cwd.length; i++) {
    const code = cwd.charCodeAt(i);
    const ch = String.fromCharCode(code);
    if (ch === "/" || ch === "\\" || ch === ":") {
      if (!separatorRun) readable += "-";
      separatorRun = true;
    } else if (ch !== "~" && /^[A-Za-z0-9._-]$/.test(ch)) {
      readable += ch;
      separatorRun = false;
    } else {
      readable += "~" + code.toString(16).toUpperCase().padStart(4, "0");
      separatorRun = false;
    }
  }
  return `--${(readable.replace(/^-+/, "") || "root").slice(0, 251)}--`;
}

const oldDir = join(SESSIONS_ROOT, projectKey(OLD_CWD), SESSION_ID);
const newDir = join(SESSIONS_ROOT, projectKey(NEW_CWD), SESSION_ID);
const oldPath = join(oldDir, "session.jsonl.zstd");
const newPath = join(newDir, "session.jsonl.zstd");

console.log("旧路径:", oldPath, existsSync(oldPath) ? "存在" : "不存在");
console.log("新路径:", newPath);

if (!existsSync(oldPath)) {
  console.error("找不到旧会话文件，可能已处理过或路径变化。退出。");
  process.exit(1);
}

// 1) 解压整个多帧容器为明文
const compressed = readFileSync(oldPath);
const plaintext = zstdDecompressSync(compressed).toString("utf8");
const lines = plaintext.split("\n");
const headerLine = lines[0];
const header = JSON.parse(headerLine);
console.log("原 header:", JSON.stringify(header));

if (header.cwd !== OLD_CWD) {
  console.error(`header.cwd 是 "${header.cwd}"，不是预期 "${OLD_CWD}"。不修改，退出。`);
  process.exit(1);
}
if (header.id !== SESSION_ID) {
  console.error(`header.id 是 "${header.id}"，不是预期 "${SESSION_ID}"。退出。`);
  process.exit(1);
}

// 2) 修改 cwd
header.cwd = NEW_CWD;
lines[0] = JSON.stringify(header);
const eventText = lines.slice(1).join("\n"); // 事件区（可能为空串）
console.log("新 header:", JSON.stringify(header));

// 3) 逐帧重压：header 单独一帧（必须恰好一行），事件单独一帧，concat 为多帧容器
//    node:zlib.zstdCompressSync 每次调用产出一个完整 zstd 帧；
//    与后端一致启用 checksum flag（ZSTD_c_checksumFlag=1）。
const FRAME_OPTIONS = { params: { [constants.ZSTD_c_checksumFlag]: 1 } };
const headerFrame = zstdCompressSync(Buffer.from(lines[0] + "\n", "utf8"), FRAME_OPTIONS);
const eventFrame = eventText.length > 0
  ? zstdCompressSync(Buffer.from(eventText, "utf8"), FRAME_OPTIONS)
  : Buffer.alloc(0);
const recompressed = Buffer.concat([headerFrame, eventFrame]);

// 4) 写入新位置（正确的 project 目录），旧文件备份后删除
mkdirSync(newDir, { recursive: true });
writeFileSync(newPath, recompressed);
console.log("已写入:", newPath, `(${recompressed.length} 字节, 帧数: ${eventText.length > 0 ? 2 : 1})`);

const backupPath = oldPath + ".bak-arch-cwd";
renameSync(oldPath, backupPath);
console.log("旧文件已备份:", backupPath);

console.log("完成。现在可以重启 dsh-web：");
console.log("  sudo systemctl start dsh-web");
console.log("重启后：");
console.log("  1. 会话将归属到 DSH-Desktop 工作区（cwd 匹配 workspace.path）");
console.log("  2. 工具后端（bash/glob/grep/custom-fs）将以正确的 /home/zhouwr 为工作根恢复");
