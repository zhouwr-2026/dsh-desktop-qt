#!/usr/bin/env bash
# @author zhouwr

set -euo pipefail

checker="${1:?checker path is required}"
# 显式 unset DSH_HOME，避免开发环境（如 opencode 注入）把 /home/<user>/.dsh
# 暴露给 checker，让 checker 走"已配置 service"的分支（直接拿到真实
# dsh-web.service 的 Environment=HOME=/home/<user>）而不是 fixture 设的
# LoadState=not-found fallback。这条是 59d2ff0 改 checker 后的回归根因：
# fixture 设计假设 DSH_HOME 未设置，但开发环境往往相反。
unset DSH_HOME
# 同样 unset DSH_WEB_URL（开发环境可能注入默认 URL，让 fixture base_url 解析
# 走 ExecStart 路径失效）。
unset DSH_WEB_URL
fixture_root="$(mktemp -d "${TMPDIR:-/tmp}/dsh-profile-check-test.XXXXXX")"
service_process=""
cleanup() {
  [[ -z "${service_process}" ]] || kill "${service_process}" 2>/dev/null || true
  rm -rf -- "${fixture_root}"
}
trap cleanup EXIT

mkdir -p "${fixture_root}/bin" \
         "${fixture_root}/home/profiles/web/node_modules/root-export/dist" \
         "${fixture_root}/home/profiles/web/node_modules/conditional-export/dist"

printf '%s\n' \
  '{"name":"fixture-profile","dependencies":{"root-export":"1.0.0","conditional-export":"1.0.0"}}' \
  > "${fixture_root}/home/profiles/web/package.json"
printf '%s\n' \
  '{"name":"root-export","version":"1.0.0","exports":"./dist/index.js"}' \
  > "${fixture_root}/home/profiles/web/node_modules/root-export/package.json"
printf '%s\n' \
  '{"name":"conditional-export","version":"1.0.0","exports":{"import":"./dist/index.mjs","default":"./dist/index.js"}}' \
  > "${fixture_root}/home/profiles/web/node_modules/conditional-export/package.json"

cat > "${fixture_root}/bin/systemctl" <<'SH'
#!/usr/bin/env bash
printf 'LoadState=loaded\nMainPID=%s\n' "${FAKE_SERVICE_PID}"
printf 'ExecStart=%s\n' "${FAKE_EXEC_START}"
printf 'Environment="DSH_HOME=%s"\n' "${FAKE_DSH_HOME}"
SH
cat > "${fixture_root}/bin/curl" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "${CURL_LOG}"
SH
chmod +x "${fixture_root}/bin/systemctl" "${fixture_root}/bin/curl"

DSH_HOME="${fixture_root}/home" sleep 30 &
service_process=$!
export FAKE_SERVICE_PID="${service_process}"
export FAKE_DSH_HOME="${fixture_root}/home"
export FAKE_EXEC_START='{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh --profile web plugin install ; }'
export CURL_LOG="${fixture_root}/curl.log"

if PATH="${fixture_root}/bin:${PATH}" HOME="${fixture_root}/unused-home" \
     "${checker}" > "${fixture_root}/foreign-command.log" 2>&1; then
  printf 'checker unexpectedly accepted a non-web dsh command\n' >&2
  exit 1
fi
grep -F "profile 不存在：${fixture_root}/unused-home/.dsh/profiles/web" \
  "${fixture_root}/foreign-command.log" >/dev/null

export FAKE_EXEC_START='{ path=/usr/bin/dsh ; argv[]=/usr/bin/dsh --profile web web --host 127.0.0.2 --port 9090 ; }'
if PATH="${fixture_root}/bin:${PATH}" HOME="${fixture_root}/unused-home" \
     "${checker}" > "${fixture_root}/missing.log" 2>&1; then
  printf 'checker unexpectedly accepted a missing string root export\n' >&2
  exit 1
fi
grep -F '构建产物缺失 ./dist/index.js' "${fixture_root}/missing.log" >/dev/null
grep -F 'conditional-export: 构建产物缺失 ./dist/index.mjs' \
  "${fixture_root}/missing.log" >/dev/null

printf 'export default {}\n' \
  > "${fixture_root}/home/profiles/web/node_modules/root-export/dist/index.js"
printf 'export default {}\n' \
  > "${fixture_root}/home/profiles/web/node_modules/conditional-export/dist/index.mjs"
printf 'module.exports = {}\n' \
  > "${fixture_root}/home/profiles/web/node_modules/conditional-export/dist/index.js"
PATH="${fixture_root}/bin:${PATH}" HOME="${fixture_root}/unused-home" "${checker}" >/dev/null
grep -F 'http://127.0.0.2:9090/' "${fixture_root}/curl.log" >/dev/null

kill "${service_process}"
wait "${service_process}" 2>/dev/null || true
service_process=""
FAKE_SERVICE_PID=0 PATH="${fixture_root}/bin:${PATH}" \
  HOME="${fixture_root}/unused-home" "${checker}" >/dev/null || true
