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
preference uses `lang:pt` or `lang:en` and falls back to original if unavailable. Canonical track codes include
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
| `GET /api/nest/pos?sid=<id>` | `<state>\t<absolute position s>\t<error>\t<revision of gated MP3 reader or 0>`; states include `LOADING`, `PLAYING`, `PAUSED`, `BUFFERING`, `STALE`, `IDLE` and `ERRO` |
| `GET /api/nest/local?sid=<id>&rev=<revision>` | `WAIT\t<revision>` or `QUIET\t<revision>`. Reserves the TV output and revokes the previous Cast URL. Use the returned quiet revision as `claim` on the next play. |
| `GET /api/nest/unmute?sid=<id>&rev=<play revision>` | Open the waiting MP3 body after the TV mutes and its device buffer drains. Returns `ok\t<absolute position s>` on a fresh PLAYING reply for that media session, 409 on obsolete ownership or 503 on an unconfirmed start. |
| `GET /api/nest/pause` · `resume` · `stop` | |
| `GET /api/nest/volume?v=0..100` | |
| `GET /api/audio/<id>?t=<s>&track=<selection>` | the selected MP3 the speaker downloads (supports `Range`) |

A receiver status supplies its own update timestamp. The server extrapolates a playing position for at most 1.2 s, then returns `STALE` with the last observation. The Xbox discards normal position samples with a round trip above 120 ms. A server advertising `X-Nest-Handoff: 1` arbitrates the two outputs. A stale speaker clock leaves local PCM muted. If a LOAD could have emitted sound, the server waits for a fresh quiet status after revoking its URL and stopping the receiver. The server records the possible owner on disk before a LOAD so that restarting the process still requires confirmation. A control-channel partition can therefore leave a silent interval after the receiver runs out of buffered MP3.

The player trims or holds the muted PCM queue against the PTS of the displayed frame. It enables the TV when the server has granted it ownership and the local audio clock is within 22 ms of that frame. For a return to the speaker, the console requests a future frame with `play&claim=<quiet revision>`. The Cast receiver can open its unique URL while the TV is playing. The server withholds the MP3 body until the console mutes the TV, waits for the local device to drain and reaches the selected frame. On Xbox the driver queues eight 256-sample blocks at 48 kHz. The console also checks the kernel’s consumed-block counter across the queue, the mixer and the last submitted block. The server then confirms playback of that specific Cast session. A VOD handoff starts with a clip cut at the requested position. A live handoff uses a complete MP3 frame from the current encoder.

The optional `sid` on play, position, pause, resume and stop is the stream ID from the response header. A late stop for an older playback has no effect on a newer speaker session. A fenced or expired play receives 409. Stop revokes the speaker's media URL immediately. The server retries an undelivered Cast stop when the media channel returns and also stops orphaned playback when console polls disappear. Audio already buffered on the receiver can finish during a network partition.

VOD uses Cast `BUFFERED` with the known remaining duration. A seek to converted audio in the currently loaded resource preserves its URL and Cast media session. If the destination precedes that resource, lies beyond the converted prefix, or the receiver does not confirm SEEK, the server can LOAD an appropriate MP3. Live uses Cast `LIVE` and a session-specific provisional file. After a pause it starts again at the live edge.

**MP3 downloads.** Conversion writes `<id>_<t>.part.mp3` for original audio; other selections add a hash of the
selection to the ID. Jobs and cache keys are partitioned by selection. Changing language forces a Cast LOAD,
even for the same video and position; it cannot SEEK within the old language's resource. A successful conversion with a nonempty output promotes it atomically to `<id>_<t>.mp3`. Failed and abandoned outputs are regenerated. A complete target supports `HEAD` with length and byte-range GETs with a known total. For a growing file, an initial `bytes=0-` receives a continuous 200 response. Other single ranges receive 206 with the available extent and `*` for an unknown total. Unsupported range forms receive 200. Positions beyond the available data can receive 416.

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

## Player focus and captions

This section supersedes the earlier player D-pad shortcuts. A hidden control bar consumes the first direction
solely to reveal the timeline. On the timeline, Left/Right seek 10 seconds and A pauses/resumes. Up focuses the
options row (Captions, Quality, Sound, Sleep timer, Rate, Go to channel); Left/Right choose an option, A opens it,
and Down returns to the timeline. B closes one level: panel → options → timeline → hidden bar → leave video.
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

### GET /api/captions/{video_id}?lang=pt|en&track=<optional track code>

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

`GET /api/nav?kind=<kind>&id=<browse ID>&q=<query>&tab=videos|live|playlists&filter=0..3&offset=0&n=24&fresh=0&lang=pt|en`

Kinds: `channel`, `subscriptions`, `library`, `playlist`, `history`, `later`, `search`, `music`, `music_search`,
`album`, `artist`, `radio`, `music_playlist`. Channel/artist IDs are `UC…`; album IDs are Music browse IDs;
playlist IDs accept the `VL` prefix. Music uses a public YTMUSIC session; account lists and channel actions use
TV credentials. Subscriptions, library, history and Watch later require sign-in in the account service.

