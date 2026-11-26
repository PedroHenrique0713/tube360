# Changelog

## [0.3.3] — 2026-11-26

### Added

- Shorts are grouped into one row of smaller 9:16 cards in search, Home (public and signed in), Subscriptions, History, Watch later and Library, placed where the first Short appeared. `shorts=row|hide` in Settings (Show Shorts); hiding keeps pagination indexes intact. The channel Shorts tab is unchanged.
- Automatic error reports (#41): the app keeps a durable session marker; after a crash or failed start the launcher sends a sanitized report (IPv4/IPv6, host:port, video IDs, Cast/Wi-Fi names, cookies, tokens and user paths removed) within a 1.5 s bound and shows a receipt code. Opt out with `no-error-reports.txt` or Settings → Error reports. The receiver (`tools/error-reports/`) classifies reports, keeps network/YouTube refusals as counters only, and opens private triage only for crash/startup signatures seen on three installations. [Privacy](docs/PRIVACY.md).
- Org CI: PR builds and tagged release drafts are built inside the pinned public `ghcr.io/revive360/devkit` image, with SHA256SUMS and signed build provenance for the ZIP, 7z and XEX.

### Changed

- Console-only Settings: the PC server row and the notebook/both sleep options were removed; legacy `server=` and `sleep_what=notebook|together|xbox_notebook` values load as console-only. READMEs and the package no longer include PC server instructions or `server.example.txt`; v0.3.2 is the last release with the PC server.
- The sleep timer marks the session clean before powering the console off.

### Fixed

- Search results no longer leave an empty row between rows: a card whose text crossed the footer was dropped entirely (#21).
- Opening a video with the Nest busy (another Cast/Bluetooth app holding it) or silent no longer hangs: after 9 s the video plays on the TV with the existing notice and X retries the Nest; 25 s of tolerance during playback is unchanged (#12).
- Members-only videos opened through the TV account stop at the first HTTP 403 (MP4, HLS, fixed quality) with a specific message instead of retrying with a generic error; ordinary videos still renew their links (#11).

## [0.3.2] — 2026-11-25

### Added

- Watch later and resume without an account, stored beside the app with bounded atomic snapshots, progress bars and confirmed local clearing. Account lists remain separate.
- Local clearing settings show actual counts with units, an explicit empty state and separately labeled storage limits in all four languages.
- Text without a font glyph is omitted in the per-font layout cache before fitting/shaping; Arabic joining/bidi controls and literal question marks are preserved. Emoji-only titles render empty.
- Channel Shorts cards and tab, protocol-relative artwork URLs and JPEG fallback for unsupported ytimg artwork. [Behavior and validation](docs/LOCAL-LIBRARY.md).

- Public search suggestions with debounced/cancellable anonymous lookup, session recent searches in the same list, and Series-style LB filters for type, duration, upload-date period and relevance/view-count order. Typed channel/playlist results open their pages; filtered continuations retain their query/params/language. English, Portuguese, Spanish and Arabic include 4:3/large-text layouts. Current YouTube durations use 3-minute boundaries. Removed legacy upload-date ordering after it was measured being ignored; view-count order worked in real WEB results (see `docs/SEARCH.md`).
- Native H.264 1080p30 decoding uses the measured Xenon VMX kernels, six FRAME workers across HW 0,2,4,1,3,5 and a larger bounded frame queue. Only known 1080p sources up to 30.1 fps omit deblocking; `h264_loop_filter=full` restores the complete filter after restarting the app. Smaller/unknown/high-cadence sources retain full filtering. The isolated decoder median is 22.57 ms/frame. On the owner’s Xbox on 2026-11-25, an action video sustained 30.00 fps without frame loss; football ran at 29.97 fps with one synchronization correction per minute, matching 720p. This does not promise 1080p60. Native 1080p output requires HDMI with 1080p progressive selected in the Xbox dashboard.
- Saved Nest IPs connect directly; metadata discovery runs in the background. Cast clock filtering and bounded picture correction keep Wi-Fi status jitter from changing playback cadence.
- Signed app updates through a separate `default.xex` launcher with startup confirmation and rollback. Migrate by copying the complete package once; keep account/settings files.
- Board simulation through `XT_BOARD` / `board-sim.txt`, bounded fault/delay/mapped-memory controls and durable output/font checkpoints for isolated qualification.
- Fixed 480p/360p/240p/144p H.264 choices in Settings and the player's Series-style Y Quality menu. During playback, choices follow the video's direct AVC/AAC offers; selection preserves position/pause and warns under sustained pressure without switching source or falling back to a PC. Existing Automatic/720p60/1080p30 profiles retain their adaptive ladder. New messages are available in Portuguese, English, Spanish and Arabic.

### Validated on Xbox — 2026-11-25

- Native 1080p output is sharp with HDMI and 1080p progressive selected in the Xbox dashboard. With the dashboard output at 720p, the app downscales and fast movement can show aliasing. Source quality and display output remain separate choices.
- Account Music, search filters and suggestions, Watch later and resume without an account, Y Quality, the SponsorBlock skip button and Settings were validated with the controller on the owner’s Xbox.

### Fixed

- Video search stays anonymous with a connected TV account, with or without filters and throughout pagination. Catalogue failures retain route, HTTP and numeric API/transport diagnostics without credentials or personal data; HTTP 200 error bodies are rejected.
- Start (☰) prompts on video cards say Save / options in all four languages, with room reserved in the 4:3/large-text footer. Music history uses a clock without a detached arrow; the heart tip is rounded.
- Cropped video quality uses its width class for display (1920×800/24 appears as 1080p24 in the player and Y menu). Source/decode limits, fixed-height matching and recovery still use real dimensions. Account Music shortcuts have local clock/list/heart artwork.
- Xbox MP4 opening and seeks above 2 GiB retain exact 64-bit sizes/offsets by avoiding an unsafe xsdk GCC combine optimization in FFmpeg's demuxer. Large-index and 60 fps/PQ fixtures reproduce the GCC defect in PowerPC emulation; the fix was validated on the owner’s Xbox on 2026-11-25. Opening logs now separate header/seek time and transferred bytes. [Cause and console procedure](bench/opening/README.md).

- Public Xbox packaging rejects benchmark/mute executables even with a stale OFF cache; independent package checks verify production trust in both launcher and app.

- Adaptive recovery uses the measured 1080p30 decoder cost as a floor and preserves higher costs observed in the session. Explicit worker priorities keep UI, PCM and network above video work; SponsorBlock retains its temporary HW5 TLS boost and restores the background priority afterward.
- Video drawing bypasses both UI scale axes, so SD and 1080 output use the whole physical framebuffer while menus retain their scale and margins.
- Cached same-video seeks, including SponsorBlock undo, notify the phone even when the fast reopen never appears as a sampled loading state. Continuous playback keeps its existing notification rate.
- Corona 480i/component simulation selects the kernel SD viewport in both native and safe paths, even when the reported signal is HD. A bounded `board-sim.txt` contract supports board/driver/mode/cable overrides for isolated testing, preserving real hardware detection, viewport and signal timings.
- SD/interlaced dashboard modes use the kernel's existing framebuffer dimensions consistently across display surfaces, swapchain and presentation. 480i/576i/480p preserve the dashboard scaler/signal and use its 4:3/widescreen aspect, including non-square PAL pixels. Boot logs include mode, connector and actual viewport.
- Quality recovery projects the next format from measured decode milliseconds per frame, exact pixels and frame rate. Failed source/cadence steps stay remembered across seeks in the playback session, with increasing retry delays; transient network recovery remains possible.
- Corona/Winchester keep the native video path by default, preserving the tested 360 E baseline; other/unknown board families use the conservative framebuffer/staging path. `video-driver.txt` can explicitly select `safe`, `native` or `auto`. Board/path details appear in both durable boot logs and the normal app/UDP log.
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

- Update rules and signed app candidates try `revive360/tube360` first and `PedroHenrique0713/tube360` as a reserve. Both exact repository paths remain allowed through HTTPS redirects, with Ed25519 verification and SHA-256 unchanged. Older public apps need a complete manual package upgrade; see [repository transfer compatibility](docs/REPOSITORY-TRANSFER.md).

- Hold LT/RT to rewind/fast-forward with an accelerating destination preview; release seeks once and preserves pause. Speaker synchronization remains in Settings.

- SponsorBlock with Off, Skip button (default), and automatic skipping; Y skips or returns after an automatic skip, with community warning and attribution in English, Portuguese, Spanish and Arabic.
- Discreet timeline marks, private four-character SHA-256 prefix requests, background loading and bounded per-video session cache. Sponsors, self-promotion and subscription reminders are enabled; intros/outros are preserved.

- Music now tries the TV music catalogue with the existing account grant after a web Music refusal, exposes recent plays/playlists/likes, labels public fallback, and keeps continuation tokens with their original client. Account Music was validated on the owner’s Xbox on 2026-11-25; Music/TV recommendation parity is not guaranteed.

- Arabic interface, native YouTube locale and controller search keyboard (RB switches to Latin).
- Arabic titles in every UI language: contextual forms, lam-alef, bidi and per-character Noto Sans Arabic fallback loaded on demand.
- Full app packages now include four Noto fonts/OFL and FriBidi LGPL notices; copy the complete folder once.

### Known limits

- Fat board families still have no physical acceptance test; simulation does not establish hardware compatibility.
- Arabic still needs native-speaker review.

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

[0.3.0]: https://github.com/revive360/tube360/compare/89a7158...v0.3.0
