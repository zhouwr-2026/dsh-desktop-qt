#!/usr/bin/env bash
# @author zhouwr

set -euo pipefail

profile="${DSH_PROFILE:-web}"
dsh_home="${DSH_HOME:-}"
base_url="${DSH_WEB_URL:-}"

log()  { printf '[profile-check] %s\n' "$*"; }
fail() { printf '[profile-check] ERROR: %s\n' "$*" >&2; exit 1; }

systemd_environment_value() {
  local requested_key="$1"
  node -e '
    const requestedKey = process.argv[1]
    let input = ""
    process.stdin.setEncoding("utf8")
    process.stdin.on("data", chunk => { input += chunk })
    process.stdin.on("end", () => {
      const tokens = []
      let current = ""
      let singleQuoted = false
      let doubleQuoted = false
      for (const character of input.trim()) {
        if (character === "\u0027" && !doubleQuoted) {
          singleQuoted = !singleQuoted
        } else if (character === "\u0022" && !singleQuoted) {
          doubleQuoted = !doubleQuoted
        } else if (/\s/.test(character) && !singleQuoted && !doubleQuoted) {
          if (current) tokens.push(current)
          current = ""
        } else {
          current += character
        }
      }
      if (current) tokens.push(current)
      const prefix = `${requestedKey}=`
      const match = tokens.find(token => token.startsWith(prefix))
      if (match) process.stdout.write(match.slice(prefix.length))
    })
  ' "${requested_key}"
}

command -v node >/dev/null 2>&1 || fail "未找到 node"
command -v curl >/dev/null 2>&1 || fail "未找到 curl"

service_show=""
if command -v systemctl >/dev/null 2>&1; then
  for scope in user system; do
    systemctl_args=(--no-pager show --property=LoadState --property=MainPID --property=ExecStart --property=Environment dsh-web.service)
    [[ "${scope}" == user ]] && systemctl_args=(--user "${systemctl_args[@]}")
    candidate_show="$(systemctl "${systemctl_args[@]}" 2>/dev/null || true)"
    load_state="$(sed -n 's/^LoadState=//p' <<<"${candidate_show}")"
    exec_start="$(sed -n 's/^ExecStart=//p' <<<"${candidate_show}")"
    official_web_pattern='(^|[/[:space:]])dsh[[:space:]]+web([[:space:];}]|$)'
    official_profile_pattern='(^|[/[:space:]])dsh[[:space:]]+--profile(=|[[:space:]])web[[:space:]]+web([[:space:];}]|$)'
    if [[ "${load_state}" == loaded
          && ("${exec_start}" =~ ${official_web_pattern}
              || "${exec_start}" =~ ${official_profile_pattern}) ]]; then
      service_show="${candidate_show}"
      break
    fi
  done
fi

if [[ -n "${service_show}" ]]; then
  main_pid="$(sed -n 's/^MainPID=//p' <<<"${service_show}")"
  exec_start="$(sed -n 's/^ExecStart=//p' <<<"${service_show}")"
  if [[ -z "${dsh_home}" && "${main_pid}" =~ ^[1-9][0-9]*$
        && -r "/proc/${main_pid}/environ" ]]; then
    while IFS= read -r -d '' environment_entry; do
      if [[ "${environment_entry}" == DSH_HOME=* ]]; then
        dsh_home="${environment_entry#DSH_HOME=}"
        break
      fi
    done < "/proc/${main_pid}/environ"
  fi
  if [[ -z "${dsh_home}" ]]; then
    service_environment="$(sed -n 's/^Environment=//p' <<<"${service_show}")"
    dsh_home="$(systemd_environment_value DSH_HOME <<<"${service_environment}")"
  fi
  if [[ -z "${base_url}" ]]; then
    detected_host="127.0.0.1"
    detected_port="3080"
    host_pattern='--host(=|[[:space:]])([^[:space:];}]+)'
    port_pattern='--port(=|[[:space:]])([0-9]+)'
    [[ "${exec_start}" =~ ${host_pattern} ]] && detected_host="${BASH_REMATCH[2]}"
    [[ "${exec_start}" =~ ${port_pattern} ]] && detected_port="${BASH_REMATCH[2]}"
    [[ "${detected_host}" == "0.0.0.0" ]] && detected_host="127.0.0.1"
    [[ "${detected_host}" == "::" ]] && detected_host="::1"
    if [[ "${detected_host}" == *:* ]]; then
      base_url="http://[${detected_host}]:${detected_port}"
    else
      base_url="http://${detected_host}:${detected_port}"
    fi
  fi
