# Native console paths and optional server protocol

Tube360 v0.3.0 runs YouTube on the Xbox. No `server.txt` beside the XEX (and no desktop `XT_SERVER`)
means **console only**, including account, Music, phone Cast and Nest: no HTTP requests to a PC.
Without a server, the console owns the sleep deadline and end-of-video shutdown too.
`settings.txt` can additionally force `server=off`; `server=auto` only permits a configured reserve.
The first line of `server.txt` is `<IP> <port>`, for example `192.0.2.10 8091` (replace the documentation IP).

## Native ownership

| Function | Console path |
|---|---|
| YouTube browsing/account | HTTPS InnerTube/catalog requests in `client/src/yt/`; TV OAuth device code, refresh and protected local session in `yt-console-account.json`. No password is entered on Xbox. |
| Video | Original AVC/H.264 and AAC, direct ranged VOD or segmented HLS, decoded locally by FFmpeg. Adapt source/rate before the configured server reserve. |
| Audio selection | Original before automatic dubbing; offered language/role catalog used for player selection and direct speaker audio. |
| Source/output | `video_mode=auto|720p60|1080p30` selects source preferences; `output_height=720|1080` selects framebuffer size. 1080 needs dashboard HDMI 1080p progressive. These are distinct settings, not guaranteed throughput. |
| Account navigation/Music | Native TV account and public catalog; local queues, history, likes, subscriptions, library, Music and audio-only playback. Music can fall back to public shelves if its endpoint rejects the TV token. |
| Phone | Native DIAL/SSDP and YouTube lounge receiver; identity files `cast-device.txt` / `cast-screen.txt`. See Cast from the phone below. |
| Nest | Cast v2 TLS directly from Xbox, configured in two-line `nest.txt`; the speaker fetches YouTube audio. See Direct Cast below. |
| Sleep | Console-owned deadline and end-of-video shutdown with no server. A configured server can own the reserve timer; both paths keep warnings, fades and shutdown on Xbox. |

Local runtime files (account grants, Cast identities, Nest ownership journal, history, settings and logs)
are private; ship only the inactive examples in `deploy/xbox/`. Examples for `nest.txt` and `server.txt`
put values on the first lines because those readers do not skip leading comments.

## Optional PC reserve

The remainder documents the legacy/reserve **HTTP/1.0 TCP** API on port **8091**, without TLS or chunked
encoding. This API is used only with a configured/enabled server. Its account/navigation bridge and timer
are separate from the native session. It can remux original AVC/AAC; MPEG-1/MP2 is the converted compatibility
path, not normal console playback. [Current architecture](STANDALONE.md) · [Installation](../README.md)

## Text rules

List responses are `text/plain; charset=utf-8`: one item per line, fields separated by TAB, lines ending in `\n`. No field
ever contains a TAB or a line break (the server replaces them with spaces). The first line is a header. Errors are
`erro\t<short message>`; messages follow the requested `lang` (`pt`, `en` or `es`), or `language` in `config.json`.

## Feeds

`GET /api/feed?kind=home|search|subs|history|later&q=<text>&n=24&offset=0&fresh=0|1&lang=pt|en|es`

```
ok\t<count>\t<feed title>
<id>\t<duration s>\t<channel>\t<formatted views>\t<title>\t<resume position s>\t<membership>
...
```

- `kind=home`: personalized recommendations when the account is connected. If the account service fails, the server
  uses searches from `home_queries` in `config.json`. Without an account it uses those searches too.
- `kind=search` needs `q` (percent-encoded UTF-8).
- `kind=subs`: subscriptions; `kind=later`: Watch later (`WL`). Both require an account; otherwise the response is
  `erro\tConecte sua conta do YouTube.` (`erro\tSign in to your YouTube account.` in English).
- `kind=history`: account watch history when signed in, local console history otherwise. A failure loading signed-in
  history is reported as an error; it does not silently replace the account's list with local history.
- `offset` is the number of filtered videos to skip, starting at 0; `n` is the page size (1–100, default 24).
  The server caches and joins variable-sized YouTube continuation pages, so offsets 0, 24, 48… work for every kind.
  End of list returns `ok\t0\t<title>`. Account list order is preserved; duplicate videos, Shorts, live streams and
  entries without video IDs are excluded.
- `fresh=1` at offset 0 reloads an account list and its continuation cache. The console sends it on **X** (Refresh).
- `lang=pt|en|es` changes list titles, error messages and views (`1.9M views` in English). It defaults to the config's
  `language` (`en` in the example config). It does not translate video titles or change `home_queries`.
  yt-dlp extraction uses the requested language; player metadata and the Node TV client use the config language
  (`pt`/BR, `en`/US or `es`/MX).
- The 6th field is the resume position in seconds (may be fractional), 0 if unavailable. For account lists the TV
  thumbnail overlay's watched percentage is multiplied by the duration, so the position is approximate. For local
  history it is the last console position. Thumbnails from all kinds enter the same prefetch queue.

The optional 7th feed field and 9th navigation item field are membership states: empty for ordinary videos,
`members` for members-only content with unknown/denied access, and `member` only when YouTube explicitly
sets `isPlayable: true` on that members-only card. Missing fields from older servers mean ordinary content.
A watch endpoint or a channel subscription does not prove paid access. Cards show localized Members/Member
badges; playback still asks YouTube, and a membership refusal shows a specific message without rotating clients.
The account JSON API carries the same optional `membership` field; yt-dlp `availability=subscriber_only` maps
to `members`. Catalog state never grants access or replaces YouTube authorization. With an active console account, member
cards request the player through the existing TV Samsung OAuth session on the worker; the returned signed
formats use the same AVC/AAC selection. Tokens remain inside ConsoleAccount and never enter list responses.
The internal `/api/video/<id>/player` ConsoleAccount request is not a server HTTP endpoint.

## YouTube account

The console uses only GET requests:

