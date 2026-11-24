# v0.3.0 screenshot notes

App images are JPEGs (1280×720, or 640×480 for the CRT captures), captured from the current desktop app with the production UI/player,
`server=off`, a dedicated HOME/assets directory and recorded transports. They illustrate console mode;
they do not measure Xbox frame rate or prove access to a real account, phone or speaker.

| File in `docs/img/` | Content |
|---|---|
| `home.jpg` | Public Palmeiras highlights and TV Palmeiras behind-the-scenes videos; guest home |
| `search.jpg` | Controller keyboard with “Palmeiras goleadas” |
| `search-results.jpg` | Curated public results for the same query |
| `channel.jpg` | TV Palmeiras, public avatar and videos, no signed-in account |
| `player.jpg` | A real excerpt of Palmeiras 8×1 Independiente Petrolero, native H.264/AAC player |
| `rate.jpg` | Current player with the Like icon focused; fictional Demo account |
| `up-next.jpg` | Native countdown to TV Palmeiras’s Palmeiras 5×0 São Paulo behind-the-scenes video |
| `settings.jpg` | Settings with fictional Demo account; no personal identity |
| `signin.jpg` | Account TV-code panel; `DEMO-CODE` is a nonfunctional placeholder |
| `sleep-timer.jpg` | Native console shutdown warning with football behind it |
| `cast.jpg` | Native phone panel, loopback receiver, placeholder `123 456 789 012` |
| `nest.jpg` | Native sound menu with generic Google Nest configuration; no real speaker connection |
| `members-badge.jpg` | Synthetic member badge in the TV Palmeiras layout; the card explicitly says Demo |
| `members-only.jpg` | Native channel-members refusal driven by a synthetic player response |
| `hero.jpg` | 1600×820 HTML composition using the new home capture |

The public feed mixes 8×1, 6×2, 6×1, 5×0, 5×1, 4×0, 4×1, 0×5 and 0×3. It is a curated demonstration,
not an unmodified anonymous recommendation feed. Public metadata/thumbnails were fetched once and reused
locally with pauses between requests. The player uses a 20-second excerpt of
[SBT Sports’s Palmeiras 8×1 highlights](https://www.youtube.com/watch?v=68WY6G_wjoY).
TV Palmeiras material includes [5×0 São Paulo](https://www.youtube.com/watch?v=jzeFpEF6FQ8),
[6×2 CSA](https://www.youtube.com/watch?v=5sVEcIZQlL0),
[6×1 Remo](https://www.youtube.com/watch?v=6ElN1CStOUE) and
[0×5 Independiente Petrolero](https://www.youtube.com/watch?v=JQ_uHzugp8c).

No active live stream was found on the channel’s public Streams tab during capture. Public searches did
not provide a verifiable TV Palmeiras members-only item, so membership behavior is shown with synthetic
responses rather than claiming that a public video is exclusive. No real account or member video was opened.
The Nest address is documentation-only, and the screenshot stops before applying the speaker selection.

The old rating modal is no longer reachable in this version: Like and Dislike are direct player actions.
The new `rate.jpg` therefore captures the actual current icon row. All images were opened and reviewed;
titles deliberately use the app’s normal ellipsis, and the home’s next row remains partly visible as its
normal scrolling affordance. JPEG/PNG metadata is stripped before distribution. The website copies match
`docs/img/`; `player-clean.jpg` is a still from the same public excerpt. Its interactive preview is simplified.

## Final integration captures

The final merged app adds `comments.jpg`, `speed.jpg`, `spanish.jpg`, `crt.jpg`, `crt-fill.jpg`, `large-text.jpg` and `dislikes.jpg`, with byte-identical website copies. All were opened and inspected. Comments use clearly fictional “Torcedor · Demo” names and synthetic text/counts; the RYD count is a demonstration value, not a measured count for the public clip. Footage is the same public Palmeiras excerpt used by the release captures. No account login, external speaker or console was used.

Comments, speed, Spanish settings, large text and dislike count: 1280×720. CRT fit/fill: actual 640×480 framebuffer, with letterboxing or crop respectively. JPEG metadata was stripped and the final metadata/byte audit covers the copies in the built website too. These images prove layout on the PC renderer; they do not certify physical CRT overscan, Xbox performance or listening quality.
