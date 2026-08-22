#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

export GIT_PAGER=cat
export PAGER=cat
export GIT_EDITOR=true

rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure

if find . -maxdepth 1 -type f \( -name '*.o' -o -name 'CMakeCache.txt' -o -name 'build.ninja' \) -print -quit | grep -q .; then
  echo '错误：项目根目录仍存在编译产物' >&2
  exit 1
fi

git add -A
git diff --cached --check
if ! git diff --cached --quiet; then
  git commit -m 'fix: harden desktop runtime and packaging'
fi
git push origin main

echo '构建、测试、提交和推送全部完成。'
