# Changelog

## [0.3.2]

### Added

- Public search suggestions with debounced/cancellable anonymous lookup, session recent searches in the same list, and Series-style LB filters for type, duration, upload date and order. Typed channel/playlist results open their pages; filtered continuations retain their query/params/language. English, Portuguese, Spanish and Arabic include 4:3/large-text layouts. Current YouTube durations use 3-minute boundaries; legacy date/view-count ordering needs provider/console acceptance (see `docs/SEARCH.md`).
- Native H.264 1080p30 decoding uses the measured Xenon VMX kernels, six FRAME workers across HW 0,2,4,1,3,5 and a larger bounded frame queue. Only known 1080p sources up to 30.1 fps omit deblocking; `h264_loop_filter=full` restores the complete filter after restarting the app. Smaller/unknown/high-cadence sources retain full filtering. The inherited isolated decoder median is 22.57 ms/frame; integrated Xbox playback with audio/UI/network still awaits console acceptance. This does not promise 1080p60. Native 1080p output requires HDMI with 1080p progressive selected in the Xbox dashboard.
- Saved Nest IPs connect directly; metadata discovery runs in the background. Cast clock filtering and bounded picture correction keep Wi-Fi status jitter from changing playback cadence.
- Signed app updates through a separate `default.xex` launcher with startup confirmation and rollback. Migrate by copying the complete package once; keep account/settings files.
- Board simulation through `XT_BOARD` / `board-sim.txt`, bounded fault/delay/mapped-memory controls and durable output/font checkpoints for isolated qualification.
- Fixed 480p/360p/240p/144p H.264 choices in Settings and the player's Series-style Y Quality menu. During playback, choices follow the video's direct AVC/AAC offers; selection preserves position/pause and warns under sustained pressure without switching source or falling back to a PC. Existing Automatic/720p60/1080p30 profiles retain their adaptive ladder. New messages are available in Portuguese, English, Spanish and Arabic.

### Fixed

- Adaptive recovery uses the measured 1080p30 decoder cost as a floor and preserves higher costs observed in the session. Explicit worker priorities keep UI, PCM and network above video work; SponsorBlock retains its temporary HW5 TLS boost and restores the background priority afterward.
- Video drawing bypasses both UI scale axes, so SD and 1080 output use the whole physical framebuffer while menus retain their scale and margins.
- Cached same-video seeks, including SponsorBlock undo, notify the phone even when the fast reopen never appears as a sampled loading state. Continuous playback keeps its existing notification rate.
- Corona 480i/component simulation selects the kernel SD viewport in both native and safe paths, even when the reported signal is HD. A bounded `board-sim.txt` contract supports board/driver/mode/cable overrides for isolated testing, preserving real hardware detection, viewport and signal timings.
- SD/interlaced dashboard modes use the kernel's existing framebuffer dimensions consistently across display surfaces, swapchain and presentation. 480i/576i/480p preserve the dashboard scaler/signal and use its 4:3/widescreen aspect, including non-square PAL pixels. Boot logs include mode, connector and actual viewport.
- Quality recovery projects the next format from measured decode milliseconds per frame, exact pixels and frame rate. Failed source/cadence steps stay remembered across seeks in the playback session, with increasing retry delays; transient network recovery remains possible.
- Corona/Winchester keep the native video path by default, preserving the coordinator's 360 E baseline; other/unknown board families use the conservative framebuffer/staging path. `video-driver.txt` can explicitly select `safe`, `native` or `auto`. Board/path details appear in both durable boot logs and the normal app/UDP log.
- VOD quality decisions ignore startup and post-switch settling, then require sustained measured overload before descending one rung. A viable 720p30 stream stays at 720p; when no separate 720p30 source exists, 720p60 first tries 30 fps presentation before a lower resolution. Stable measured headroom still restores quality.
- Safe-mode output changes keep the existing display/framebuffer. Font/output resource replacements check GPU idle, reset completed command references and publish a new atlas before retiring the old one; language and fit changes do not recreate the display.
- Native GPU/display setup records checkpoints before register access, including LUT/scaler/surface/line-counter steps. Initial display-show failures free buffers and return an error before interrupt registration.
- Native startup records durable CRT, storage, video/GPU, font, memory, network and TLS checkpoints beside each XEX. First-frame acquire failures are reported instead of accepting an invalid buffer. See `docs/STARTUP-DIAGNOSTICS.md` for interpreting a freeze.
- Startup no longer requires DbgPrint, and the measured kernel exception handler is opt-in. Unsupported XNet limits/socket options can use standard defaults; access-denied errors and TLS validation remain enforced. ABadAvatar compatibility is untested.
- Without `server.txt`, default Automatic mode and settings toggles keep the server disabled. Home shows the native failure and console clock for certificate dates; server recovery instructions and the configured endpoint appear only when the server is effectively enabled.
- Public browsing distinguishes certificate date errors (with the console clock), other TLS failures, DNS, network loss and YouTube refusal in English, Portuguese, Spanish and Arabic. TLS verification remains enabled.
- Startup failures show the reason and recovery steps with a built-in font, persist `startup.log`, and wait for B. The launcher checks four Inter fonts (Arabic fonts load on demand) before handoff and reports launch failures/timeouts.
- Direct VOD playback can recover higher source quality after sustained measured decoder/network headroom, with cooldown and backoff after unsuccessful upgrades. The original profile ceiling survives cached seeks.

### Changed