```
ok\t<title>\t<subtitle>\t<art key>\t<subscribed 0|1>\t<more 0|1>\t<channel ID>
<type>\t<id>\t<title>\t<byline>\t<duration s>\t<art key>\t<channel ID>\t<section>
```

Item types are `video`, `channel`, `playlist`, `album`, `artist`. Empty fields remain present. Lists keep YouTube's
order and deduplicate within each type. `offset` counts all returned items (including subscription avatars).
`more` is explicit; short or empty continuation pages do not imply the end. Errors are `erro\t<localized message>`.
The client keeps parent pages, focus, tab and scroll when opening a child; B pops that page without a network reload.
Library adds local shortcuts for History and Watch later before the account's playlists/mixes.

For channel pages, `tab` selects the corresponding TV shelf and that shelf's continuation, excluding Shorts.
Search `filter`: 0 all, 1 videos, 2 channels. Music search: 0 all, 1 songs, 2 albums, 3 artists.
For `music`, `q=charts` follows Explore’s Charts endpoint, `q=mixes` selects playlists from the Music home,
and empty `q` puts playable tracks before collections. Shelf titles retain YouTube's language; subscriber/video/view
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
visible avatars and the current cover request artwork.

With `server=off`, account routes use the console TV session in `yt-console-account.json`. Music receives the
current access token on login, refresh and logout; the token never appears in the text protocol. Changing it clears
catalog caches and discards navigation responses from the previous token. Public channel/playlist requests use the
native catalog first; a failed account playlist request can use the TV session, including private library playlists.

Now playing decodes only AAC, including when the console controls a Nest directly. The muted PCM reserve follows
the speaker clock and becomes audible only after Cast confirms silence. The PC audio reserve accepts AAC or MP2
without decoding video. B stops and clears the local queue, returning to the source page. Show video reopens at the
current position, preserving pause; B through the player's focus levels returns to audio at that position. Lyrics
can be scrolled with up/down. Account history also records audio-only sessions.

Local JSON account additions: `GET /nav` with `kind,id,q,tab,filter,page,fresh`, returning
`{title,subtitle,image,channel_id,subscribed,items:[{type,id,title,channel,duration,image,channel_id,section}],more}`;
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
192.168.101.41 8009
Teste Eliot
```

This makes the existing Sound setting available with `server=off`. The native player retains the
selected AAC/m4a audio offer even when the displayed MP4 already contains audio. It passes that
unchanged signed URL to the Default Media Receiver (`CC1AD845`) using Cast v2 TLS and protobuf.
The channel implements CONNECT, PING/PONG, receiver/media GET_STATUS, LAUNCH, LOAD, SEEK,
PAUSE, PLAY and STOP. A seek within the same URL retains the Cast media session. A new resource
waits for the previous playback to stop. Discovery currently uses the configured host.

Each LOAD carries an ownership token in `media.customData.tube360`. Clock samples require that token, the exact loaded content URL
or an already verified media session. Command request IDs fence older clock replies across seeks.
The observation time advances only when an accepted MEDIA_STATUS contains `currentTime`. Fast
request/reply pairs use their midpoint (120 ms maximum round trip). Unsolicited status uses receipt
time. The picture extrapolates PLAYING observations and keeps the local PCM reserve muted. Eight
seconds without a new clock or position progress requests release. BUFFERING also requires progress.
A missing media namespace during reconnection waits for a complete receiver status.

The TV takes over after a fresh status confirms IDLE/empty media or an empty application list.
A timeout by itself retains the mute. Returning to the speaker preloads with `autoplay=false`,
then waits for the selected frame and for the TV device queue to drain before issuing PLAY.

The client writes `nest-owner.json` before LOAD. Leaving the video keeps the Cast worker alive to
retry STOP during navigation. Application shutdown waits up to 1.8 s for quiet confirmation and
retains an unfinished journal for the next launch. With an unreachable speaker and a direct YouTube
URL, audio can continue after the application exits. That URL cannot be revoked by the console.
The implementation sends no volume commands.

Direct live HLS playback currently uses TV sound after speaker release. The native HLS reader's
zero-based video PTS and the receiver's event-relative `currentTime` need a verified common origin
before independent YouTube HLS playback can use the Nest clock.

The desktop-only `XT_YT_REPLAY=<directory>` replaces YouTube responses with sanitized local
fixtures. `bench/standalone/cast_e2e.py <repo>` prepares synthetic
720p30 AVC/AAC media, drives the desktop app with `server=off`, serves AAC over local HTTPS on
8233, exercises a TLS/protobuf Cast receiver and writes captures under `.work/provas/`.
Run `python3 bench/standalone/cast_test.py client/build/nest-test` for channel, ownership and recovery
tests after building the `nest-test` target. The Python tests use the installed pychromecast protobuf
binding and OpenSSL to create a temporary certificate.
