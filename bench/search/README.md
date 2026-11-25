# F43 — Search

See [behavior, recorded provenance, test commands and console acceptance](../../docs/SEARCH.md).

`test.cpp` checks the actual public parameters and typed renderer fragments, parser
bounds/failures, normalization, cache separation and continuation ownership.
`worker.cpp` holds an old response while sending 5,000 edits, deliberately ignores
cancellation in its fake transport and requires that only the newest result wins.
Checks execute in Release builds too; they do not use disabled C `assert` macros.

`prepare.py` relinks production PC code against a closed transport. `e2e.py` drives
real controller input for suggestions, filters, search, channel/playlist opening,
B/cancel/clear, recent searches, failures and frame timing in all four languages
and combined 4:3/large text. It writes an assertion-checked manifest of captures.
The negative cases intentionally use 503, malformed responses and slow/stale data.

The frame histogram is desktop-only and opt-in (`XT_SEARCH_METRICS`). No console
benchmark probe or test network route is compiled into the Xbox app.
