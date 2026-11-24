# Changelog

## [0.3.0] — Unreleased

Prepared against the last public release baseline, `89a7158` (`git log 89a7158..HEAD`).
This entry describes the integrated console branch; publication and hardware acceptance remain with the release coordinator.

### Added

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
