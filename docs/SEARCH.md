# Search filters and suggestions

Open Search with Y. Suggestions appear below the query, beside the keyboard in
widescreen and in a scrolling row below it in 4:3. Move right past the keyboard's
last key to focus the list, use up/down and A to search immediately. Left returns
to typing; B returns to the previous page. Recent searches share the list, carry a
Recent label and are deduplicated against suggestions. They remain session-only,
as before. Suggestions are unavailable in the separate Music search.

LB opens Filters from the keyboard or public search results. Up/down selects a
row; left/right or A changes its value. Select Apply to use the draft, or Clear
filters to apply the defaults. X clears the draft; B discards it. Results show the
active choices. Filters remain with their page when visiting a channel/playlist
and returning with B. The keyboard reuses the current results' choices.

Types: all, video, channel, playlist and live. Duration: any, under 3 minutes,
3–20 minutes and over 20 minutes. Upload date: any, today, week, month and year.
Order: relevance, upload date and view count. Channel/playlist disable duration
and upload date; live disables duration. Selecting duration from All selects
Video. Filtering happens on YouTube, before pagination; the app does not sort a
partial page locally. Typed results open channels/playlists without starting the
video player.

## YouTube contract and current limits

Suggestions use an anonymous HTTPS GET to
`suggestqueries-clients6.youtube.com/complete/search?client=youtube&ds=yt&hl=…&gl=…&q=…`.
The public JSONP wrapper is parsed as data, never executed. The endpoint was
checked without cookies or an account on 2026-11-25. The maintained
[YouTube.js implementation](https://github.com/LuanRT/YouTube.js/blob/main/src/Innertube.ts)
also uses this client contract, with the same
[host constant](https://github.com/LuanRT/YouTube.js/blob/main/src/utils/Constants.ts). No visitor/bootstrap/OAuth session is needed for
suggestions; the transport explicitly suppresses cookies and sends no grant.

The worker waits 300 ms after an edit and spaces request starts by at least 750 ms.
Changing query, language or leaving search cancels the transfer and invalidates
its generation. Late responses cannot replace current data even if the transport
does not honor cancellation. Requests have 1.5 s connect/2.5 s total limits, no
automatic retries and a 32 KiB body cap. The parser limits nesting, nodes, text
length and list size (8). Failure leaves typing, recent searches and submission
available without an error toast. Rendering does no network or JSON work. On
Xbox the sleeping worker has the existing background role, HW5 / priority −1.

Filters are the small protobuf in WEB `searchEndpoint.params`, base64 then percent
encoded. The API JSON receives that exact string; the internal navigation URL
encodes it once more for transport. The recorded public menu is the oracle for
types, dates, live, durations and view count. The current
[SearchFilter schema](https://github.com/LuanRT/YouTube.js/blob/main/protos/misc/params.proto)
agrees on the changed 3-minute boundary. It replaces the older 4-minute duration
values; copying the old short/medium constants would silently request the wrong
range.

The public menu observed on 2026-11-25 exposes Relevance and Popularity instead of
the older sort list. Upload-date order is retained as requested using field 1 = 2,
documented in the
[v16.0.1 schema](https://github.com/LuanRT/YouTube.js/blob/v16.0.1/protos/misc/params.proto).
A public request with `CAI%3D` returned HTTP 200 and preserved that field when
combining menu filters. This suggests recognition, but does **not** establish
effective chronological ordering. Date order and current view-count semantics
need acceptance on actual results; Popularity may rank differently from strict
view count. No claim of verified ordering follows from the replay tests.

Public search now uses the typed catalogue in `yt/catalog.hpp`, sharing channel,
playlist, WEB bootstrap, pagination and cache infrastructure. The cache/cursor key
includes query, params and language. Filtered search errors cannot fall through to an
older PC endpoint that discards these parameters. Music's filters remain in its
own catalogue and client identity.

## Reproduce PC proofs

Build with the usual local FFmpeg/toolchain configuration:

```sh
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 search-test search-worker-test
client/build/search-test "$PWD/bench/search/fixtures"
client/build/search-worker-test
python3 bench/search/prepare.py .work/search/ui
python3 bench/search/e2e.py .work/search/ui
```

Only fixture preparation's ImageMagick and the build are required; these tests
make no internet requests. The optional `search-live-probe --live` is a separate,
explicit opt-in that sends exactly one public request through production HTTPS;
it is excluded from default builds and offline test runs. The replay binary relinks the production main object
and wraps every `yt::https` constructor. Unrecorded requests fail closed; account,
HOME/XDG/cache/settings and assets are isolated, and phone discovery is disabled.
It never opens personal Tube360 configuration. Screenshots and app logs live in
`.work/search/ui/<case>/`; open the PNGs to review appearance. Pass case names after
the directory to execute a subset. `frames.json` reports offscreen PC frame
timings under ordinary, fast-edit and 1.8 s suggestion-delay cases; these are not
Xbox frame-time measurements.

Fixture provenance: `bench/search/fixtures/suggestions-pt.jsonp` is one public
response for `astronomia`. `search-public.json` contains reduced public renderer
fragments from the corresponding anonymous results page (titles/IDs/navigation
and thumbnail URLs), with tracking/session/ads removed. `home.html` retains only
the published WEB name/version. `filters-public.json` and `order-date-public.json`
retain menu labels/parameters only. Three YouTube requests total were used for
capture, separated by implementation work. One additional, later request through
the production curl/Mbed TLS probe returned eight suggestions without an account. No cookies, grants or media were
collected. Continuation, transport failures, delayed responses, other query echoes, Arabic completion rows
and gradient artwork in the tests are **synthetic**, explicitly isolated from
the public recordings. Channel/playlist destination responses reuse the existing
sanitized catalogue fixtures. The test does not assert that their displayed
metadata belongs to the original search cards.

## Console acceptance (coordinator only)

Do not deploy from the feature worktree. Integrate and build the coordinator's
candidate with the shared Xbox lock, then use the coordinator's existing console
installation/log procedure. Keep real accounts and `net_log.txt` out of packages.

1. With server off, press Y and type a public query slowly. Suggestions should
   appear after a pause. Type quickly, change query, press B during a request and
   reopen: no stale list, frozen keys or interruption of B. The log should show
   `app-thread: suggestions HW 5 … priority -1`.
2. Move right off the final keyboard key; navigate up/down and A. It should search
   the selected suggestion immediately. Repeat a search and confirm a single
   recent entry, with recent searches still available when internet is absent.
3. LB → choose video, short, week, date order → Apply → Menu. Check all active
   choices on results. From results, LB → change each type/date/duration/order;
   Apply reloads, B discards, Clear returns to relevance/all. Validate actual
   durations/upload dates and the order caveat above against today's YouTube.
4. Open a channel result and a playlist result. Expect
   `navegação channel` / `navegação playlist`; no `player: pedido` until a video or
   Play all is selected. B restores the original search selection and filters.
   Scroll past the first page with filters and confirm they remain in effect.
5. Repeat at 720p and 4:3 with large text in pt/en/es/ar. In Arabic, confirm joined
   letters, right alignment, mixed numbers and RB Arabic/Latin keyboard. Native
   speaker review of the new translations is pending.
6. Compare UI frame cadence while holding keys with suggestions available,
   unavailable and slow. Confirm no new 30 fps misses and no lingering suggestion
   activity after starting playback. Record console build/hash, sample duration,
   frames/drops and whether the sixth decoder worker shares HW5. PC timing cannot
   establish this scheduler/CPU result.