| Request | Response |
|---|---|
| `GET /api/account` | `ok\t<state>\t<name>\t<code>\t<url>\t<avatar art key>` |
| `GET /api/account/login` | Starts TV device-code sign-in; returns the same fields. Repeated requests while pending return the same code. |
| `GET /api/account/logout` | Signs out, removes the local token and returns the same fields. |

`state` is `signed_out`, `pending` or `signed_in`. Empty fields remain present (including trailing TABs).
`name` is cached from YouTube account info; `code` and `url` are populated only while pending. Confirm the code at
`https://www.google.com/device` using any browser; a phone is optional. An expired code returns to `signed_out`.
If the Node account service cannot be reached, all three routes return a signed-out state with empty fields.
Responses use `Cache-Control: no-store`.

The two mutating GET routes require the native console's `User-Agent: Tube360/1.0` and reject browser `Origin` or
`Sec-Fetch-Site` headers. Browser navigation cannot sign the owner in or out. The status page's
`POST /api/account/login` requires a same-origin `Origin` header and rejects cross-origin fetch metadata.
For example, native tools can log out with `curl -A Tube360/1.0 http://<server>:8091/api/account/logout`.
These checks prevent browser CSRF; they do not authenticate other devices on the LAN.

Account status has a two-second wall-clock timeout; other account calls have 25 seconds. Loading all continuation
pages for one Python feed request, including waiting for its per-kind lock, also has a total 25-second deadline.

The account service runs **inside the Cast receiver process**, binds only to `127.0.0.1`, and uses
`TUBE360_ACCOUNT_PORT` (default 8099). Python reaches it through `TUBE360_ACCOUNT_URL` (default
`http://127.0.0.1:8099`). No third service is required. For isolated tests, `node cast/account.mjs` starts only this
HTTP module: no DIAL advertisement and no Cast pairing code. `TUBE360_ACCOUNT_TOKEN` overrides
`~/.cache/tube360/yt-conta.json` (atomic writes, mode 600); `TUBE360_CONFIG` overrides the server config in both runtimes.
`node cast/conta.mjs entrar|testar` is an optional CLI using the same module.

Local JSON API (for Python, not the console):

- `GET /status` → `{state, name, handle, code, url, avatar}`.
- `POST /login` and `POST /logout` → same object.
- `GET /feed?kind=home|subs|history|later&page=0&fresh=0|1` →
  `{items: [{id, title, channel, duration, views_text, pos}], more: boolean}`. Pages are zero-based, variable-sized,
  use TV `/browse` continuations, and are cached per kind. Empty pages can still have `more: true` after filtering.
- `POST /watched` with `{id, pos?: seconds}` records a playback using `VideoInfo.addToWatchHistory`; optional `pos`
  also calls `updateWatchTime`. `POST /progress` with `{id, pos}` updates time without recording another start.
  Both return `{ok: true}` on success; failures return a non-2xx JSON `{error: <code>}`.

When `Remote.poll` detects a new playback ID (or a new playback session after a disconnect), Python sends `/watched`
once in the background. It sends `/progress` every 30 seconds and when leaving/ending a video. These requests never
block the console poll, and a YouTube failure does not interrupt playback. Local console history is still saved.

The Node bridge uses a Samsung TV client identity for player tracking while retaining the TV grant for account
browsing. `/watched` records the start and `/progress` updates the position. Missing playback tracking returns
HTTP 502 `{error: "tracking_unavailable"}`; a failed write must not be presented as confirmed history sync.
This server-side compatibility path is separate from the native console account.


## Server status page

`GET /` returns a small server-generated HTML page in the config language, titled with `brand`. It shows server
availability, time since the last console poll, configured Nest, the current Cast TV pairing code, and account state.
While pending it shows the account device code and confirmation link. When signed out, its form submits
`POST /api/account/login`, which starts sign-in and redirects to `/` (303). The page refreshes every 15 seconds and has
no JavaScript or external assets. Browser playback controls have been removed; Cast's JSON command/state API remains.

For tests, `TUBE360_PORT=8094` changes the Python listener (default 8091), and `TUBE360_WARM_CACHE=0` disables background
search warming. Use an isolated HOME to keep test playback out of the owner's local history.

## Thumbnails

`GET /api/thumb/<id>?w=320&h=180` returns a **baseline** (not progressive) JPEG, exactly `w`×`h`, cropped to fill. On
error: HTTP 404. The server prefetches thumbnails for every list it serves.

## Video

`GET /api/stream/<id>?t=<seconds>&q=1080|720|480|360&a=0|1&speaker=0|1` returns `video/mpeg`: MPEG-PS with **one** `mpeg1video` stream and,
unless `a=0`, **one** `mp2` stream.

Clients with the FFmpeg decoder can append `&codec=h264`. Read `X-Video-Codec` before opening the decoder.
The value `h264` selects `video/mp2t` with the source AVC and AAC tracks copied into MPEG-TS (video only for
`a=0`). All other responses, including older servers with no codec header, use the MPEG-PS path below.
Original mode needs a matching SDR AVC rendition and a direct AAC source. Optional `mode=auto|720p60|1080p30`
selects the source: `720p60` favors frame rate within 720p; the other modes prefer 1080p at up to 30 fps,
then 720p (including 60 fps). `q=480|360` imposes a lower ceiling; for `q>=720`, `mode` decides the ceiling.
Live, incompatible sources and a failed remux
use the existing conversion, limited to 720p30 even when `q=1080`.

Optional `audio=original|lang:<language>|<track code>` selects the audio for both the stream and its speaker
session. The default is original, even if YouTube marks an automatic dub as default. The interface-language
preference uses `lang:pt`, `lang:en` or `lang:es` and falls back to original if unavailable. Canonical track codes include
the language and role, e.g. `en-original`, `pt-auto`; read the response catalog rather than inventing codes.
`X-Audio-Track` is the percent-encoded selected code; `X-Audio-Tracks` is a percent-encoded UTF-8 catalog with
one TAB-separated `code, language, name, original (0|1), automatic (0|1)` row per track. `X-Source-Fps` reports
the output source rate (30 for conversion). The native client builds the same catalog from YouTube/HLS.