- Hold LT/RT to rewind/fast-forward with an accelerating destination preview; release seeks once and preserves pause. Speaker synchronization remains in Settings.

- SponsorBlock with Off, Skip button (default), and automatic skipping; Y skips or returns after an automatic skip, with community warning and attribution in English, Portuguese, Spanish and Arabic.
- Discreet timeline marks, private four-character SHA-256 prefix requests, background loading and bounded per-video session cache. Sponsors, self-promotion and subscription reminders are enabled; intros/outros are preserved.

- Music now tries the TV music catalogue with the existing account grant after a web Music refusal, exposes recent plays/playlists/likes, labels public fallback, and keeps continuation tokens with their original client. Real-account acceptance and Music/TV recommendation parity remain pending console validation.

- Arabic interface, native YouTube locale and controller search keyboard (RB switches to Latin).
- Arabic titles in every UI language: contextual forms, lam-alef, bidi and per-character Noto Sans Arabic fallback loaded on demand.
- Full app packages now include four Noto fonts/OFL and FriBidi LGPL notices; copy the complete folder once.

## [0.3.1] — 2026-11-24

### Fixed

- Videos open again when Google flags your IP for unusual traffic: the watch page redirects to `google.com/sorry`, so the visitor session and the player script now come from routes outside that check.
- Estimated dislike counts load: the public Return YouTube Dislike endpoint refused the app's generic agent with HTTP 403.
- The phone's cast list shows the app's own name (Tube360, or the name in `marca.txt`) instead of a fixed "YouTube 360".

### Known

- Phone casting works with the TV code from the Phone panel; the console is not yet listed by itself in the phone's cast list.

## [0.3.0] — 2026-11-24

Prepared against the last public release baseline, `89a7158` (`git log 89a7158..HEAD`).

### Added

- Comments with controller scrolling/pagination and labeled Return YouTube Dislike estimates.
- Eight playback speeds from 0.25× to 2× with pitch preservation, TV sound at changed speed, and repeat-video with native timer priority.
- 4:3 / 480 CRT layout with safe margins, fit/fill picture modes, large text in SD/HD, and Spanish alongside English/Portuguese.
- Signed Ed25519 YouTube rule updates with compatibility rollback: when YouTube changes something, the console picks up the fix without a new version. In-app replacement of the app itself is held back to v0.3.1 (its launcher froze on a real console in testing).

- Console-only YouTube over HTTPS, with native H.264/AAC playback and no PC requests when `server.txt` is absent.
- TV-code account sign-in stored on the console, recommendations, subscriptions, library, Watch later,
  history/resume, ratings, channel actions and account playlists.
- Native channel and Music browsing: Explore/charts, mixes, search, albums/artists, radio, audio-only queues,
  Now playing, optional lyrics and transitions between audio and video.
- Native phone receiver with SSDP/DIAL discovery, YouTube lounge TV-code pairing, playback queues,
  seek, volume and mute; account, Music and timer priorities survive phone commands.
- Direct Google Cast v2 control from the console, selected audio sent to the speaker, speaker-clock video,
  ownership handoff with an optional server and a saved pending STOP journal.
- Sleep timer runs on the console: 15 min to 2 h or end of video, 60 s warning with volume fade, then the Xbox powers off. No PC needed.
- Members-only badges in home/search/channel grids, explicit access notices and account-backed playback for eligible channel members. Paid access is not bypassed.
- VOD captions, track/language selection, chapters and player metadata.
- Automatic / 720p60 / 1080p30 source profiles, 720p/1080p output selection and original/dubbed audio choices.
  Original audio is preferred over automatic dubbing, including on Nest and in server reserve streams.

### Changed

- H.264 is the primary playback path. The PC is an optional reserve, with original AVC/AAC remuxing when
  possible and MPEG-1/MP2 conversion retained for compatibility.
- Direct playback retries fresh connections, resumes byte ranges and negotiates lighter native sources before
  server reserve; pressure can reduce presentation to 30 fps without restarting the audio clock.
- H.264 decoding uses Xenon-safe VMX paths and mapped video surfaces when supported, with a safe copy fallback.
- Native 1080p framebuffer support, filtered downscaling and private build copies of display dependencies.
- Sidebar access across screens, channel banners, round avatars, library artwork, Music shelves and the
  player icon row below the timeline, with direct like/dislike controls and B navigation by focus level.
- Server live starts at the live edge, keeps video/audio aligned and recovers interrupted streams.
- Nest seeks can reuse an active resource; handoffs fence old sessions, wait for confirmed silence and retain
  TV audio in reserve. Brief speaker network interruptions no longer immediately bounce audio to the TV.
- Exit waits up to five seconds for speaker silence; pending stops can recover on the next launch.
- Isolated recorded account/catalog/media fixtures, local TLS Cast receivers and quality/network probes cover
  the combined native and reserve paths without production accounts or devices.
- Bilingual console-first installation and troubleshooting, versioned allowlist packaging and release/privacy notes.

### Known limits


- HD source profiles do not guarantee sustained 60 fps or 1080p on every console/video/network. Hardware
  acceptance includes actual source, presented rate, audio continuity and both output sizes.
- Direct live Nest audio and live captions still need a common verified clock; live currently uses TV audio.
- YouTube may reject unofficial clients or TV grants; Music can fall back to public browsing.
- Direct audio may continue on an unreachable speaker after a forced app exit. Leave playback with B first.

[0.3.0]: https://github.com/PedroHenrique0713/tube360/compare/89a7158...v0.3.0
