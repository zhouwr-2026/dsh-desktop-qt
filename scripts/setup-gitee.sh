#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
remote_url="git@gitee.com:eruditeLoong/dsh-desktop-qt.git"

prepare() {
  git config --global user.name 'eruditeLoong'
  git config --global user.email '1551541860@qq.com'

  if [[ ! -f "$HOME/.ssh/id_rsa" ]]; then
    mkdir -p "$HOME/.ssh"
    chmod 700 "$HOME/.ssh"
    ssh-keygen -t rsa -b 4096 -C '1551541860@qq.com' -f "$HOME/.ssh/id_rsa"
  fi

  printf '\n请把以下公钥添加到 Gitee SSH 公钥设置：\n\n'
  cat "$HOME/.ssh/id_rsa.pub"
  printf '\n添加完成后运行：%s push\n' "$0"
}

push_baseline() {
  cd "$repo_root"
  rm -f -- ./*.o

  if [[ ! -d .git ]]; then
    git init -b main
  fi

  if git remote get-url origin >/dev/null 2>&1; then
    git remote set-url origin "$remote_url"
  else
    git remote add origin "$remote_url"
  fi

  ssh -T git@gitee.com || [[ $? -eq 1 ]]
  git add -A
  if ! git diff --cached --quiet; then
    git commit -m 'chore: import current dsh desktop baseline'
  fi
  git push -u origin main
}

case "${1:-prepare}" in
  prepare) prepare ;;
  push) push_baseline ;;
  *) printf '用法: %s [prepare|push]\n' "$0" >&2; exit 2 ;;
esac