The TS clock is relative to the requested position. A seek includes the previous keyframe. Decode the negative
PTS for reference, discard its pictures and trim negative-time PCM before starting the shared presentation clock.
The original and converted streams use the same session fencing, Nest MP3, TV audio reserve and captions clock.
If original decoding fails during playback, reopen at the displayed position without the capability.

| q | size | video |
|---|---|---|
| 720 | 1280×720 | 5.5 Mbit/s |
| 480 | 854×480 | 2.5 Mbit/s |
| 360 | 640×360 | 1.5 Mbit/s |

- Always a constant 30 fps, 2 B-frames; GOP of at most 30 at 720p and 15 at 480p/360p. Black bars if the source is not 16:9. Audio: MP2, 44.1 kHz stereo,
  192 kbit/s.
- Starts at `t` (fast seek) and streams while it converts: the first byte arrives in a few seconds.
- Timestamps start at 0. The console adds `t`.
- Headers include `X-Duration` in seconds, a percent-encoded `X-Title`, `X-Live: 0|1` and `X-Stream-Id`, an integer scoped to this server process.
- One stream at a time: a new request ends the previous one. When the console closes the connection, ffmpeg is stopped.
- Current consoles use `speaker=1` and retain MP2 for failover. A live stream produces the Nest MP3 from the same FFmpeg process. Older consoles can still request video only with `a=0`.
- Live ignores `t`. The server prefers an HLS rendition with both tracks and starts at its newest complete segment. Broadcast and CDN delay precede that segment. Resume reopens at the edge. The client can drain a small accumulated delay at up to 1.03× on TV. If presentation stops for over 1.1 s with an empty MPEG ring and less than 50 ms of PCM, the client checks when the last MPEG packet arrived. After 1.35 s of network silence on TV, or 3 s in speaker mode, it can reopen at the edge during the outage. Retries are at least 20 s apart.

## Remote control

The console polls about every 500 ms and reports its state:

`GET /api/remote/poll?playing=<id or empty>&pos=<s>&dur=<s>&paused=0|1&ended=<id or empty>`

The answer is one line:

| Line | Meaning |
|---|---|
| `none` | nothing to do |
| `play\t<id>\t<t>\t<title>[\tnavigation]` | play now; the optional fifth field identifies navigation queue playback, including automatic next |
| `pause` · `resume` · `stop` | |
| `desligar` | stop playback, show a two-second shutdown message and power down the Xbox (see Sleep timer). |
| `seek\t<delta s>` | relative seek, may be negative |
| `aviso\t<text>` | show a short on-screen message (e.g. "phone connected") |

The Cast receiver uses this JSON API:

- `POST /api/remote/cmd` with `{"action": "play"|"queue"|"pause"|"resume"|"seek"|"stop"|"next", "id"?: str, "t"?: num, "delta"?: num}`
- `GET /api/remote/state`: what the console reported last (video, position, duration, paused, title, ended), plus the queue.

## Cast from the phone

The native receiver runs with both `server=off` and `server=auto`. It advertises `YouTube 360` through SSDP UDP
1900 and DIAL TCP 8134 (`/ssdp/device-desc.xml`, `/apps/YouTube`). The Phone panel uses its native lounge TV code.
Identity files are `cast-device.txt` and `cast-screen.txt`; lounge tokens stay in memory. A phone request starts
direct playback even when PC reserve is enabled, preserves account history and replaces Music/navigation queues.
Pending navigation replies and old server navigation play commands cannot take that phone session back.

Sleep expiry has priority over phone and navigation commands. At natural end with an end-of-video timer, the
client reports the end to the timer and suppresses lounge queue advancement while waiting for shutdown. Local
navigation can take playback back from the phone; the lounge then clears its previous queue.

Phone volume/mute controls both the TV gain and the direct Nest receiver. The Nest keeps its existing volume until
a phone volume command or timer fade changes it. Its initial volume is reported to the phone. Timer gain multiplies
the selected volume, and cancellation/confirmed stop restores that selected level. Volume messages use the Cast v2
receiver namespace, independently of playback PAUSE/PLAY/SEEK.

Desktop fixtures use `XT_CAST_DISABLE=1` to suppress discovery and lounge traffic when testing other features.
`cast-test-app` reuses the production main/player/render object, replaces YouTube transports with loopback adapters,
and restricts discovery to DIAL 8134 and SSDP 1934 on loopback. Production Xbox discovery remains on UDP 1900.

- `GET /api/cast/code` → `ok\t<TV code>\t<connected sender name or empty>`. The console's Phone panel shows it.
  The receiver (`cast/receptor.mjs`) writes a new code to `~/.cache/tube360/cast-code.txt` every 5 minutes.
- `POST /api/cast/event` with `{"kind": "connect"|"disconnect", "name": str}`. The receiver calls it when a phone
  connects or leaves, and the console shows a message.

## Google Cast speaker

Set in `config.json` as `"nest": {"host": "<IP>", "name": "<name>"}`.

| Request | Answer |
|---|---|
| `GET /api/nest/info` | `ok\t<name>` or `erro\t...` |
| `GET /api/nest/prepare` | wakes the speaker and opens the media receiver ahead of time |
| `GET /api/nest/play?id=<id>&t=<s>&sid=<X-Stream-Id>&audio=<selection>` | `ok\t<revision>`; start at absolute `t`, using a Cast seek within the current VOD resource when possible |
| `GET /api/nest/pos?sid=<id>` | `<state>\t<absolute position s>\t<error>\t<revision of gated MP3 reader or 0>`; states include `LOADING`, `PLAYING`, `PAUSED`, `BUFFERING`, `STALE`, `STALLED`, `IDLE` and `ERRO` |
| `GET /api/nest/local?sid=<id>&rev=<revision>` | `WAIT\t<revision>` or `QUIET\t<revision>`. Reserves the TV output and revokes the previous Cast URL. Use the returned quiet revision as `claim` on the next play. |
| `GET /api/nest/unmute?sid=<id>&rev=<play revision>` | Open the waiting MP3 body after the TV mutes and its device buffer drains. Returns `ok\t<absolute position s>` on a fresh PLAYING reply for that media session, 409 on obsolete ownership or 503 on an unconfirmed start. |
| `GET /api/nest/pause` · `resume` · `stop` | |
| `GET /api/nest/volume?v=0..100` | |
| `GET /api/audio/<id>?t=<s>&track=<selection>` | the selected MP3 the speaker downloads (supports `Range`) |

