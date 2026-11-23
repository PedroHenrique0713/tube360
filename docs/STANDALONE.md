# Standalone playback on Xbox 360

Native H.264 playback and direct access to YouTube have separate acceptance tests. The local benchmark can settle CPU cost even when YouTube declines public playback. Keep the existing server as a selectable source while both paths are under evaluation. The normal client still speaks the [current protocol](PROTOCOL.md).

## Public playback experiment, 23 November 2026

From the development PC in Brazil we sent one anonymous HTTPS player request per client and video. The VOD was the ge tv football highlights `4p_9OPKgcDE`. The live was France 24 English `HvZt-nh9sGg`. Requests supplied the client name, version, user agent and device context from the installed yt-dlp client definitions. They carried no cookies or PO token and executed no player JavaScript.

| Client | Version | VOD H.264 720p | Live HLS | Observed expiry |
|---|---|---|---|---|
| VISIONOS | 1.02 | HTTP 200, LOGIN_REQUIRED | HTTP 200, LOGIN_REQUIRED | No link returned |
| ANDROID_VR | 1.65.10 | HTTP 200, LOGIN_REQUIRED | HTTP 200, LOGIN_REQUIRED | No link returned |
| IOS | 21.26.4 | HTTP 200, LOGIN_REQUIRED | HTTP 200, LOGIN_REQUIRED | No link returned |

The six responses contained a sign-in challenge. This establishes availability for this IP, date and pair of videos. Client identities, rate limits, account requirements, codec availability and CDN authorization can change independently. A direct-mode release needs a successful byte-range fetch for VOD and sustained segment delivery for live. A player response declaring a lifetime does not demonstrate that the CDN serves the asset for that long.

The raw measurements from that run are included here. Its probe and decoder benchmarks live on branch `f5-h264`. [F5 responses](../bench/h264-decode/results/2026-11-23/innertube.json)

### Follow-up from the same home connection

The VOD, live ID, country and ANDROID_VR version stayed constant. Requests used an isolated home, with no account or browser cookie file. F7 spaced requests in each comparison by four seconds. The comparisons cover a single VOD and a single live event.

| Request path, 23 November (UTC) | VOD | Live | Lifetime |
|---|---|---|---|
| Minimal ANDROID_VR 1.65.10, 19:05 | HTTP 200, `LOGIN_REQUIRED` | HTTP 200, `LOGIN_REQUIRED` | No URL |
| Stock yt-dlp 2026.10.26, ANDROID_VR, 19:04 and 19:06 | Player `OK`. Signed AVC URLs for itags 136 (720p30) and 135 (480p30). Each returned HTTP 206, 2,048 bytes and an MP4 header. | Player `OK`; HLS master and 720p variant both HTTP 200. The selected segment returned HTTP 403 on the byte-range read. The test stopped there. | Player advertised 21,540 s (5 h 59 min) on each response. Measured byte delivery only at issue time. |
| Same yt-dlp VOD control, 19:13, plugins explicitly disabled | Player `OK`, no authorization header or player PO token. The extractor warned about a PO token for media and only listed itag 18 as eligible AVC. | — | 21,540 s advertised |
| Native C++, public web session | 28 search cards. Watch page carried player status `OK` and metadata; neither a ready AVC nor a ready AAC URL met selection. A separate POST to the web player during development returned `UNPLAYABLE`. | — | 21,540 s advertised by watch page |

The initial six failures establish the server's stated reason (sign-in/anti-bot check). The two paths gave different decisions within minutes on the same IP and the same ANDROID_VR version. An IP-wide denial and an obsolete version alone cannot explain these results. The controls do not isolate a specific visitor field, header, session history or IP reputation signal. A PO token for the player was unnecessary in the clean successful VOD control. Media delivery is a separate decision. The positive MP4 probes checked video bytes only. Neither an uninterrupted video with sound nor expiry at the advertised time was tested. [Minimal repeat](../bench/standalone/fixtures/direct.json) · [extractor comparison](../bench/standalone/fixtures/comparison.json) · [delivery samples](../bench/standalone/fixtures/delivery.json) · [clean control](../bench/standalone/fixtures/clean-extractor.json)

NewTube's Lua implementation builds a regional client context, includes a visitor header in its requests and checks media availability separately after player resolution. Source inspection establishes how that project works. Its success on this network was not tested. Tube360 includes no NewTube source. [Reviewed revision](https://github.com/sebbz360/NewTube/blob/05540a834f45f3697277c10ce29e0abe83f4ca9b/youtube.lua)

### Native check and first integration

`client/src/yt` offers public home, search, next, player metadata, direct AVC/AAC selection, HLS manifest discovery, caption tracks/JSON3 cues, and a separate OAuth device flow. Search and browse use the context issued to the current guest by the public web page. The player reads the initial player response from the public watch page. It accepts already supplied HTTPS URLs up to 1280×720 and 30 fps, with a caller-selected lower ceiling. The result carries the upstream status and the source availability separately. For adaptive VOD the caller needs both audio and video. The library reports the HLS master to the caller; variant selection and segment playback belong to the player integration. A row of catalogue metadata is a successful search even when its video is unavailable.

