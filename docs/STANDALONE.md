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

Run `python3 bench/h264-decode/probe.py` with yt-dlp installed. The saved response projection contains playability, HTTP status, time, client and format/manifest fields. When streaming data is present the probe tests an untouched VOD URL or a 720p AVC HLS variant and one segment. It records returned bytes, declared TTL and/or remaining signed URL lifetime without retaining signed links. For live qualification repeat segment reads across playlist refreshes and beyond 30 seconds. For an expiry measurement retest the same URL near its expiry and request a fresh one after a 403. [Recorded responses](../bench/h264-decode/results/2026-11-23/innertube.json)

## Where each feature would run

| Current feature | Proposed owner | Implementation cost or dependency |
|---|---|---|
| Anonymous home and search | Console, with optional server fallback | HTTPS player/browse/search handling, JSON parsing with bounded memory, continuations, language and filtered unavailable videos. Cache results per query. |
| Recommendations, subscriptions, Watch later | Optional account service initially | These lists require an authenticated TV session. Port the TV browse renderer and continuation handling before moving them to the console. |
| Thumbnails | Console | Fetch CDN JPEGs on a worker, bound image dimensions and cache size, resize/crop off the draw thread. The current protocol returns ready-sized baseline JPEGs. |
| VOD, audio to TV, quality and seeking | Console | Resolve video and audio, select measured H.264 and AAC/Opus profiles, retain separate time bases, issue byte ranges for MP4 indexes and keyframes. Decode ahead within a bounded queue. |
| Live | Console when the source and decode budget qualify | Fetch the master, choose an AVC rendition, start from matching audio/video segments, honor media sequence and discontinuities. Resume at the live edge. Measure segment duration, startup and capture-to-screen lag independently. Keep server conversion for incompatible sources. |
| TV device-code sign-in and logout | Optional account service | A console port needs device-code polling, refresh, revocation and protected token storage. Preserve sign-out and anonymous local operation. |
| History and resume | Local history on console, optional account service for YouTube sync | Save position atomically, send play/watch-time tracking only for the active session. Avoid duplicate starts during retries. |
| Like/dislike | Optional account service | Requires authenticated mutations and TV client compatibility. Show the confirmed account response. |
| Up next and autoplay | Console countdown, optional server recommendation | Parse watch-next for public VOD and account-aware recommendations if available. Keep B to cancel. |
| Captions | Console proposal | Fetch the selected timed-text track, parse cues and render from the audio clock. Track URLs expire. Captions have no endpoint in this worktree's protocol. |
| Nest and Chromecast speaker | Optional server | Keep Cast discovery/control, media URL serving, transcoding when required, and speaker clock/failover. TV audio is the standalone output. |
| Send from the phone | Optional server | Keep DIAL discovery, lounge pairing, queue and polling. A native receiver needs a persistent connection, LAN listener, reconnection and protected pairing state. |
| Sleep timer | Console for Xbox shutdown, optional server for notebook shutdown | Use monotonic deadlines and end-of-video events locally. The server must own a timer that outlives the console or powers down the PC. |
| Remote commands, status page and lid inhibitor | Optional server | Expose server-only functions while connected. The console can hold its own settings, local status and playback queue. |

### Transport and TLS

`tools/build-mbedtls-x360.sh` builds the official Mbed TLS 3.6.8 release as static PowerPC libraries. Its release archive is pinned by SHA-256. The platform adapter seeds the library's entropy collector from xsdk's kernel RNG and supplies a monotonic millisecond clock. X.509 date checking stays enabled. TCP send/receive, nonblocking deadlines and cancellation belong to a network worker. Connect the callbacks with `mbedtls_ssl_set_bio`, set the requested hostname for SNI and verification, configure `MBEDTLS_SSL_VERIFY_REQUIRED`, and verify each redirect host with a new TLS context.

For integration, embed a versioned CA bundle extracted from the Mozilla root store in the release as read-only DER certificates. Record its upstream date, license and digest, and load it at startup into the trust chain. Retain the issuer roots needed by the API, thumbnail and media hosts, including rollover roots. Check the console's civil clock and reject an expired certificate, bad hostname or untrusted issuer. Updating the bundle requires a versioned data or title update. The libraries built here are separate from the local decoder benchmark.

Three cold connects from the PC using system OpenSSL with normal trust and hostname checks took 74–79 ms for TLS to www.youtube.com and 82–86 ms to i.ytimg.com, after DNS/TCP. Those timings include round trips. Xenon crypto time is unknown. Instrument DNS, TCP connect, the TLS handshake and first media byte separately using CLOCK_MONOTONIC. Collect 20 cold and 20 session-resumed requests per host and report median, p95, handshake failures, peak bytes and decoder underruns during each. Reuse connections by origin for segment downloads. Allow one retry after a transient error and offer the configured yt-dlp server on source failure. [Mbed TLS release](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.8)

### Player integration

The local PC target `h264-play` exercises `client/src/h264.hpp`. Audio is stereo interleaved float at 44.1 kHz, including the resampler tail. The video view has three 8-bit 4:2:0 planes, each with its own signed stride, visible dimensions, color matrix, range and presentation timestamp. The backing frame is valid until the next decode call on that track. The local-file prototype uses one input cursor per track. Live demux, seeking and speaker synchronization remain integration work.

1. In `player.hpp`, keep source selection outside the render loop. For MPEG-PS continue through pl_mpeg. For H.264 feed a custom AVIOContext from the network ring, demux once for multiplexed live or use separate inputs for adaptive VOD, and dispatch packets to the correct decoder. Bound queues by time and bytes. Seek to a previous keyframe, flush codecs, reset the resampler/clock and discard preroll to the target PTS.
2. On the decoder worker, copy only visible pixels row-by-row from the Y, Cb and Cr planes into the player's reusable frame storage. Allocate for `w*h + 2*ceil(w/2)*ceil(h/2)`. On a format change recreate the texture at a safe render boundary. Set the shader's limited/full range and BT.601/BT.709 coefficients from the frame. The shader at this branch point assumes limited BT.601.
3. Preserve the existing SDL output and volume/timer handling. For TV, anchor to audio PTS and subtract queued samples and measured device latency. For Nest, keep the sampled speaker position and token checks. VOD offsets and live discontinuities need a common timestamp origin. Bound the number of decoded reference surfaces before selecting six frame threads on a 512 MiB console.
4. In the player region of `main.cpp`, retain `gfx::videoStaging` and `videoCommit`, the controls and overlay. Carry color metadata with the queued frame and select the proper shader parameters. Route quality changes and seeks through the new source. Add localized source/quality errors during integration.
5. Choose 480p30 or 720p30 only after both the local decoder benchmark and playback with audio, upload and network fit the frame budget. Check p95 against 33.37 ms for 29.97 fps and 16.68 ms for 59.94 fps. Measure actual queue underruns, frame drops and live latency on the console. Frame-thread throughput leaves a startup buffer of several pictures.

The [benchmark instructions](../bench/h264-decode/README.md) describe the exact build and the two console variants.