A receiver status supplies its own update timestamp. While the receiver keeps reading its MP3 (any bytes in the last 8 s), the server keeps extrapolating a playing position, even if the Default Media Receiver sends no new status (it only does so on changes or every few seconds). Without MP3 reads, it extrapolates for at most 1.2 s, then returns `STALE`, and after 8 s without status progress or MP3 reads returns `STALLED`; only `STALLED`, `IDLE` or an error let the Xbox hand the sound to the TV. The Xbox discards normal position samples with a round trip above 120 ms. A server advertising `X-Nest-Handoff: 1` arbitrates the two outputs. A stale speaker clock leaves local PCM muted. If a LOAD could have emitted sound, the server waits for a fresh quiet status after revoking its URL and stopping the receiver. The server records the possible owner on disk before a LOAD so that restarting the process still requires confirmation. A control-channel partition can therefore leave a silent interval after the receiver runs out of buffered MP3.

The player trims or holds the muted PCM queue against the PTS of the displayed frame. It enables the TV when the server has granted it ownership and the local audio clock is within 22 ms of that frame. For a return to the speaker, the console requests a future frame with `play&claim=<quiet revision>`. The Cast receiver can open its unique URL while the TV is playing. The server withholds the MP3 body until the console mutes the TV, waits for the local device to drain and reaches the selected frame. On Xbox the driver queues eight 256-sample blocks at 48 kHz. The console also checks the kernel’s consumed-block counter across the queue, the mixer and the last submitted block. The server then confirms playback of that specific Cast session. A VOD handoff starts with a clip cut at the requested position. A live handoff uses a complete MP3 frame from the current encoder.

The optional `sid` on play, position, pause, resume and stop is the stream ID from the response header. A late stop for an older playback has no effect on a newer speaker session. A fenced or expired play receives 409. Stop revokes the speaker's media URL immediately. The server retries an undelivered Cast stop when the media channel returns and also stops orphaned playback when console polls disappear. Audio already buffered on the receiver can finish during a network partition.

VOD uses Cast `BUFFERED` with the known remaining duration. A seek to converted audio in the currently loaded resource preserves its URL and Cast media session. If the destination precedes that resource, lies beyond the converted prefix, or the receiver does not confirm SEEK, the server can LOAD an appropriate MP3. Live uses Cast `LIVE` and a session-specific provisional file. After a pause it starts again at the live edge.

**MP3 downloads.** Conversion writes `<id>_<t>.part.mp3` for original audio; other selections add a hash of the
selection to the ID. Jobs and cache keys are partitioned by selection. Changing language forces a Cast LOAD,
even for the same video and position; it cannot SEEK within the old language's resource. A successful conversion with a nonempty output promotes it atomically to `<id>_<t>.mp3`. Failed and abandoned outputs are regenerated. A complete target supports `HEAD` with length and byte-range GETs with a known total. For a growing file, an initial `bytes=0-` receives a continuous 200 response. Other single ranges receive 206 with the available extent and `*` for an unknown total. Unsupported range forms receive 200. Positions beyond the available data can receive 416.

Cast URLs carry an opaque `s`. A revoked URL receives 410. Small fixed ranges and completed unscoped downloads include Content-Length. A larger session-bound GET streams until EOF or revocation. A 206 still reports Content-Range. Delivery allows four seconds for startup or a range entrance and about 1.5 s ahead of the confirmed clock in steady playback, using the 192 kbit/s MP3 rate.

## Sleep timer

In console mode, `requestSleep` handles set/add/cancel locally using `SDL_GetTicks` and the same `SleepState`
wire representation below. The UI offers 15/30/45/60/90/120 minutes or natural end of video. The target is
always the Xbox; a saved notebook preference cannot power off a PC in console mode. The final 60 seconds
show a warning and fade volume; A adds 15 minutes and B cancels. Expiry stops playback and powers down the
console after a two-second message. The desktop build exits its own process instead. Timer state is volatile. End-of-video sleep suppresses repeat and automatic next; the notebook target requires the optional server.

The following HTTP contract applies to the **optional server path**. The server owns one volatile timer. It continues counting without console polls; restarting the server clears it.
All responses below are `text/plain`, with `Cache-Control: no-store`:

```
ok\t<off|on>\t<seconds remaining>\t<xbox|xbox_notebook>\t<minutos|fim_do_video>
```

| Request | Effect |
|---|---|
| `GET /api/sleep` | Read the timer. |
| `GET /api/sleep/set?min=15&what=xbox` | Replace the timer with 15–240 whole minutes. |
| `GET /api/sleep/set?end=1&what=xbox_notebook` | Bind to the current video's natural `ended` event; 409 if no video is playing. |
| `GET /api/sleep/set?target_only=1&what=xbox_notebook` | Change the target of a running timer, preserving its deadline (409 during shutdown). |
| `GET /api/sleep/add?min=15` | Add 15 minutes to the remaining time; end-of-video becomes a minutes timer. 409 without a running timer or known video duration. |
| `GET /api/sleep/cancel` | Cancel, including a pending notebook shutdown during its grace period. |

`what` defaults to `xbox`. Mutating GETs require `User-Agent: Tube360/1.0` and reject `Origin` / `Sec-Fetch-Site`,
using the same browser CSRF protection as account actions. Invalid parameters return 400/422; rejected CSRF returns 403.
These checks do not authenticate LAN clients. `min` and `end=1` are mutually exclusive.

