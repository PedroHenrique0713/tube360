#!/usr/bin/env python3
"""Confere o nsig-bench contra o yt-dlp-ejs (referência) em vários players do YouTube.

    bench/nsig/compare.py <caminho do nsig-bench> <id do player> [<id> ...]
    bench/nsig/compare.py <caminho do nsig-bench> current        (o player atual do iframe_api)

Precisa de `node` e do pacote pip `yt-dlp-ejs`. Sem `n` do YouTube envolvido: usa um desafio fixo.
Também serve de canário: rodar contra `current` avisa quando uma mudança do player quebra o redutor.
"""
import re, subprocess, sys, tempfile, urllib.request
from pathlib import Path

CHALLENGE = "abcdefghijklmnopqrstuvwxyz0123"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"
EJS = r"""
const fs=require('fs');const D=process.argv[2];
(0,eval)(fs.readFileSync(D+'/lib.min.js','utf8')+'\nvar meriyah=lib.meriyah,astring=lib.astring;\n'+fs.readFileSync(D+'/core.min.js','utf8')+'\n;globalThis.__jsc=jsc;');
try{const r=__jsc({type:'player',player:fs.readFileSync(process.argv[3],'utf8'),requests:[{type:'n',challenges:[process.argv[4]]}]}).responses[0];
console.log(r.type==='result'?Object.values(r.data)[0]:'ERR');}catch(e){console.log('ERR')}
"""

def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=30).read()

def main():
    bench, ids = sys.argv[1], sys.argv[2:]
    import yt_dlp_ejs, os
    solver = Path(yt_dlp_ejs.__file__).parent / "yt" / "solver"
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "ejs.js").write_text(EJS)
        for pid in ids:
            if pid == "current":
                pid = re.search(r"s\\/player\\/([0-9a-f]{8})", fetch("https://www.youtube.com/iframe_api").decode()).group(1)
            try:
                js = fetch(f"https://www.youtube.com/s/player/{pid}/tv-player-ias.vflset/tv-player-ias.js")
            except Exception as e:
                print(f"{pid}: not available ({e})"); continue
            path = Path(tmp) / f"{pid}.js"; path.write_bytes(js)
            ref = subprocess.run(["node", str(Path(tmp) / "ejs.js"), str(solver), str(path), CHALLENGE], capture_output=True, text=True).stdout.strip()
            out = subprocess.run([bench, str(path), CHALLENGE], capture_output=True, text=True).stdout
            mine = (re.search(r"solve_ms [\d.]+ \S+ -> (\S+)", out) or [None, None])[1]
            ok = mine is not None and mine == ref
            bad += not ok
            red = re.search(r"reduce_ms ([\d.]+).*error=(\S*) entry=(\S*).*kept=(\d+) program_bytes=(\d+)", out)
            print(f"{pid}: {'OK  ' if ok else 'FAIL'} ejs={ref} nsig={mine}" + (f"  reduce {red[1]} ms, kept {red[4]} units, program {int(red[5])//1024} KB" if red else ""))
    return 1 if bad else 0

sys.exit(main())
