# Console ⇄ server protocol

The console app (`client/`) talks to the PC server (`server/`, port **8091**) over plain **HTTP/1.0 on TCP**, with no TLS
and no chunked encoding. It reads the server address from `server.txt` next to the `.xex` (`<IP> <port>`).

## Text rules

List responses are `text/plain; charset=utf-8`: one item per line, fields separated by TAB, lines ending in `\n`. No field
ever contains a TAB or a line break (the server replaces them with spaces). The first line is a header. Errors are
`erro\t<short message>`; messages follow the requested `lang` (`pt` or `en`), or `language` in `config.json`.

## Feeds

`GET /api/feed?kind=home|search|subs|history|later&q=<text>&n=24&offset=0&fresh=0|1&lang=pt|en`

```
ok\t<count>\t<feed title>
<id>\t<duration s>\t<channel>\t<formatted views>\t<title>\t<resume position s>
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
- `lang=pt|en` changes list titles, error messages and views (`1.9M views` in English). It defaults to the config's
  `language` (`en` in the example config). It does not translate video titles or change `home_queries`.
  yt-dlp extraction uses the requested language; player metadata and the Node TV client use the config language
  (`pt`/BR or `en`/US).
- The 6th field is the resume position in seconds (may be fractional), 0 if unavailable. For account lists the TV
  thumbnail overlay's watched percentage is multiplied by the duration, so the position is approximate. For local
  history it is the last console position. Thumbnails from all kinds enter the same prefetch queue.

## YouTube account

The console uses only GET requests:

| Request | Response |
|---|---|
| `GET /api/account` | `ok\t<state>\t<name>\t<code>\t<url>` |
| `GET /api/account/login` | Starts TV device-code sign-in; returns the same five fields. Repeated requests while pending return the same code. |
| `GET /api/account/logout` | Signs out, removes the local token and returns the same five fields. |

`state` is `signed_out`, `pending` or `signed_in`. Empty fields remain present (including trailing TABs).
`name` is cached from YouTube account info; `code` and `url` are populated only while pending. Confirm the code at
`https://www.google.com/device` using any browser; a phone is optional. An expired code returns to `signed_out`.
If the Node account service cannot be reached, all three routes return `ok\tsigned_out\t\t\t`.
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

- `GET /status` → `{state, name, handle, code, url}`.
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

Compatibility observed with 16.0.1 and TV device tokens: TV browse and account info work, but `/player` may respond
`UNPLAYABLE` ("The page needs to be refreshed") without playback tracking. In that case `/watched` and `/progress`
return HTTP 502 `{error: "tracking_unavailable"}`; the account history/time update has **not** been confirmed.
The library has `addToWatchHistory` and `updateWatchTime`, but neither can run without those tracking URLs.


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

`GET /api/stream/<id>?t=<seconds>&q=720|480|360&a=0|1&speaker=0|1` returns `video/mpeg`: MPEG-PS with **one** `mpeg1video` stream and,
unless `a=0`, **one** `mp2` stream.

| q | size | video |
|---|---|---|
| 720 | 1280×720 | 5.5 Mbit/s |
| 480 | 854×480 | 2.5 Mbit/s |
| 360 | 640×360 | 1.5 Mbit/s |

- Always a constant 30 fps, GOP 15, 2 B-frames. Black bars if the source is not 16:9. Audio: MP2, 44.1 kHz stereo,
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
| `play\t<id>\t<t>\t<title>` | play now (also sent after `ended` when the queue has a next video) |
| `pause` · `resume` · `stop` | |
| `desligar` | stop playback, show a two-second shutdown message and power down the Xbox (see Sleep timer). |
| `seek\t<delta s>` | relative seek, may be negative |
| `aviso\t<text>` | show a short on-screen message (e.g. "phone connected") |

The Cast receiver uses this JSON API:

- `POST /api/remote/cmd` with `{"action": "play"|"queue"|"pause"|"resume"|"seek"|"stop"|"next", "id"?: str, "t"?: num, "delta"?: num}`
- `GET /api/remote/state`: what the console reported last (video, position, duration, paused, title, ended), plus the queue.

