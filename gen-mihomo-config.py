#!/usr/bin/env python3
"""从 base64 订阅解码出的节点列表生成 mihomo config.yaml（支持 vless/anytls/vmess/hysteria2）"""
import urllib.parse as u
import yaml, sys, base64, json

lines = [l.strip() for l in open('/tmp/nodes.txt') if l.strip()]
proxies, seen = [], set()

def uniq(name, fallback):
    if not name:
        name = fallback
    if name in seen:
        name = f"{name}-{len(seen)}"
    seen.add(name)
    return name

for line in lines:
    if line.startswith("vmess://"):
        try:
            raw = line[len("vmess://"):]
            raw += "=" * (-len(raw) % 4)
            j = json.loads(base64.b64decode(raw).decode())
            name = uniq(j.get("ps", ""), j.get("add", "vmess"))
            entry = {"name": name, "type": "vmess", "server": j.get("add"),
                     "port": int(j.get("port", 443)), "uuid": j.get("id"),
                     "alterId": int(j.get("aid", 0)), "cipher": j.get("scy", "auto"), "udp": True}
            if j.get("tls") == "true":
                entry["tls"] = True
            if j.get("sni"):
                entry["servername"] = j["sni"]
            if j.get("fp"):
                entry["client-fingerprint"] = j["fp"]
            net = j.get("net", "tcp")
            if net in ("ws", "http", "grpc", "h2"):
                entry["network"] = net
                path = j.get("path", "/")
                host = j.get("host", "")
                if net == "ws":
                    entry["ws-opts"] = {"path": path}
                    if host:
                        entry["ws-opts"]["headers"] = {"Host": host}
                elif net == "http":
                    entry["http-opts"] = {"path": [path]}
                    if host:
                        entry["http-opts"]["headers"] = {"Host": [host]}
                elif net == "grpc":
                    entry["grpc-opts"] = {"grpc-service-name": path.lstrip("/")}
        except Exception as e:
            print(f"vmess 解析失败: {e}", file=sys.stderr)
            continue
        proxies.append(entry)
        continue

    p = u.urlsplit(line)
    q = u.parse_qs(p.query)
    name = uniq(u.unquote(p.fragment), p.hostname)
    entry = {"name": name, "server": p.hostname, "port": p.port, "udp": True}
    if p.scheme == "vless":
        entry.update({
            "type": "vless",
            "uuid": p.username,
            "network": q.get("type", ["tcp"])[0],
            "tls": True,
            "servername": q.get("servername", q.get("sni", [""]))[0],
            "client-fingerprint": q.get("fp", ["chrome"])[0],
        })
        if q.get("flow", [""])[0]:
            entry["flow"] = q["flow"][0]
        if q.get("pbk"):
            entry["reality-opts"] = {"public-key": q["pbk"][0], "short-id": q.get("sid", [""])[0]}
    elif p.scheme == "anytls":
        entry.update({
            "type": "anytls",
            "uuid": p.username,
            "sni": q.get("sni", [""])[0],
            "skip-cert-verify": q.get("insecure", ["0"])[0] == "1",
        })
        if q.get("alpn"):
            entry["alpn"] = [q["alpn"][0]]
    elif p.scheme == "hysteria2":
        entry.update({
            "type": "hysteria2",
            "password": p.username or q.get("password", [""])[0],
            "sni": q.get("sni", [""])[0],
        })
        if q.get("insecure", ["0"])[0] == "1":
            entry["skip-cert-verify"] = True
        if q.get("obfs"):
            entry["obfs"] = q["obfs"][0]
            entry["obfs-password"] = q.get("obfs-password", [""])[0]
    else:
        print(f"忽略未知协议: {p.scheme}", file=sys.stderr)
        continue
    proxies.append(entry)

names = [x["name"] for x in proxies]
config = {
    "mixed-port": 7897,
    "allow-lan": False,
    "bind-address": "*",
    "mode": "rule",
    "log-level": "info",
    "ipv6": False,
    "external-controller": "127.0.0.1:9090",
    "proxies": proxies,
    "proxy-groups": [
        {"name": "PROXY", "type": "select", "proxies": names + ["AUTO"]},
        {"name": "AUTO", "type": "url-test", "url": "http://www.gstatic.com/generate_204",
         "interval": 300, "proxies": names},
    ],
    "rules": [
        "IP-CIDR,10.0.0.0/8,DIRECT,no-resolve",
        "IP-CIDR,172.16.0.0/12,DIRECT,no-resolve",
        "IP-CIDR,192.168.0.0/16,DIRECT,no-resolve",
        "IP-CIDR,127.0.0.0/8,DIRECT,no-resolve",
        "MATCH,PROXY",
    ],
}
yaml.safe_dump(config, sys.stdout, allow_unicode=True, sort_keys=False)
print(f"# 共 {len(proxies)} 个节点", file=sys.stderr)
