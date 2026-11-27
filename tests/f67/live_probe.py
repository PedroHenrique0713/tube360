#!/usr/bin/env python3
"""Pede o /player do TVHTML5 (Samsung, sessão de visitante) de uma live pública e grava o JSON para o f67-live-probe.

    live_probe.py <saida.json> [videoId ...]    (sem ids, procura lives na busca; usa a primeira com live=1 e itag 136/140)
"""
import json, re, sys, urllib.parse, urllib.request
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"
TV = {"name": "TVHTML5", "code": "7", "version": "5.20260707", "agent": "Mozilla/5.0 (SMART-TV; Linux; Tizen 2.4.0) AppleWebKit/538.1 (KHTML, like Gecko) Version/2.4.0 TV Safari/538.1",
      "device": {"deviceMake": "Samsung", "deviceModel": "UKS9800", "osName": "Tizen", "osVersion": "2.4.0"}}
def get(url, headers=None, data=None):
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers or {}), timeout=30) as r: return r.read()
out, ids = sys.argv[1], sys.argv[2:]
api = get("https://www.youtube.com/iframe_api", {"User-Agent": UA}).decode(); pid = re.search(r"s\\/player\\/([0-9a-f]{8})", api).group(1)
js = get(f"https://www.youtube.com/s/player/{pid}/tv-player-ias.vflset/tv-player-ias.js", {"User-Agent": UA}); sts = int(re.search(rb"signatureTimestamp:(\d+)", js).group(1))
vis = json.loads(get("https://www.youtube.com/sw.js_data", {"User-Agent": UA, "Referer": "https://www.youtube.com/sw.js"})[4:])[0][2][0][0][13]
if not ids:
    page = get("https://www.youtube.com/results?search_query=ao+vivo+futebol&sp=EgJAAQ%253D%253D", {"User-Agent": UA, "Accept-Language": "pt-BR"}).decode()
    ids = list(dict.fromkeys(re.findall(r'"videoId":"([\w-]{11})"', page)))[:14]
for vid in ids:
    ctx = {"clientName": TV["name"], "clientVersion": TV["version"], "hl": "en", "gl": "US", "visitorData": vis, "timeZone": "UTC", "utcOffsetMinutes": 0, "userAgent": TV["agent"], **TV["device"]}
    body = {"context": {"client": ctx}, "videoId": vid, "contentCheckOk": True, "racyCheckOk": True, "playbackContext": {"contentPlaybackContext": {"html5Preference": "HTML5_PREF_WANTS", "signatureTimestamp": sts}}}
    hdr = {"Content-Type": "application/json", "Origin": "https://www.youtube.com", "X-Youtube-Client-Name": TV["code"], "X-Youtube-Client-Version": TV["version"], "X-Goog-Visitor-Id": vis, "User-Agent": TV["agent"]}
    try: r = json.loads(get("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", hdr, json.dumps(body).encode()))
    except Exception as e: print(vid, "ERR", e); continue
    af = {f["itag"]: f for f in r.get("streamingData", {}).get("adaptiveFormats", []) if "live=1" in f.get("url", "")}
    print(vid, r["playabilityStatus"]["status"], "live=1 itags", sorted(af))
    if 136 in af and 140 in af:
        json.dump(r, open(out, "w")); print("saved", vid); sys.exit(0)
sys.exit(1)
