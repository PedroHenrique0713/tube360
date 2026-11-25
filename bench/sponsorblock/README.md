# SponsorBlock qualification

`client/src/yt/sponsorblock.*` uses the application's HTTPS transport and Mbed TLS, with the existing GTS
roots, certificate and hostname verification enabled. Only four hex characters of the SHA-256 video ID are
sent to `/api/skipSegments/<prefix>`. No credential, video ID query, view reporting or submission is sent.
One dedicated SDL worker fetches in the background; revisions discard responses from a closed or replaced
video. Off starts no request. Live/audio-only playback has no actions or timeline marks.

Cache: at most 32 videos in memory per session; positive TTL ten minutes, empty/failure TTL one minute.
Requests/parsing are bounded to 512 KiB, 16 levels, 16384 nodes and 128 accepted segments per video.
Only `sponsor`, `selfpromo`, `interaction` with `actionType=skip` (or the older absent action type) are used.
Invalid times and outdated durations are ignored. Touching/overlapping segments form one seek/undo range.
An automatically skipped group stays handled until the video closes or changes, including after Y returns
to its start. Same-video seeks and quality/sound changes preserve the state. Manual mode offers the button
again if you seek back into a segment. Settings arrows immediately save the mode; B closes one screen level.

## Recorded response and license

`fixtures/public-prefix.json` is an **unmodified public response**, retrieved 2026-11-24 from:

```text
https://sponsor.ajay.app/api/skipSegments/5f6b?categories=%5B%22sponsor%22%2C%22selfpromo%22%2C%22interaction%22%5D&actionTypes=%5B%22skip%22%5D
```

It contains 81 video IDs sharing the prefix, 27093 bytes. The PC app proofs select `wWC8wVI4E3E`:
Playing With Power MTG, **Reanimator vs Izzet Delver | Legacy Gameplay**, duration 666.681 s, sponsor range
**7.774863–20.877296 s**. A public YouTube oEmbed request confirmed the title/channel without account,
cookies or media extraction. Fixture visuals/audio are generated locally with FFmpeg, with explicitly Demo
metadata and no public video download. The recorded community segment data remain unchanged.

