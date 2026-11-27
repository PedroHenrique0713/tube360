#!/usr/bin/env python3
"""403 -> 206 com o `n` resolvido pelo nsig-bench, num vídeo público aberto pelo TVHTML5 (sessão de visitante).

    bench/nsig/e2e.py <caminho do nsig-bench> [videoId]

Só rede pública do YouTube. Nenhuma conta, nenhum token.
"""
import json, re, subprocess, sys, tempfile, urllib.parse, urllib.request

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"
# Mesma identidade que console_account.cpp usa no caminho da conta (policy->samsung).
TV = {"name": "TVHTML5", "code": "7", "version": "5.20260707",
      "agent": "Mozilla/5.0 (SMART-TV; Linux; Tizen 2.4.0) AppleWebKit/538.1 (KHTML, like Gecko) Version/2.4.0 TV Safari/538.1",
      "device": {"deviceMake": "Samsung", "deviceModel": "UKS9800", "osName": "Tizen", "osVersion": "2.4.0"}}

def get(url, headers=None, data=None):
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers or {}), timeout=30) as r:
        return r.read()

def http_code(url):
    out = subprocess.run(["curl", "-sL", "-o", "/dev/null", "-w", "%{http_code} %{size_download}", "-r", "0-99999", "-A", TV["agent"], url],
                         capture_output=True, text=True).stdout
    return out

def with_n(url, n):
    p = urllib.parse.urlparse(url)
    q = [(k, n if k == "n" else v) for k, v in urllib.parse.parse_qsl(p.query, keep_blank_values=True)]
    return urllib.parse.urlunparse(p._replace(query=urllib.parse.urlencode(q, safe="-_.~", quote_via=urllib.parse.quote)))

def main():
    bench, vid = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "aqz-KE-bpKQ")
    api = get("https://www.youtube.com/iframe_api", {"User-Agent": UA}).decode()
    pid = re.search(r"s\\/player\\/([0-9a-f]{8})", api).group(1)
    js = get(f"https://www.youtube.com/s/player/{pid}/tv-player-ias.vflset/tv-player-ias.js", {"User-Agent": UA})
    sts = int(re.search(rb"signatureTimestamp:(\d+)", js).group(1))
    print(f"player {pid} tv-player-ias {len(js)} bytes, signatureTimestamp {sts}")
    with tempfile.NamedTemporaryFile(suffix=".js") as f:
        f.write(js); f.flush()
        data = json.loads(get("https://www.youtube.com/sw.js_data", {"User-Agent": UA, "Referer": "https://www.youtube.com/sw.js"})[4:])
        visitor = data[0][2][0][0][13]
        ctx = {"clientName": TV["name"], "clientVersion": TV["version"], "hl": "en", "gl": "US", "visitorData": visitor,
               "timeZone": "UTC", "utcOffsetMinutes": 0, "userAgent": TV["agent"], **TV["device"]}
        body = {"context": {"client": ctx}, "videoId": vid, "contentCheckOk": True, "racyCheckOk": True,
                "playbackContext": {"contentPlaybackContext": {"html5Preference": "HTML5_PREF_WANTS", "signatureTimestamp": sts}}}
        hdr = {"Content-Type": "application/json", "Origin": "https://www.youtube.com", "X-Youtube-Client-Name": TV["code"],
               "X-Youtube-Client-Version": TV["version"], "X-Goog-Visitor-Id": visitor, "User-Agent": TV["agent"]}
        resp = json.loads(get("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", hdr, json.dumps(body).encode()))
        status = resp["playabilityStatus"]["status"]
        sd = resp.get("streamingData", {})
        fmts = {f["itag"]: f["url"] for f in sd.get("formats", []) + sd.get("adaptiveFormats", []) if "url" in f and f["itag"] in (18, 136, 140)}
        print(f"playability {status}, urls {sorted(fmts)}, hls {'hlsManifestUrl' in sd}")
        ns = sorted({urllib.parse.parse_qs(urllib.parse.urlparse(u).query)["n"][0] for u in fmts.values()})
        out = subprocess.run([bench, f.name, *ns], capture_output=True, text=True).stdout
        print("\n".join(l for l in out.splitlines() if l.split(" ")[0] in ("reduce_ms", "load_ms", "solve_ms")))
        solved = dict(re.findall(r"solve_ms [\d.]+ (\S+) -> (\S+)", out))
        ok = len(solved) == len(ns)
        for itag, url in sorted(fmts.items()):
            n = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)["n"][0]
            raw, fixed = http_code(url), http_code(with_n(url, solved[n])) if n in solved else "-"
            print(f"itag {itag}: n={n} raw -> {raw}   solved ({solved.get(n)}) -> {fixed}")
            ok = ok and raw.startswith("403") and fixed.startswith("206")
        print("RESULT", "403 -> 206 OK" if ok else "FAILED")
        return 0 if ok else 1

sys.exit(main())