Use one transport/client per network worker. The same libcurl handle keeps connections per origin. Each transfer has an 8 s connect timeout, a 15 s total timeout and a 3 MiB response limit. Set the transport's atomic cancellation flag when leaving the source. Hand off results to the UI after parsing. The diagnostic runs on a pthread; its main thread waits because it has no renderer. Credentials stay in memory. The device flow requires the application's own TV OAuth client ID and secret, displays Google's returned URL and user code, honors the polling interval and slow-down responses, and refreshes the grant. The `youtube.readonly` scope authorizes supported YouTube API calls. It does not establish access to media files. The existing account service has authenticated TV feed/history code; this experiment used no production grant and did not demonstrate a TV token opening a media stream. [Google device flow](https://developers.google.com/identity/protocols/oauth2/limited-input-device)

Build the stand-alone check from the repository root:

```sh
tools/build-standalone.sh pc
.work/standalone-pc/yt-test bench/standalone/fixtures
.work/standalone-pc/yt-check --live
flock ~/.cache/tube360-frota-xbox.lock systemd-run --user --scope -q \
  -p MemoryMax=6G -p MemorySwapMax=1G tools/build-standalone.sh xbox
```

The PC build uses Python 3 and the openssl CLI for a loopback test that rejects a private CA. The PC live command is optional (at most three requests). A result of 0 requires usable VOD URLs, 1 means search/setup failed, and 2 means metadata lookup produced no usable pair. Offline tests include the redacted VOD response from the measured 720p stream. Signed addresses in that fixture are replaced with `media.example`. Search layouts, captions and OAuth edge cases in the tests are synthetic. For the console, launch `.work/standalone-xbox/yt-check.xex` in a separate folder in Aurora with the console clock set. Read `yt-check.log` next to the executable. Record the three HTTP results, timings, search count, player status, height, fps, audio itag, muxed flag and TTL. The check does not run a decoder. [PC observation](../bench/standalone/fixtures/web-probe.txt) · [Watch response projection](../bench/standalone/fixtures/watch.json)

**Release order and effort** (engineering estimates for one developer after dependencies are available):

1. Qualify the direct source on the console (0.5–1 day). Confirm TLS, DNS and the three-call check. Require delivered video and matching audio bytes before enabling VOD. A working source is the open dependency and its discovery time is unknown.
2. Connect it to the F6 player on a worker (2–4 days). Exercise separate AVC/AAC ranges, startup, seek and cancellation at 480p30 and 720p30. Measure p95 frame time, underruns and peak memory. Keep the existing server selectable.
3. Connect public browse/search/next, thumbnail loading and VOD captions (2–3 days). Keep the render thread polling completed work. Add native continuations, localized source errors and a bounded thumbnail cache.
4. Qualify live independently (2–4 days once segments are available). Require changing media sequences, decodable segments across refreshed playlists and more than 30 s of synchronized output. A 403 leaves live on the existing server.
5. Add account storage, revocation and account-specific browse/history/like only after a test grant and authorized media delivery are independently verified (3–5 days). Add the console-only timer separately (0.5–1 day).

First standalone scope is public browsing and qualified VOD with TV audio. Account sync, likes, Music, Nest, phone Cast, live captions, background timers and PC shutdown remain server features at this stage. Music needs its own catalogue, radio/queue and player integration (3–5 days). Native Cast and Nest require separate discovery, receiver/controller and synchronization work; budget a separate prototype for each before estimating a release.

## Where each feature would run