## Cast from the phone

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
| `GET /api/nest/play?id=<id>&t=<s>&sid=<X-Stream-Id>` | `ok\t<revision>`; start at absolute `t`, using a Cast seek within the current VOD resource when possible |
| `GET /api/nest/pos?sid=<id>` | `<state>\t<absolute position s>\t<error>\t<revision of gated MP3 reader or 0>`; states include `LOADING`, `PLAYING`, `PAUSED`, `BUFFERING`, `STALE`, `IDLE` and `ERRO` |
| `GET /api/nest/local?sid=<id>&rev=<revision>` | `WAIT\t<revision>` or `QUIET\t<revision>`. Reserves the TV output and revokes the previous Cast URL. Use the returned quiet revision as `claim` on the next play. |
| `GET /api/nest/unmute?sid=<id>&rev=<play revision>` | Open the waiting MP3 body after the TV mutes and its device buffer drains. Returns `ok\t<absolute position s>` on a fresh PLAYING reply for that media session, 409 on obsolete ownership or 503 on an unconfirmed start. |
| `GET /api/nest/pause` · `resume` · `stop` | |
| `GET /api/nest/volume?v=0..100` | |
| `GET /api/audio/<id>?t=<s>` | the MP3 the speaker downloads (supports `Range`) |

A receiver status supplies its own update timestamp. The server extrapolates a playing position for at most 1.2 s, then returns `STALE` with the last observation. The Xbox discards normal position samples with a round trip above 120 ms. A server advertising `X-Nest-Handoff: 1` arbitrates the two outputs. A stale speaker clock leaves local PCM muted. If a LOAD could have emitted sound, the server waits for a fresh quiet status after revoking its URL and stopping the receiver. The server records the possible owner on disk before a LOAD so that restarting the process still requires confirmation. A control-channel partition can therefore leave a silent interval after the receiver runs out of buffered MP3.

The player trims or holds the muted PCM queue against the PTS of the displayed frame. It enables the TV when the server has granted it ownership and the local audio clock is within 22 ms of that frame. For a return to the speaker, the console requests a future frame with `play&claim=<quiet revision>`. The Cast receiver can open its unique URL while the TV is playing. The server withholds the MP3 body until the console mutes the TV, waits for the local device to drain and reaches the selected frame. On Xbox the driver queues eight 256-sample blocks at 48 kHz. The console also checks the kernel’s consumed-block counter across the queue, the mixer and the last submitted block. The server then confirms playback of that specific Cast session. A VOD handoff starts with a clip cut at the requested position. A live handoff uses a complete MP3 frame from the current encoder.

The optional `sid` on play, position, pause, resume and stop is the stream ID from the response header. A late stop for an older playback has no effect on a newer speaker session. A fenced or expired play receives 409. Stop revokes the speaker's media URL immediately. The server retries an undelivered Cast stop when the media channel returns and also stops orphaned playback when console polls disappear. Audio already buffered on the receiver can finish during a network partition.

VOD uses Cast `BUFFERED` with the known remaining duration. A seek to converted audio in the currently loaded resource preserves its URL and Cast media session. If the destination precedes that resource, lies beyond the converted prefix, or the receiver does not confirm SEEK, the server can LOAD an appropriate MP3. Live uses Cast `LIVE` and a session-specific provisional file. After a pause it starts again at the live edge.

**MP3 downloads.** Conversion writes `<id>_<t>.part.mp3`. A successful conversion with a nonempty output promotes it atomically to `<id>_<t>.mp3`. Failed and abandoned outputs are regenerated. A complete target supports `HEAD` with length and byte-range GETs with a known total. For a growing file, an initial `bytes=0-` receives a continuous 200 response. Other single ranges receive 206 with the available extent and `*` for an unknown total. Unsupported range forms receive 200. Positions beyond the available data can receive 416.

Cast URLs carry an opaque `s`. A revoked URL receives 410. Small fixed ranges and completed unscoped downloads include Content-Length. A larger session-bound GET streams until EOF or revocation. A 206 still reports Content-Range. Delivery allows four seconds for startup or a range entrance and about 1.5 s ahead of the confirmed clock in steady playback, using the 192 kbit/s MP3 rate.

## Sleep timer

The server owns one volatile timer. It continues counting without console polls; restarting the server clears it.
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
D-pad **↑** opens the timer chooser, **← →** selects, **A** applies and **B** closes.

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
