# Native console paths

Tube360 runs YouTube on the Xbox. The console talks directly to YouTube, the phone and the speaker, with no PC in
the path: account, Music, phone Cast and Nest, the sleep deadline and the end-of-video shutdown are all on the console.
An older `server=` line in `settings.txt` or a `server.txt` beside the XEX is ignored, and the next save drops the line.

## Native ownership

| Function | Console path |
|---|---|
| YouTube browsing/account | HTTPS InnerTube/catalog requests in `client/src/yt/`; TV OAuth device code, refresh and protected local session in `yt-console-account.json`. No password is entered on Xbox. |
| Video | Original AVC/H.264 and AAC, direct ranged VOD or segmented HLS, decoded locally by FFmpeg. |
| Audio selection | Original before automatic dubbing; offered language/role catalog used for player selection and direct speaker audio. |
| Source/output | `video_mode=auto|720p60|1080p30` selects source preferences; `output_height=720|1080` selects framebuffer size. 1080 needs dashboard HDMI 1080p progressive. These are distinct settings, not guaranteed throughput. |
| Account navigation/Music | Native TV account and public catalog; local queues, history, likes, subscriptions, library, Music and audio-only playback. Music can fall back to public shelves if its endpoint rejects the TV token. |
| Phone | Native DIAL/SSDP and YouTube lounge receiver; identity files `cast-device.txt` / `cast-screen.txt`. See Cast from the phone below. |
| Nest | Cast v2 TLS directly from Xbox, configured in two-line `nest.txt`; the speaker fetches YouTube audio. See Direct Cast below. |
| Sleep | Console-owned deadline and end-of-video shutdown, with warnings, fades and shutdown on the Xbox. |

Local runtime files (account grants, Cast identities, Nest ownership journal, history, settings and logs)
are private; ship only the inactive examples in `deploy/xbox/`. The example for `nest.txt` puts values on the
first lines because that reader does not skip leading comments.

## Cast from the phone

The native receiver advertises `YouTube 360` through SSDP UDP
1900 and DIAL TCP 8134 (`/ssdp/device-desc.xml`, `/apps/YouTube`). The Phone panel uses its native lounge TV code.
Identity files are `cast-device.txt` and `cast-screen.txt`; lounge tokens stay in memory. A phone request starts
direct playback, preserves account history and replaces Music/navigation queues.
Pending navigation replies cannot take that phone session back.

Sleep expiry has priority over phone and navigation commands. At natural end with an end-of-video timer, the
client reports the end to the timer and suppresses lounge queue advancement while waiting for shutdown. Local
navigation can take playback back from the phone; the lounge then clears its previous queue.

Phone volume/mute controls both the TV gain and the direct Nest receiver. The Nest keeps its existing volume until
a phone volume command or timer fade changes it. Its initial volume is reported to the phone. Timer gain multiplies
the selected volume, and cancellation/confirmed stop restores that selected level. Volume messages use the Cast v2
receiver namespace, independently of playback PAUSE/PLAY/SEEK.

Desktop fixtures use `XT_CAST_DISABLE=1` to suppress discovery and lounge traffic when testing other features.

## Sleep timer

In console mode, `requestSleep` handles set/add/cancel locally using `SDL_GetTicks` and the same `SleepState`
wire representation below. The UI offers 15/30/45/60/90/120 minutes or natural end of video. The target is
always the Xbox. The final 60 seconds
show a warning and fade volume; A adds 15 minutes and B cancels. Expiry stops playback and powers down the
console after a two-second message. The desktop build exits its own process instead. Timer state is volatile. End-of-video sleep suppresses repeat and automatic next.

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

**Go to channel** closes the video and opens the channel page (by channel ID, or resolved from the video when the
ID is unknown).

## Direct Cast from the console

A console using the native YouTube player can configure its own speaker through `nest.txt` beside
`settings.txt`. The first line contains the host and optional Cast TLS port (default 8009). The second
line contains the display name. For example, a LAN configuration can contain the following lines.

```text
192.0.2.20 8009
Google Cast speaker
```

Replace the documentation address with the speaker's actual LAN IP. Find it in Google Home device information or the router's DHCP list. Keep values on the first two lines, without leading comments. This makes the Sound setting available. The native player retains the
selected AAC/m4a audio offer even when the displayed MP4 already contains audio. It passes that
unchanged signed URL to the Default Media Receiver (`CC1AD845`) using Cast v2 TLS and protobuf.
The channel implements CONNECT, PING/PONG, receiver/media GET_STATUS, LAUNCH, LOAD, SEEK,
PAUSE, PLAY and STOP. A seek within the same URL retains the Cast media session. A new resource
waits for the previous playback to stop. Discovery currently uses the configured host.

Native LAUNCH replaces an idle/default receiver when needed. The console confirms STOP and closes its TLS
channel before another sender is allowed to take the speaker.

Initial LAUNCH/media confirmation is limited to 3.5 seconds. Missing confirmation releases the speaker
and continues the current video with TV audio and a short notice; an initial failure is retried with X.
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

The desktop-only `XT_YT_REPLAY=<directory>` replaces YouTube responses with sanitized local
fixtures. `bench/standalone/cast_e2e.py <repo>` prepares synthetic
720p30 AVC/AAC media, drives the desktop app, serves AAC over local HTTPS on
8233, exercises a TLS/protobuf Cast receiver and writes captures under `.work/provas/`.
Run `python3 bench/standalone/cast_test.py client/build/nest-test` for channel, ownership and recovery
tests after building the `nest-test` target. The Python tests use the installed pychromecast protobuf
binding and OpenSSL to create a temporary certificate.

## Native player extras

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
