# nsig: the YouTube `n` challenge on the console (spike)

Feasibility bench for playing members-only videos: the TVHTML5 links carry an `n` parameter that googlevideo
answers with HTTP 403 until it is transformed by the player's JavaScript.

| File | What it is |
|---|---|
| `client/src/yt/nsig_reduce.hpp` | Tokenises the TV player (`tv-player-ias.js`, 2.5 MB), splits the IIFE body into top-level units and keeps the dependency closure of the URL builder (the function that calls `x.set("alr","yes")`). No JS parser. |
| `client/src/yt/nsig.cpp` | QuickJS-ng glue: evaluates the reduced program, calls the builder, triggers the `n` transform, validates the result. Optional bytecode cache. |
| `client/third_party/quickjs-ng/` | QuickJS-ng 0.18.0 (MIT), sources unmodified. |
| `bench.cpp`, `empty.cpp` | Timings (reduce, load, bytecode load, solve) and the XEX size baseline. |
| `e2e.py` | Visitor session, TVHTML5 `/player`, solve with the bench, then `403` (raw `n`) against `206` (solved `n`). |
| `compare.py` | Same answer as yt-dlp-ejs on any list of player ids (`current` = what iframe_api announces). |

```bash
NSIG_BUILD_DIR=/tmp/nsig-pc tools/build-nsig.sh pc
python3 bench/nsig/e2e.py /tmp/nsig-pc/nsig-bench
python3 bench/nsig/compare.py /tmp/nsig-pc/nsig-bench current 5ec65609 74edf1a3
# console build (compile only; serialize with the shared lock):
NSIG_BUILD_DIR=/tmp/nsig-xbox flock /tmp/tube360-build-xbox.lock tools/build-nsig.sh xbox
```

On the console `nsig-bench.xex` reads `game:\tv-player.js` and logs through `xsdk_log_write`. `NSIG_STACK=1` adds the JS
stack to error messages; `NSIG_DUMP=<file>` writes the reduced script (to try other engines).
