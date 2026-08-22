#!/usr/bin/env bash
# mihomo 节点管理小工具（通过本机 9090 控制 API）
# 用法:
#   ./switch-node.sh list          # 列出全部节点及当前选中
#   ./switch-node.sh set "节点名"   # 切换默认节点
API="http://127.0.0.1:9090"
case "${1:-list}" in
  list)
    curl -s "$API/proxies/PROXY" | python3 -c '
import json, sys
d = json.load(sys.stdin)
print("当前选中:", d.get("now"))
for i, n in enumerate(d.get("all", [])):
    print(f"{i:2d}. {n}")'
    ;;
  set)
    [ -n "$2" ] || { echo "用法: $0 set \"节点名\""; exit 1; }
    curl -s -X PUT -H 'Content-Type: application/json' -d "{\"name\":\"$2\"}" "$API/proxies/PROXY" && echo "已切换到: $2"
    ;;
  *)
    echo "用法: $0 [list|set <节点名>]"; exit 1 ;;
esac
