# Console ⇄ server protocol

The console app (`client/`) talks to the PC server (`server/`, port **8091**) over plain **HTTP/1.0 on TCP**, with no TLS
and no chunked encoding. It reads the server address from `server.txt` next to the `.xex` (`<IP> <port>`).

## Text rules

List responses are `text/plain; charset=utf-8`: one item per line, fields separated by TAB, lines ending in `\n`. No field
ever contains a TAB or a line break (the server replaces them with spaces). The first line is a header. Errors are
`erro\t<short message>`; the messages are in Brazilian Portuguese because they are shown on screen as is.

## Feeds

`GET /api/feed?kind=home|search|history&q=<text>&n=24&offset=0&fresh=0|1`

```
ok\t<count>\t<feed title>
<id>\t<duration s, 0 if live/unknown>\t<channel>\t<formatted views>\t<title>\t<resume position s>
...
```

- `kind=home` is built from `home_queries` in `config.json`. `offset` pages through it without end: when the console
  scrolls near the bottom it asks for the next page, and the server searches more topics if needed.
- `kind=search` needs `q` (percent-encoded UTF-8).
- `kind=history` lists what this console watched and where it stopped.
- `fresh=1` skips the cache. The console sends it when you press **X** (Refresh).
- The 6th field is where you stopped last time, so a video can resume.

## Thumbnails

`GET /api/thumb/<id>?w=320&h=180` returns a **baseline** (not progressive) JPEG, exactly `w`×`h`, cropped to fill. On
error: HTTP 404. The server prefetches thumbnails for every list it serves.

## Video

`GET /api/stream/<id>?t=<seconds>&q=720|480|360&a=0|1` returns `video/mpeg`: MPEG-PS with **one** `mpeg1video` stream and,
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
- Extra headers: `X-Duration: <s>` and `X-Title: <percent-encoded title>`.
- One stream at a time: a new request ends the previous one. When the console closes the connection, ffmpeg is stopped.
- `a=0` is used in speaker mode, when the sound goes to a Google Cast speaker instead.

## Remote control

The console polls about every 500 ms and reports its state:

`GET /api/remote/poll?playing=<id or empty>&pos=<s>&dur=<s>&paused=0|1&ended=<id or empty>`

The answer is one line:

| Line | Meaning |
|---|---|
| `none` | nothing to do |
| `play\t<id>\t<t>\t<title>` | play now (also sent after `ended` when the queue has a next video) |
| `pause` · `resume` · `stop` | |
| `seek\t<delta s>` | relative seek, may be negative |
| `aviso\t<text>` | show a short on-screen message (e.g. "phone connected") |

The web remote (`GET /`) and the Cast receiver use a JSON API:

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
| `GET /api/nest/play?id=<id>&t=<s>` | the speaker starts playing the audio of `id` from `t` |
| `GET /api/nest/pos` | `<state>\t<absolute position s>\t<error>`; state is `PLAYING`, `PAUSED`, `BUFFERING`, `IDLE`… |
| `GET /api/nest/pause` · `resume` · `stop` | |
| `GET /api/nest/volume?v=0..100` | |
| `GET /api/audio/<id>?t=<s>` | the MP3 the speaker downloads (supports `Range`) |

The console polls `/api/nest/pos` every 200 ms and follows that clock (see "Sound on a Google speaker" in the README). The
server also stops the speaker on its own if the console stops polling or leaves the video, so the sound never keeps
playing alone.
