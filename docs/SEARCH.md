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
Order: relevance and view count. Channel/playlist disable duration
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

The public menu observed on 2026-11-25 exposes Relevance and Popularity. The
coordinator then measured actual WEB `youtubei/v1/search` results for `astronomia`,
`hl=pt`, `gl=BR`, at 07h on that date and supplied this evidence:

| Params | Observation |
|---|---|
| None | Relevance baseline: dates began “há 1 ano, há 3 meses, há 1 ano, há 10 meses…” |
| `CAI=` (field 1 = 2) | Same order as relevance except the eighth entry; date ordering was ignored. |
| `CAISAhAB` (date order + video) | “há 1 mês, há 1 mês, há 7 meses, há 2 meses, há 1 ano, há 5 minutos…”; not chronological. |
| `CAM=` (field 1 = 3) | View counts began 477, 183, 89, 80, 48 million, strictly decreasing. |

Consequently, Sort by offers **Relevance** and **View count** only. Both controller
directions cycle between those two values; no supported state emits field 1 = 2.
The separate **Upload date** period filter (today/week/month/year) remains.
HTTP 200 and preserved menu parameters were insufficient evidence for the removed
date-order option. View-count ordering is supported by the coordinator's measured
results, rather than inferred from the menu label or replay tests. This evidence
is transcribed in
[`order-coordinator-20261125.json`](../bench/search/fixtures/order-coordinator-20261125.json);
it is a reported measurement, not a raw response fixture or a new request by this
feature agent. No additional live requests were needed for this correction.

Video search is anonymous with or without a connected account, including unfiltered
queries and continuation requests. It never sends the TV Bearer to WEB search;
bootstrap and search also suppress the cookie jar. This deliberately makes search
results public rather than personalized. Account recommendations, library and
Music keep their account routes. An anonymous search failure is logged with its
internal route, HTTP status and numeric API/transport code, without the query,
cursor, token, remote error message or response body. HTTP 200 with a top-level
error is a failure too. Search stays on the typed catalogue even with an optional
PC server configured; errors cannot fall through to a TV-account or PC search.
[Adversarial account-search replay and visual proofs](../bench/search-account/README.md).

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
sanitized catalogue fixtures. Runtime channel templates remap their UC identifiers
to the chosen search card so tab endpoints stay with that channel; menu/title
metadata still belongs to the older recording. Every browse ID/cursor must match
an explicit rule; unknown continuations fail closed. The test does not assert
that destination metadata belongs to the original search cards.

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
3. LB → choose video, short, week, view-count order → Apply → Menu. Check all active
   choices on results. From results, LB → change each type/date/duration/order;
   Apply reloads, B discards, Clear returns to relevance/all. Validate actual
   durations/upload dates and decreasing view counts. Sort by must cycle only
   Relevance ↔ View count, while the separate upload-date period row stays available.
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