For end-of-video mode, seconds are estimated from the current video's duration and position (including while paused),
or `-1` if duration is unknown. A connected console's `ended=<bound id>` triggers expiry, overriding queued autoplay;
the estimate alone never expires the timer while polls continue. If polls stop for 60 seconds and the last estimated
end has passed, the timer expires too. An unknown duration needs `ended` or cancellation.

At expiry, the server stops active Nest playback, clears queued commands/videos and gives the next console poll the
priority line **`desligar\n`**. The console stops playback, displays “Shutting down…” for two seconds, then resolves
`xboxkrnl.exe` ordinal `0x28` and calls `HalReturnToFirmware(5 /* HalPowerDownRoutine */)`. The PC build logs the
command and exits the app. It never powers down the PC itself.

With `what=xbox_notebook`, the server waits approximately 20 seconds after expiry, logs the action and runs
`systemctl poweroff` without `--force`. `TUBE360_POWEROFF_CMD` overrides the executable and arguments (parsed with
`shlex.split`, no shell). Shutdown failures are logged. During those 20 seconds the timer remains `on` with zero
seconds so the status page can still cancel the notebook shutdown. For `xbox`, expiry immediately changes it to `off`.
The minutes deadline applies even when the console has been absent for 60 seconds or longer.

The client reads `/api/sleep` on a dedicated worker about every five seconds. Settings offer Off, 15, 30, 45, 60, 90,
120 minutes and At end of video, plus Xbox only / Xbox and notebook. Only that target preference is saved in
`settings.txt` as `sleep_what=xbox|xbox_notebook`; the timer itself is never saved by the client. In the player,
the focused timer icon opens the timer chooser, **← →** selects, **A** applies and **B** closes.

During the final 60 seconds a card offers **A +15 min** and **B Cancel**, and the SDL stream gradually fades, restoring
its original gain on cancellation or extension. The server applies the same fade to the Nest through
`/api/nest/volume?fade=<0..1>` (the existing `?v=<0..100>` is unchanged), retaining its original volume and restoring it
when the timer is cancelled, extended or stopped. The server also fades when console polls have stopped.
The TV is not controlled: the UI recommends Samsung's Eco “No Signal Power Off” setting.

The status page shows the timer, target and countdown (or end-of-video mode). Its form posts to
`POST /api/sleep/cancel`, using the same same-origin `Origin` and fetch-metadata checks as `POST /api/account/login`,
and redirects to `/` with 303. The page refreshes every 15 seconds.

For isolated tests **only**, `TUBE360_SLEEP_TEST=1` allows positive fractional minutes up to 240 (including a one-minute
warning test). `TUBE360_SLEEP_TEST_DELAY` can shorten the notebook grace period only in that mode; its default is
still 20 seconds. Always set `TUBE360_POWEROFF_CMD` to a harmless marker-writing executable for tests. Never test
shutdown against a real console or the owner's live server. Example (test listener 8094):

```bash
curl -A Tube360/1.0 'http://127.0.0.1:8094/api/sleep/set?min=1&what=xbox'
curl -A Tube360/1.0 'http://127.0.0.1:8094/api/sleep/add?min=15'
curl -A Tube360/1.0 'http://127.0.0.1:8094/api/sleep/cancel'
```

## Player focus and captions

This section supersedes the earlier player D-pad shortcuts. A hidden control bar consumes the first direction
solely to reveal the timeline. On the timeline, Left/Right seek 10 seconds and A pauses/resumes. Down focuses the
options row (Play/Pause, Captions, Quality, Sound, Sleep timer, Audio, Like, Dislike, Go to channel, and Live when behind).
Left/Right move focus across both groups. A activates the focused control. Up returns to the timeline.
B closes one level at a time, from panel to options, timeline, hidden bar and finally out of the video.
The bar hides after **5 seconds** without interaction on the timeline, including while paused. Options and panels
hold it open. The next reveal always focuses the timeline. A/Menu, Y, X, LB/RB and LT/RT retain their playback
functions; View still leaves directly. Modal A applies the focused choice. Seeking, including phone seeking, is
blocked while `Player::state == CONNECTING`, before `X-Live` is known; live seeking remains blocked afterwards.

The player timer has a primary time row and a secondary target row. Up/Down choose the row, Left/Right change it.
A on the time row applies; A on the target row changes the saved preference. Both the player and Settings use
`Settings::sleepNotebook`, saved as `sleep_what`. Changing a running timer's target calls the existing
`/api/sleep/set?target_only=1&what=…`, preserving the deadline. B returns to the options row.

**Go to channel** closes the video and opens the channel page (by channel ID, or resolved from the video when the
ID is unknown).

### GET /api/captions/{video_id}?lang=pt|en|es&track=<optional track code>

Read-only, `text/plain; charset=utf-8`, `Cache-Control: no-store`. Uses the existing ID validation. `lang` defaults
to `en`; the client always sends its interface language. Track codes accept `[A-Za-z0-9_-]{1,32}`.

```
ok\t<selected code or empty>\t<manual|auto|none>\t<live 0|1>\t<reason or empty>
meta\t<title>\t<channel>\t<upload date YYYY-MM-DD>\t<channel ID or empty>
track\t<code>\t<display name>\t<manual|auto>
chapter\t<absolute start seconds>\t<title>
cue\t<absolute start seconds>\t<absolute end seconds>\t<text>
```

Fields obey the text rules above. Only one `meta` line; repeated `track`, `chapter` and `cue` lines. Empty fields
are retained. Cues are sorted, nonoverlapping and use half-open intervals `[start, end)`. VOD timestamps are
absolute video seconds, **not relative to the current seek**. Metadata and available tracks remain present when a
requested language is missing. Reasons are `unavailable`, `language_missing`, `live_unavailable`, or `live_clock`.
Extraction/download failure returns `erro\tfailed\n`; oversize output returns `erro\ttoo_large\n`. Invalid IDs or
track codes return HTTP 400; invalid interface language returns HTTP 422. Errors do not stop video playback.