| Current feature | Proposed owner | Implementation cost or dependency |
|---|---|---|
| Anonymous home and search | Console, with optional server fallback | HTTPS player/browse/search handling, JSON parsing with bounded memory, continuations, language and filtered unavailable videos. Cache results per query. |
| Recommendations, subscriptions, Watch later | Optional account service initially | These lists require an authenticated TV session. Port the TV browse renderer and continuation handling before moving them to the console. |
| Thumbnails | Console | Fetch CDN JPEGs on a worker, bound image dimensions and cache size, resize/crop off the draw thread. The current protocol returns ready-sized baseline JPEGs. |
| VOD, audio to TV, quality and seeking | Console | Resolve video and audio, select measured H.264 and AAC/Opus profiles, retain separate time bases, issue byte ranges for MP4 indexes and keyframes. Decode ahead within a bounded queue. |
| Live | Console when the source and decode budget qualify | Fetch the master, choose an AVC rendition, start from matching audio/video segments, honor media sequence and discontinuities. Resume at the live edge. Measure segment duration, startup and capture-to-screen lag independently. Keep server conversion for incompatible sources. |
| TV device-code sign-in and logout | Optional account service | The F7 library provides device-code polling and refresh. Connection to the account UI, revocation and protected token storage remain. Preserve sign-out and anonymous local operation. |
| History and resume | Local history on console, optional account service for YouTube sync | Save position atomically, send play/watch-time tracking only for the active session. Avoid duplicate starts during retries. |
| Like/dislike | Optional account service | Requires authenticated mutations and TV client compatibility. Show the confirmed account response. |
| Up next and autoplay | Console countdown, optional server recommendation | Parse watch-next for public VOD and account-aware recommendations if available. Keep B to cancel. |
| Captions | Console proposal | The library fetches a selected timed-text track and parses JSON3 cues. Integrate rendering against the audio clock. Track URLs expire. The integrated server already has a captions endpoint, including a separate live-clock limitation. |
| Nest and Chromecast speaker | Optional server | Keep Cast discovery/control, media URL serving, transcoding when required, and speaker clock/failover. TV audio is the standalone output. |
| Send from the phone | Optional server | Keep DIAL discovery, lounge pairing, queue and polling. A native receiver needs a persistent connection, LAN listener, reconnection and protected pairing state. |
| Sleep timer | Console for Xbox shutdown, optional server for notebook shutdown | Use monotonic deadlines and end-of-video events locally. The server must own a timer that outlives the console or powers down the PC. |
| Remote commands, status page and lid inhibitor | Optional server | Expose server-only functions while connected. The console can hold its own settings, local status and playback queue. |

### Transport and TLS

`tools/build-mbedtls-x360.sh` builds the official Mbed TLS 3.6.8 release as static PowerPC libraries. Its release archive is pinned by SHA-256. The platform adapter seeds the library's entropy collector from xsdk's kernel RNG and supplies a monotonic millisecond clock. X.509 date checking stays enabled. The F7 network worker uses the xsdk libcurl port for sockets, TLS callbacks and request deadlines.

The F7 target embeds four GTS roots as PEM in the executable. libcurl verifies the host and chain through Mbed TLS, refuses redirects by default and uses the console clock for validity. Update the bundle with the executable and requalify changed issuers on the console. The source and digest are recorded in [third-party notices](../THIRD_PARTY.md).

In F5, three cold connects from the PC using system OpenSSL with normal trust and hostname checks took 74–79 ms for TLS to www.youtube.com and 82–86 ms to i.ytimg.com, after DNS/TCP. Those timings include round trips. Xenon crypto time is unknown. Instrument DNS, TCP connect, the TLS handshake and first media byte separately using CLOCK_MONOTONIC. Collect ordinary usage samples during integration and report median, p95, handshake failures, peak bytes and decoder underruns. Reuse connections by origin for segment downloads. Allow one retry after a transient error and offer the configured yt-dlp server on source failure. [Mbed TLS release](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.8)

### Player integration

On `f5-h264`, the PC target `h264-play` exercises `client/src/h264.hpp`. Audio is stereo interleaved float at 44.1 kHz, including the resampler tail. The video view has three 8-bit 4:2:0 planes, each with its own signed stride, visible dimensions, color matrix, range and presentation timestamp. The backing frame is valid until the next decode call on that track. The local-file prototype uses one input cursor per track. Live demux, seeking and speaker synchronization remain integration work.

1. In `player.hpp`, keep source selection outside the render loop. For MPEG-PS continue through pl_mpeg. For H.264 feed a custom AVIOContext from the network ring, demux once for multiplexed live or use separate inputs for adaptive VOD, and dispatch packets to the correct decoder. Bound queues by time and bytes. Seek to a previous keyframe, flush codecs, reset the resampler/clock and discard preroll to the target PTS.
2. On the decoder worker, copy only visible pixels row-by-row from the Y, Cb and Cr planes into the player's reusable frame storage. Allocate for `w*h + 2*ceil(w/2)*ceil(h/2)`. On a format change recreate the texture at a safe render boundary. Set the shader's limited/full range and BT.601/BT.709 coefficients from the frame. The shader at this branch point assumes limited BT.601.
3. Preserve the existing SDL output and volume/timer handling. For TV, anchor to audio PTS and subtract queued samples and measured device latency. For Nest, keep the sampled speaker position and token checks. VOD offsets and live discontinuities need a common timestamp origin. Bound the number of decoded reference surfaces before selecting six frame threads on a 512 MiB console.
4. In the player region of `main.cpp`, retain `gfx::videoStaging` and `videoCommit`, the controls and overlay. Carry color metadata with the queued frame and select the proper shader parameters. Route quality changes and seeks through the new source. Add localized source/quality errors during integration.
5. Choose 480p30 or 720p30 only after both the local decoder benchmark and playback with audio, upload and network fit the frame budget. Check p95 against 33.37 ms for 29.97 fps and 16.68 ms for 59.94 fps. Measure actual queue underruns, frame drops and live latency on the console. Frame-thread throughput leaves a startup buffer of several pictures.

The separate decoder benchmark instructions are available with `git show f5-h264:bench/h264-decode/README.md`. The F7 title above checks the network and catalogue paths.