fi

if [[ -z "${dsh_home}" ]]; then
  [[ -n "${HOME:-}" ]] || fail "无法确定 DSH_HOME；请显式设置 DSH_HOME"
  dsh_home="${HOME}/.dsh"
fi
[[ -n "${base_url}" ]] || base_url="http://127.0.0.1:3080"
profile_dir="${dsh_home}/profiles/${profile}"

[[ -f "${profile_dir}/package.json" ]] || fail "profile 不存在：${profile_dir}"
[[ -d "${profile_dir}/node_modules" ]] || fail "依赖未安装：${profile_dir}/node_modules"

log "检查 ${profile_dir} 的插件入口"
node - "${profile_dir}" "${profile}" <<'NODE'
const fs = require('node:fs')
const path = require('node:path')

const profileDir = process.argv[2]
const profileName = process.argv[3]
const profile = JSON.parse(fs.readFileSync(path.join(profileDir, 'package.json'), 'utf8'))
const dependencies = Object.keys(profile.dependencies ?? {})
const failures = []

function collectExportPaths(value, paths = []) {
  if (typeof value === 'string') paths.push(value)
  else if (Array.isArray(value)) value.forEach(item => collectExportPaths(item, paths))
  else if (value && typeof value === 'object') {
    Object.values(value).forEach(item => collectExportPaths(item, paths))
  }
  return paths
}

function rootExport(exportsField) {
  if (!exportsField || typeof exportsField !== 'object' || Array.isArray(exportsField)) {
    return exportsField
  }
  const keys = Object.keys(exportsField)
  return keys.some(key => key.startsWith('.')) ? exportsField['.'] : exportsField
}

for (const name of dependencies) {
  const packageDir = path.join(profileDir, 'node_modules', name)
  const manifestPath = path.join(packageDir, 'package.json')
  if (!fs.existsSync(manifestPath)) {
    failures.push(`${name}: package.json 缺失`)
    continue
  }
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'))
  const declaredEntries = [
    manifest.main,
    manifest.module,
    ...collectExportPaths(rootExport(manifest.exports)),
    ...collectExportPaths(manifest.exports?.['./client']),
  ].filter(value => typeof value === 'string' && !path.isAbsolute(value))
  const entries = new Map()
  for (const entry of declaredEntries) entries.set(path.resolve(packageDir, entry), entry)
  for (const [target, entry] of entries) {
    if (!target.startsWith(`${packageDir}${path.sep}`)) {
      failures.push(`${name}: 非法入口路径 ${entry}`)
      continue
    }
    if (!fs.existsSync(target) || !fs.statSync(target).isFile()
        || fs.statSync(target).size === 0) {
      failures.push(`${name}: 构建产物缺失 ${entry}`)
      continue
    }
    if (/\.(?:c|m)?js$/.test(entry)) {
      try {
        require('node:child_process').execFileSync(
          process.execPath, ['--check', target], { stdio: 'pipe' })
      } catch (error) {
        failures.push(`${name}: JavaScript 语法检查失败 ${entry}`)
      }
    }
  }
}

if (failures.length) {
  for (const failure of failures) process.stderr.write(`[profile-check] ERROR: ${failure}\n`)
  process.stderr.write(`[profile-check] 修复：dsh plugin --profile ${profileName} install\n`)
  process.stderr.write('[profile-check] 若仅被新包发布年龄策略拦截，确认来源后临时追加 --config.minimumReleaseAge=0\n')
  process.exit(1)
}
process.stdout.write(`[profile-check] ${dependencies.length} 个 profile 依赖入口正常\n`)
NODE

log "检查 Web 首页与核心会话 bundle"
curl --fail --silent --show-error --max-time 5 --output /dev/null "${base_url}/" \
  || fail "Web 首页不可达：${base_url}/"
curl --fail --silent --show-error --max-time 10 --output /dev/null \
  "${base_url}/plugins/@deepseek-ai/dsh-client-ui-conversation/client.js" \
  || fail "核心会话 bundle 不可达"

log "全部通过"