Selection prefers the interface language, creator captions within that language, then original ASR (`*-orig`)
before translated ASR, then other languages (creator captions first). Creator captions replace automatic captions
with the same code. Explicit `track` selects that exact offered code; it never silently changes languages. The
caption panel offers on/off and a language row; changing the language enables captions and saves the code. If a
saved code is missing on another video, available choices remain accessible in the panel.

The server uses yt-dlp without downloading video for caption discovery and fetches JSON3 (preferred) or VTT in a
worker. HTML, inline VTT timestamps and control characters are stripped. Automatic rolling windows remove only
the overlapping suffix/prefix of adjacent cues; repeated speech after a gap is kept. JSON3 append events are
joined. Long phrases become consecutive chunks of at most 100 characters (except an unbroken word), with their
time divided proportionally, so the two-line display need not discard the rest of the sentence.

Limits: 25-second route deadline, 12-second caption download timeout, 2 MiB download/output, 20,000 cues, 256
tracks and 128 chapters. Discovery has a 15-minute cache of 16 videos/interface languages; parsed VOD tracks have
a 15-minute cache of 24 selections. These caches are independent of the production stream resolver, so discovery
can require one additional YouTube extraction when opening a new video. Choosing an original ASR track matters:
YouTube may return HTTP 429 for the translation into its own language while the original still downloads.

The console's dedicated `cc::Loader` fetches and parses outside rendering, discards obsolete request revisions and
caps its response at 2 MiB. Rendering performs a binary search using `Player::position()` **after `take()`**, so CC
follows the displayed frame, pause, seeking and the Nest-controlled picture clock. Wrapping and line widths
are cached until the active cue changes. Option labels, values and their widths are cached until their text changes.
A translucent dark box shows at most two lines, above the options/bar
while visible and near the bottom otherwise. Modal panels suppress captions, loading, pause and error overlays.
The preferences are `captions=on|off` (default off) and `caption_lang=<code>` (empty follows interface language).

### Live captions

YouTube live timedtext may use seconds since the broadcast began, while the current MPEG stream starts at zero.
The current Player/stream API provides no shared HLS origin or mapping for these two clocks. For a live with
tracks, this route returns metadata/tracks with `reason=live_clock` and no cues; the CC option is disabled with a
localized explanation. A live without tracks uses `live_unavailable`. **Live CC is not yet supported**, even if
YouTube offers a track. Integration must expose the MPEG-to-broadcast timestamp origin, then map live cues and
refresh their rolling window. Guessing an offset from wall time would give incorrect captions.

## Navigation: channels, library and Music

`GET /api/nav?kind=<kind>&id=<browse ID>&q=<query>&tab=videos|live|playlists&filter=0..3&offset=0&n=24&fresh=0&lang=pt|en|es`

Kinds: `channel`, `subscriptions`, `library`, `playlist`, `history`, `later`, `search`, `music`, `music_search`,
`album`, `artist`, `radio`, `music_playlist`. Channel/artist IDs are `UC…`; album IDs are Music browse IDs;
playlist IDs accept the `VL` prefix. Music uses YTMUSIC on the signed-in session when available. Signed-out browsing uses a guest.
If the paired token is rejected with 401/403 on Music Home, the service requests the same browse ID with the signed-in TV client.
Account lists and channel actions use TV credentials. Subscriptions, library, history and Watch later require sign-in.

```
ok\t<title>\t<subtitle>\t<art key>\t<subscribed 0|1>\t<more 0|1>\t<channel ID>\t<banner key>\t<history art key>\t<watch later art key>
<type>\t<id>\t<title>\t<byline>\t<duration s>\t<art key>\t<channel ID>\t<section>\t<membership>
```

Item types are `video`, `channel`, `playlist`, `album`, `artist`. Empty fields remain present. Lists keep YouTube's
order and deduplicate within each type. `offset` counts all returned items (including subscription avatars).
`more` is explicit; short or empty continuation pages do not imply the end. Errors are `erro\t<localized message>`.
The client keeps parent pages, focus, tab and scroll when opening a child; B pops that page without a network reload.
The client requests another page when an avatar-only page has a continuation.
Library adds History, Watch later and Mixes before account playlists. History uses the most recently watched video.
For visible playlists and Watch later the artwork worker requests the newest entry when the service offers that sort,
or the final playable entry in playlist order. A radio uses its first track. Empty or unavailable lists use their
supplied cover when available. Preview lookup is bounded and shared across thumbnail sizes.

For channel pages, `tab` selects the corresponding TV shelf and that shelf's continuation, excluding Shorts.
Search `filter`: 0 all, 1 videos, 2 channels. Music search: 0 all, 1 songs, 2 albums, 3 artists.
For `music`, `q=charts` follows Explore’s Charts endpoint, `q=mixes` selects playlists from the Music home,
and empty `q` retains the order and shelf headings of Music Home. The client stores the horizontal position per shelf.
Shelf titles retain YouTube's language; subscriber/video/view
counts and album labels in page subtitles follow the requested language.

`GET /api/nav/channel?id=<video ID>&lang=…` resolves a video's channel and returns its first channel page.
The card's Menu button opens channel / Watch later / radio actions without changing X or Y.

Mutating native GETs below require exactly `User-Agent: Tube360/1.0` and reject `Origin` / `Sec-Fetch-Site`, as account
and timer actions do. These checks prevent browser CSRF; they do not authenticate LAN clients.

- `/api/nav/action?action=subscribe&id=<channel ID>&value=0|1&lang=…`: explicit target state; unsubscribe requires
  confirmation on the console. `/api/nav/action?action=later&id=<video ID>` saves to the account's Watch later.
  Success is `ok\n`; failure is `erro\t<message>`. No success is shown before YouTube confirms the action.