Segments: [SponsorBlock, sponsor.ajay.app](https://sponsor.ajay.app/), contributed by its community,
under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). This project is noncommercial.
The data license is separate from the MIT code. Source contract:
[API](https://github.com/ajayyy/SponsorBlock/wiki/API-Docs),
[data license](https://github.com/ajayyy/SponsorBlock/wiki/Database-and-API-License).

## Reproduce on PC

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 sponsorblock-test sponsorblock-worker-test tube360-sponsorblock-fixture
client/build/sponsorblock-test bench/sponsorblock/fixtures/public-prefix.json
client/build/sponsorblock-worker-test
python3 bench/sponsorblock/e2e.py
python3 bench/sponsorblock/phone.py
```

Results, logs and captures are in `.work/sponsorblock-e2e/`, with isolated HOME/cache/assets, `server=off`,
dummy audio and offscreen SDL. No personal account, real speaker or production service is used. Settings
cases cover every mode in pt/en/es, 4:3/large text, persistence and B. Player cases cover button, natural
expiry, one-press skip, automatic skip, Y return without reskip, undo expiry, marks, API offline/slow and a
loopback Nest using real TLS/Cast v2. The slow API case requires the first frame before the delayed reply.
`e2e.py <case> ...` selects cases; unknown names fail. PC screenshots are layout proofs, not console timings.
`phone.py` reuses the native lounge/DIAL fixture server and `cast-test-app`, with the recorded public
SponsorBlock response. It sends the video from a simulated phone and checks that automatic skip and Y undo
are reported back to the phone at the correct positions, with one API request. All HTTP stays on loopback.
Build `cast-test-app` with the same CMake command before running it. These scripts never contact a real phone.

Optional single public probe through the app's Mbed TLS, without YouTube playback:

```bash
client/build/sponsorblock-test --public wWC8wVI4E3E
openssl s_client -connect sponsor.ajay.app:443 -servername sponsor.ajay.app \
  -CAfile client/src/yt/gts.pem -verify_hostname sponsor.ajay.app -verify_return_error </dev/null
```

On 2026-11-24 the chain was `ajay.app` (`*.ajay.app`) → `WE1` → `GTS Root R4`; the leaf expires
2026-12-27. Renewals may change the chain. Console TLS still requires the coordinator's check.

## Console failure investigation, 2026-11-25

The coordinator observed `SponsorBlock: 0 trechos` on real hardware with the original build. That line
could mean an empty bucket **or** a TLS/network/HTTP/parser failure. The revised worker publishes a
diagnostic together with its segments, preserving it through the negative cache and fencing stale replies.
The UI stays silent, but the log now distinguishes `ok`, `empty`, `tls_error`, `network_error`, `http_error`,
`parse_error`, cancellation and invalid IDs, with HTTP/libcurl/mbedTLS codes, error text, bucket/match counts,
prefix, time and console clock. Parser errors include bounded context. No token, cookie or response body
is dumped. `verify=unavailable` is explicit when curl has already released a failed TLS context.

`https.cpp` retains `CURLOPT_ERRORBUFFER` and records level-1 Mbed TLS error codes through
`CURLOPT_SSL_CTX_FUNCTION`; peer and hostname verification remain enabled. The buffers live as long as the
transport, including cached TLS connections. No certificates or ciphers were changed without evidence.

The **actual Xbox config** already enables ECDSA, P-256, P-384 and ECDHE-ECDSA. Native qualification now
compiles Mbed TLS 3.6.8 and curl against `tools/mbedtls-x360/config.h`, using the same platform source;
only the OS entropy provider is `getrandom` instead of `XeCryptRandom`. The effective configuration macros
were compared with the PowerPC compiler (the native-only `MBEDTLS_ARCH_IS_X64` is the sole difference).
Two real HTTP requests were made: SponsorBlock returned 200 / one segment in 0.469 s, and YouTube
`robots.txt` returned 200 in 0.191 s. Both used the existing GTS bundle. This does **not** reproduce or
resolve the hardware-specific failure; the new console log is required to choose its actual correction.

Reproduce offline diagnostics and TLS rejection (no public requests in CTest):

```bash
cmake -S bench/sponsorblock/tls -B .work/sponsorblock-xbox-config-pc -DCMAKE_BUILD_TYPE=Release
cmake --build .work/sponsorblock-xbox-config-pc -j2
ctest --test-dir .work/sponsorblock-xbox-config-pc --output-on-failure
```

Optional public requests, one each:

```bash
.work/sponsorblock-xbox-config-pc/sponsorblock-transport-probe
.work/sponsorblock-xbox-config-pc/sponsorblock-transport-probe https://www.youtube.com/robots.txt
```

The probe also builds as an XEX using the PowerPC toolchain, with one 512 KiB worker and one SponsorBlock
request, without player load. Exact coordinator commands and log interpretation: [tls/CONSOLE.md](tls/CONSOLE.md).

## Console coordinator script

Keep the installed app folder intact. Create a temporary folder, for example `HDD:\F34-SponsorBlock\`,
with `tube360.xex`, all four `inter-*.ttf` fonts and the following files. Launch that `tube360.xex` with
the coordinator's console tools. No `server.txt` or account file is needed.

`autoplay.txt`:

```text
wWC8wVI4E3E 5
```

`settings.txt`:

```text
lang=pt
server=off
sound=tv
sponsorblock=button
```

Concrete commands **for the coordinator only**, from this worktree, after exiting the running app:

```bash
python3 - <<'PY'
from pathlib import Path
import shutil
folder=Path('.work/console-f34')
folder.mkdir(parents=True,exist_ok=True)
for font in Path('client/assets').glob('inter-*.ttf'):
    shutil.copyfile(font,folder/font.name)
shutil.copyfile('client/build-x360/tube360.xex',folder/'tube360.xex')
(folder/'autoplay.txt').write_text('wWC8wVI4E3E 5\n')
(folder/'settings.txt').write_text('lang=pt\nserver=off\nsound=tv\nsponsorblock=button\n')
PY
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 mkdir 'HDD:\F34-SponsorBlock'
for asset in .work/console-f34/*; do
  python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 put "$asset" "HDD:\\F34-SponsorBlock\\${asset##*/}"
done
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 run 'HDD:\F34-SponsorBlock\tube360.xex'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 get 'HDD:\F34-SponsorBlock\tube360.log' .work/proofs/console-f34.log
```

The commands are documentation and were **not executed by this task**. Confirm the current console IP;
the log basename follows the launched XEX (`tube360.log` here, `default.log` when launched as `default.xex`).

1. At 7.775 s, see the Series **Y Pular patrocínio** prompt and the timeline mark. Y seeks to 20.877 s.
   Relaunch without pressing Y: the button disappears naturally at 20.877 s.
2. Open Settings → SponsorBlock; inspect all three choices, community warning and attribution. Check
   pt/en/es, large text and 480 / 4:3. B returns to settings, then browsing. Restart and verify saved mode.
3. Set `sponsorblock=auto`, relaunch: skip occurs at the start; **Patrocínio pulado / Y Voltar** appears
   for six seconds after playback resumes. Y returns to 7.775 s and plays through without another skip.
   Repeat without Y: prompt expires. Outside the prompt Y still changes quality.
4. Set Off, relaunch: no marks/button/skip or request. API failure also has no error on screen.
5. With the coordinator's isolated Nest setup, repeat button/auto/undo; verify picture and actual speaker
   both seek and remain synchronized. This task never accesses the household Nest.
6. Pair a phone to this temporary app and transmit the same video. Repeat auto/undo and check the phone
   position follows the seek. Test pause/manual seek/next and no segment state leaking to the next video.
7. Logs: `SponsorBlock: 1 trechos`, `SponsorBlock: skip <pos> -> 20.877`,
   `SponsorBlock: undo <pos> -> 7.775` and normal `primeiro quadro` messages.
   API/TLS failure yields `SponsorBlock: 0 trechos status=<reason>` with transport codes and error text. Check clock/DNS and certificate chain
   **on the console** if PC succeeds. Compare first-frame time in Off/button (fetch must not block it).

PC proofs do not establish Xbox performance/certificate validation, physical CRT overscan or real phone/
Nest interoperability. Final console qualification remains with the coordinator.
