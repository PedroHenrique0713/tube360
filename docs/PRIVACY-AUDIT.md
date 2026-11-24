# v0.3.0 privacy audit

Scope: tracked files at the release branch, all objects introduced by `89a7158..HEAD`, build output and the
Xbox app ZIP. Local `.git` worktree pointers, build caches and the private audit evidence are not distribution
inputs. History was inspected without rewriting it. This is a bounded review, not a claim that arbitrary
personal names or every possible credential format can be recognized automatically.

## Findings and disposition

| Finding | Location | Disposition |
|---|---|---|
| Speaker LAN IP and personal display label | `docs/PROTOCOL.md`, Direct Cast example; older versions remain in history | Replaced with documentation address `192.0.2.20` and a generic Cast label. History retained for the coordinator. |
| Absolute developer HOME in a command | `bench/quality/README.md:27` and its earlier blob | Replaced with `python3`; no developer username needed. |
| Absolute developer HOME in an isolation guard | `bench/quality/server_fixture.py:14` | Remaining code finding, outside this documentation/packaging scope. The integration owner should make the guard independent of one user's HOME. Not included in app ZIP. |
| Private-range example addresses | Previous README versions; `site/src/template.html:140` | README examples now use documentation addresses. The earlier site used a generic PC address, not the measured speaker address. The release-final website now uses console-only installation without an IP command. |
| Four public contact emails, creator names and public channel/playlist IDs | Captured public descriptions in `bench/catalog/fixtures/music-explore.json` (notably lines 1588, 2669, 2849) | Public content, not the user's account grant. No new copy in release docs; excluded from app ZIP. Fixture owner should minimize descriptions before distributing source if unnecessary for parsing tests. |
| Account/access/refresh/client-secret literals | Eight fixture/test files under `bench/` and `tests/cast` | Reviewed values are explicit `fake-*`, `fixture-*`, `console-fixture-token` or `expired-fixture`; no live token identified. |
| Visitor/account-like identifiers | Recorded OAuth request fixture and synthetic transport tests | Reviewed visitor strings are synthetic, including an encoded fixture visitor. Public video/channel IDs are content identifiers, not evidence of the owner's private account. No private account ID identified. |
| Author names, author/committer emails and copyright names | Git commits, LICENSE, README credits, third-party code/notices | Attribution and normal Git authorship retained. Not anonymized or history-rewritten. Other name-pattern hits include Portuguese words for video frames. |
| Developer HOME prefixes embedded in libraries/toolchain | Linked ELF and raw `client/build-x360/tube360.xex` | App packager verifies ELF/XEX correspondence, sanitizes fixed-length diagnostic path prefixes in a copy and reconverts the XEX. Original build retained. All ZIP members scanned again. |
| Existing screenshots | `docs/img/`, website screenshot copies and OG images | Contact sheet inspected: demo account and placeholder login/Cast codes; public video/channel names. No personal speaker/IP/account shown. EXIF scan found no author/owner/GPS fields in 27 images. |

The original baseline comparison covered **144 commits, 583 blobs and 599 trees**; tracked input before these
new release files was 276 files. Full values, object IDs and line numbers are in the ignored local audit
evidence, rather than repeated in public documentation. Historical private addresses/paths remain discoverable
in old blobs even after the current documentation is corrected. If any credential is later determined to be
real, removal from the tip does not invalidate it; coordinate handling before publication.

## Distribution boundary

Only fonts, the current executable, inactive generic examples, instructions, licenses and a hash manifest
enter the Xbox app ZIP. Neither Git history nor runtime files enter it. Public copyright/third-party contact
information is preserved. A source/relink distribution must receive its own final review after integration:
the remaining fixture/guard findings above mean this branch is not a sanitized export of all source/history.
See [release preparation](RELEASE.md).

## Release-final review — 2026-11-24

The app ZIP and matching 7z are built from an explicit list of 26 files. Both contain the same sanitized XEX,
four fonts, inactive commented examples, instructions, license notices and manifest. Runtime account/Cast/Nest
files are excluded. The current Xbox build needed 2,479 equal-length HOME-prefix replacements in a copy of
the linked ELF; the converter first reproduced the unmodified XEX exactly. Original executable code sections
are checked for byte equality before conversion. The raw build is retained privately.

The new [screenshots](SCREENSHOTS.md) use public Palmeiras material and fictional account/pairing data.
The member badge/refusal responses are synthetic and disclosed; no real account, console or speaker was used.
JPEG and PNG metadata were inspected with ExifTool, including the website's Open Graph images; no EXIF, XMP,
IPTC, GPS, author/owner or comment fields were found in the final image set. Visible identities were reviewed
by opening the captures, rather than inferring privacy from a metadata scan.

The final archive contents, all website output files (including images), and image bytes are scanned for
developer HOME/Windows/macOS user paths, the local username, private LAN addresses and common Google access
token/JWT/refresh-token patterns. Public author attribution in LICENSE and dependency notices is retained.
Checksums and detailed commands/results are in the coordinator's ignored release-final report. This remains
a bounded pattern/visual audit; it does not claim that every possible secret format can be detected.

The original matching-source/relink obligations still apply before public distribution. This front prepares
the installable app archives; the table above retains source-tree findings for the integration owner.