- `/api/nav/play?kind=playlist|album|radio|music_playlist|later&id=…&start=0&lang=…`: loads up to 256 playable items,
  then atomically replaces `remote.queue`, queues the first `play`, and returns the navigation response containing
  those playable items. `start` indexes playable videos, not collection cards. A failed load preserves the old queue.
  The existing console `ended` poll advances this queue; these starts use the remote path to suppress local autoplay.
- `/api/nav/transport?action=next|stop|clear`: queue transport. Stop clears queued videos and pending play/next/stop commands. Clear does that without stopping the current stream.

Phone play/stop replaces navigation playback, discarding its ready queue and pending commands and cancelling a
list still loading. Phone queue entries remain separate. Sleep expiry also cancels a navigation list still loading.
- `/api/nav/lyrics?id=<video ID>`: `ok\t<plain lyrics or empty>`. Lyrics are optional, unsynchronized, and may be
  unavailable without Premium. A failed lyrics request does not interrupt playback.

Artwork keys prefixed `~` use `GET /api/thumb/nav/<key without ~>?w=320&h=180` (384×216 for three columns,
640×360 for the 350px Now playing cover). They are baseline JPEGs
in the same disk directory and request cache as video thumbnails. Only allowlisted YouTube image hosts supplied by
the account service are accepted; the client cannot submit a URL, and redirects are refused. Square artwork is
letterboxed inside the existing 16:9 GPU slot and cropped using UVs, so it needs no additional GPU cache or textures.
The shared allocation budget remains `64 * 320 * 180 * 4 = 14,745,600` bytes; actual Vulkan page allocation determines
the slot count. Now playing reconfigures that same pool to 640×360, reducing its slot count within the same budget;
returning to browsing restores the configured grid size. There is no second texture pool. Only visible cards,
visible avatars and the current cover request artwork. Channel banners request exactly 1184×160 from the same worker.
While a banner is visible, its Vulkan allocation is deducted before calculating the number of ordinary thumbnail slots.
Avatar masks use the same centred texture crop as square covers.

The live player shows a grey LIVE indicator on pause or once the displayed video is a second behind its clock.
Its additional Live option reopens the same stream at the edge. At the edge the indicator is red.

With `server=off`, account routes use the console TV session in `yt-console-account.json`. Music receives the
current access token on login, refresh and logout; the token never appears in the text protocol. Changing it clears
catalog caches and discards navigation responses from the previous token. Public channel/playlist requests use the
native catalog first; a failed account playlist request can use the TV session, including private library playlists.

Both Node and native Music requests use `music.youtube.com`, the YTMUSIC/WEB_REMIX identity and the TV session's
OAuth Bearer without browser cookies. HTTP 400/401/403 fall back to public Music while the YouTube account stays
signed in. The native account navigation header includes banner, latest history video and latest Watch later video,
matching the server header used for channel banners and library artwork.

Now playing decodes only AAC, including when the console controls a Nest directly. The muted PCM reserve follows
the speaker clock and becomes audible only after Cast confirms silence. The PC audio reserve accepts AAC or MP2
without decoding video. B stops and clears the local queue, returning to the source page. Show video reopens at the
current position, preserving pause; B through the player's focus levels returns to audio at that position. Lyrics
can be scrolled with up/down. Account history also records audio-only sessions.

Local JSON account additions: `GET /nav` with `kind,id,q,tab,filter,page,fresh`, returning
`{title,subtitle,image,banner,history_video,later_video,channel_id,subscribed,items:[{type,id,title,channel,duration,image,channel_id,section}],more}`;
`GET /playlist_preview?id=…` → `{video: videoId or ""}` (signed in);
`GET /channel_for_video?id=…` → `{id: channelId}`; `POST /nav/action?action=subscribe|later&id=…&value=…` → `{ok:true}`;
`GET /lyrics?id=…` → `{lyrics}`. Images here are source URLs; Python replaces them with opaque cache keys.
Navigation caches are bounded to 24 lists and 41 continuation pages per list, expire after five minutes, and are
separated by account identity. An account reset or successful action invalidates the relevant account cache.

PC capture helpers (no console effect): `XT_NAV`, `XT_NAV_ID`, `XT_NAV_QUERY`, `XT_MUSIC=1` select a starting navigation
page. `XT_SHOT_MS` takes a screenshot after wall-clock milliseconds; `XT_KEYS_MS` spaces scripted keys by wall-clock
milliseconds. The original frame-based helpers remain available, with `S` added for Menu.

### Direct Cast from the console

A console using the native YouTube player can configure its own speaker through `nest.txt` beside
`settings.txt`. The first line contains the host and optional Cast TLS port (default 8009). The second
line contains the display name. For example, a LAN configuration can contain the following lines.

```text
192.0.2.20 8009
Google Cast speaker
```

Replace the documentation address with the speaker's actual LAN IP. Find it in Google Home device information or the router's DHCP list. Keep values on the first two lines, without leading comments. This makes the Sound setting available without `server.txt` and with `server=off`. The native player retains the
selected AAC/m4a audio offer even when the displayed MP4 already contains audio. It passes that
unchanged signed URL to the Default Media Receiver (`CC1AD845`) using Cast v2 TLS and protobuf.
The channel implements CONNECT, PING/PONG, receiver/media GET_STATUS, LAUNCH, LOAD, SEEK,
PAUSE, PLAY and STOP. A seek within the same URL retains the Cast media session. A new resource
waits for the previous playback to stop. Discovery currently uses the configured host.

The video source selects the only Cast sender: native YouTube playback uses the console; a server
reserve stream uses the server. Before native playback opens Cast, `GET /api/nest/owner?owner=console`
cancels the server's pending work and returns `WAIT\t<revision>` until its audio is quiet and its Cast
socket has closed, then `QUIET\t<revision>`. While delegated, server prepare, play and volume calls
cannot open Cast. A console with no previous reserve session can use Cast directly when the optional
server is unreachable. The attempt is bounded to 3.5 seconds; an older server's 404 or lack of confirmation cannot hold native playback hostage. Native LAUNCH replaces an idle/default receiver when needed. In the reverse direction the console confirms STOP and closes its TLS channel
before requesting `owner=server` (`ok\t<revision>`). Callers must preserve this order; the server
cannot observe whether an independent sender's socket is closed. Deploy the client and server together.

