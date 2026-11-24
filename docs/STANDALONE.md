# Native playback on Xbox 360

The v0.3.0 client runs YouTube on the console. A PC is an optional reserve. Without `server.txt` beside
the XEX, or with `server=off`, account, catalogue, Music, phone receiver, timer and speaker control do not
need Python/Node services. This supersedes the pre-integration plan formerly kept in this document.
Its dated measurements remain in Git (`git show 8236373:docs/STANDALONE.md`) and the benchmark fixtures;
they describe those earlier experiments, not the current feature boundary.

## Paths and ownership

| Feature | Current implementation |
|---|---|
| Public home/search/next | `client/src/yt/client.cpp`, native HTTPS, parsed responses and bounded workers/caches. |
| Account | `console_account.cpp`: TV-code OAuth, refresh, logout, account artwork, recommendations, history/progress, ratings, subscriptions, Watch later and library. Stored beside the app in `yt-console-account.json`. |
| Channels/playlists/Music | `catalog.hpp`, `navigation.hpp`, `music_navigation.hpp`: native catalogue, account continuations and local queues. Music token rejection can use public shelves while the account stays signed in. |
| Video and TV sound | `player_h264.hpp`, `h264.hpp`: FFmpeg AVC/AAC, direct VOD ranges or HLS. Selection, decode and buffering stay outside rendering. |
| Audio-only Music | `music_player.hpp`: AAC-only decode, local radio/playlist queue, optional lyrics and return to video at the current position. |
| Phone Cast | `client/src/cast/`: DIAL TCP 8134, SSDP UDP 1900 and YouTube lounge. Pairing, commands, queue state, volume/mute and persistent device/screen identity. |
| Nest | `nest.hpp`, `player_nest.hpp`: direct Cast v2 TLS on port 8009, Default Media Receiver, selected YouTube audio URL and speaker-clock picture. |
| Sleep | Local timer in `net.hpp` when server requests are disabled; warning, fade, end-of-video priority and kernel shutdown through `sleep.hpp`. |
| Captions | Native caption discovery/JSON3, language selection and cues against the displayed clock. Live captions remain unavailable. |
| Reserve | `server/` + `cast/`: original AVC/AAC remux when eligible, otherwise MPEG-1/MP2 conversion and the legacy HTTP control protocol. |

A media URL is distinct from the account grant. Successful browse, sign-in or player metadata does not prove
CDN byte delivery. YouTube can block clients or media independently. Saved grants, signed URLs, Cast identity
and Nest ownership files must not enter releases.

## Sources, presentation and output

`video_mode=auto|720p60|1080p30` sets source preference. Automatic can prefer 1080p detail; the fast profile
favors 720p60, and detail caps source cadence at 30 fps. These choices describe input, not guaranteed throughput.
Under pressure the player can reduce presentation to 30 fps and move to a lighter native video rendition while
retaining the audio clock, before trying a configured server reserve. The project target is 720p30 including
60 fps input; acceptance must measure actual displayed frames.

`output_height=720|1080` independently selects the framebuffer. Native 1080p requires HDMI and dashboard
1080p progressive; unsupported dashboard output retains 720p. UI coordinates scale from the 1280×720 design,
with fonts/icons rasterized at output scale. The GPU downscales larger YUV inputs with a filtered path.
Mapped linear surfaces avoid extra copies when supported; unsupported layouts keep direct playback through
the copy path. FFmpeg uses Xenon-safe VMX rather than assuming generic PowerPC instructions work.

Original/dubbed track selection carries through TV audio, native Nest, Music and reserve streams.
Original is preferred over automatic dubs. Interface-language preference falls back to original.

## Nest and phone

`nest.txt` has `<host> [port]` on line 1 (8009 by default), then a display name on line 2. No leading comments.
Discovery uses the configured host; the speaker fetches the signed audio URL itself. See [protocol](PROTOCOL.md#direct-cast-from-the-console).

Brief Wi-Fi interruptions preserve the speaker route/clock. TV sound waits for confirmed speaker silence;
a control-channel failure can leave a silent interval. Pending STOP survives video exit and is journaled for
recovery on restart. Exit waits up to five seconds but cannot revoke a YouTube URL or stop an unreachable
speaker. Leave playback with B before closing Tube360. Direct live uses TV sound: independent HLS playback
on the speaker needs a verified common clock.

The native phone receiver works in console-only and automatic modes. Phone play replaces Music/navigation
queues; sleep expiry wins over phone next. Pairing is separate from account login. The optional Node receiver
is unnecessary for native pairing and can advertise a second device.

## Transport and qualification

The build pins Mbed TLS 3.6.8 and curl-xenon. It validates the server chain/hostname against embedded GTS roots
and uses the console clock for certificate validity. One transport belongs to each worker. TLS locking supports
concurrent media, account and thumbnail transfers. See [notices](../THIRD_PARTY.md) and [build](BUILD.md).

Offline checks live in `bench/account`, `bench/catalog`, `bench/standalone`, `bench/quality` and `tests/cast`.
The [quality guide](../bench/quality/README.md) covers source/output, cadence, audio, shader comparisons and logs.
Use isolated HOME/assets/config, fake accounts and loopback TLS receivers; disable unrelated phone discovery
with `XT_CAST_DISABLE=1`. PC proofs do not establish Xenon speed, Xenos upload cost or real Wi-Fi behavior.

Before publication, qualify two minutes each of VOD 1080p30, VOD 720p60 and live 720p60 at each output size,
then account/library/Music, phone queue/volume, original/dubbed audio, Nest pause/seek/exit and sleep shutdown.
Record source, presented rate, queue minimum, drops, PCM, decode ms/s, upload/draw timings and network recovery.
Use `video-metrics.txt=0` for ordinary playback and compare with instrumentation on.
No console performance measurements were generated by documentation or packaging work.