Initial LAUNCH/media confirmation is limited to 3.5 seconds. Missing confirmation releases the speaker
and continues the current video with TV audio and a short notice; an initial failure is retried with X.
The server limits blocking Cast connection and receiver commands to 2.5 seconds so a pending
pychromecast LAUNCH cannot keep a later ownership handoff waiting for its default ten-second timeout.
The video clock keeps advancing during setup and release. Preloading does not hold the picture while
waiting for Cast; only the local audio device drain may hold the transfer frame (device latency + 120 ms
maximum). An unacknowledged LOAD that could already be audible still needs positive STOP/quiet evidence
before unmuting the TV. This preserves single audio output during a network partition.

Each LOAD carries an ownership token in `media.customData.tube360`. Clock samples require that token, the exact loaded content URL
or an already verified media session. Command request IDs fence older clock replies across seeks.
The observation time advances only when an accepted MEDIA_STATUS contains `currentTime`. Fast
request/reply pairs use their midpoint (120 ms maximum round trip). Unsolicited status uses receipt
time. The picture extrapolates PLAYING observations and keeps the local PCM reserve muted. Short channel interruptions keep the previous speaker clock instead of immediately unmuting the TV.
The current native player requests release after 25 seconds without an accepted clock; lower-level media
progress checks can release earlier. BUFFERING also requires progress. A timeout alone does not establish silence.
A missing media namespace during reconnection waits for a complete receiver status.

The TV takes over after a fresh status confirms IDLE/empty media or an empty application list.
A timeout by itself retains the mute. Returning to the speaker preloads with `autoplay=false`,
then waits for the selected frame and for the TV device queue to drain before issuing PLAY.

The client writes `nest-owner.json` before LOAD. Leaving the video keeps the Cast worker alive to
retry STOP during navigation. Application shutdown waits up to 5 s for quiet confirmation and
retains an unfinished journal for the next launch. With an unreachable speaker and a direct YouTube
URL, audio can continue after the application exits. That URL cannot be revoked by the console.
Volume and mute from the phone and the temporary sleep fade apply through the current Cast owner.

Direct live HLS playback currently uses TV sound after speaker release. The native HLS reader's
zero-based video PTS and the receiver's event-relative `currentTime` need a verified common origin
before independent YouTube HLS playback can use the Nest clock.

`python3 bench/standalone/nest_owner_wire.py client/build/nest-test` exercises simultaneous native
and pychromecast senders against a threaded TLS/protobuf receiver, including a deliberate collision
control. `python3 bench/standalone/nest_owner_e2e.py` runs the desktop app with an isolated real
server on 8138, a fake account on 8238 and synthetic media. It covers rejected/silent LAUNCH, TV
continuity, optional/offline server, source handoff and X retry; captures and wire traces go to
`.work/owner-e2e`. Optional arguments select individual scenario names.

The desktop-only `XT_YT_REPLAY=<directory>` replaces YouTube responses with sanitized local
fixtures. `bench/standalone/cast_e2e.py <repo>` prepares synthetic
720p30 AVC/AAC media, drives the desktop app with `server=off`, serves AAC over local HTTPS on
8233, exercises a TLS/protobuf Cast receiver and writes captures under `.work/provas/`.
Run `python3 bench/standalone/cast_test.py client/build/nest-test` for channel, ownership and recovery
tests after building the `nest-test` target. The Python tests use the installed pychromecast protobuf
binding and OpenSSL to create a temporary certificate.

## Native player extras (server=off)

Comments and Return YouTube Dislike counts use HTTPS directly from the console; no PC routes are added.
The comments worker boots a public WEB context, posts the comments-section continuation to InnerTube `/next`,
and accepts both `commentRenderer` and entity-backed `commentViewModel` threads. Only the comments target can
supply a page token; reply and related-video tokens are ignored. Up/Down scrolls the text and moves between
comments; A on More comments loads the next page. The current page is replaced, bounded to 60 comments with
16 KiB of text each. Closing the panel leaves playback running. Late results from another video are discarded.

`https://returnyoutubedislikeapi.com/votes?videoId=<id>` supplies like/dislike counts in the corresponding
player tooltips. The dislike value is an estimate attributed in the interface to
[Return YouTube Dislike](https://returnyoutubedislike.com). The worker caches success for five minutes and failure
for 30 seconds, at most 32 videos. Network, HTTP and parse errors leave counts absent without interrupting playback.
Source: [API documentation](https://github.com/Anarios/return-youtube-dislike#api-documentation).

Playback speed has eight steps from 0.25x through 2x, local to the current video. A speed change reopens at the
current position and preserves pause. The Xbox FFmpeg build disables libavfilter, so the decoder worker applies
bounded-memory stereo WSOLA at 44.1 kHz. Video/captions/history follow the consumed PCM clock in source seconds.
Changed speed caps presentation at 30 pictures per wall second where the source provides enough pictures.
At speeds other than 1x the TV owns audio; the interface explains this when a Nest is selected. Returning to 1x
restores the selected output. Live streams keep speed 1x and disable the selector. Loop repeats the current VOD
before automatic-next/phone-queue advancement; an end-of-video sleep timer takes priority. Both reset for a new ID.

The comments bootstrap token follows the public
[YouTube.js protobuf schema](https://github.com/LuanRT/YouTube.js/blob/main/protos/misc/params.proto).
Desktop proof: `python3 bench/quality/prepare.py .work/extras-media --mode 60 --seconds 24`,
`cmake --build client/build -j4 --target player-extras-test tube360-extras-fixture`,
`client/build/player-extras-test`, then `python3 bench/player-extras/e2e.py`.
All test media/accounts/transports are isolated; the production executable does not contain the fixture transport.
