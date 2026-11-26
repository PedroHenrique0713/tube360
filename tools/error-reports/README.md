# Tube360 error receiver

Python 3.10+, standard library only. Run one process/replica so the in-memory IP
limiter and serialized GitHub delivery have one owner. No VPS deploy is performed.

```
export REPORT_DATA=/var/lib/tube360-reports
# Optional: without BOTH settings, receive/count locally and do not contact GitHub.
export REPORT_TRIAGE_REPO=revive360/tube360-reports
export GITHUB_TOKEN=...  # fine-grained: Metadata read, Issues read/write, only this private repo
python3 tools/error-reports/receiver.py
```

`REPORT_TRIAGE_REPO` has **no default**. The former `GITHUB_REPO` variable is ignored
so an old public-repository configuration cannot publish reports. An empty repo,
missing token, or a repo that GitHub reports as public prevents all issue writes,
including summaries. Create the `crash-report` label in the private repo first.

The listener defaults to 127.0.0.1:8080. Terminate HTTPS at your reverse proxy for
`https://tube360.hypermind.space/api/report`. Forward only POST `/api/report`;
limit bodies to 64 KiB, disable access/error logs containing IPs, and do not cache.
Do not expose the HTTP listener to the internet. For one local reverse proxy, set
`REPORT_TRUSTED_PROXIES=127.0.0.1` and **overwrite** `X-Real-IP` with the proxy's peer
IP. Headers from other peers are ignored. With a CDN, configure the proxy's trusted
CDN ranges first. Use the exact container/proxy peer address, not every address.
`REPORT_BIND`, `REPORT_PORT`, `REPORT_DATA` are optional. The Dockerfile uses an
unprivileged user; mount writable `/data`, publish only to loopback
(`-p 127.0.0.1:8080:8080`).

## Protocol and classification

POST JSON has eight string fields: `version`, `build`, `hardware`, `screen`,
`error`, `log`, `category`, `installation_id`. The ID is 32 lowercase hexadecimal
characters from 128 random bits, persisted in `error-report-installation.txt`.
It is independent of console/account/network identity; copying the folder copies
the ID. The receiver stores only its digest for retained crash/startup reports,
never the ID or digest in GitHub. Legacy six-field reports remain accepted and
counted, but without an installation ID they never help reach the triage threshold.
Existing SQLite data migrates automatically; old public issue numbers are ignored.

The launcher's category is a hint. The receiver independently confirms:

- `crash`: a fault/panic/GPU fence failure, hang or unclean session.
- `startup`: app handoff/initialization failure, missing fonts/storage/video setup.
- `network`: DNS/TLS/connection timeout/offline.
- `youtube`: player `LOGIN_REQUIRED`, `UNPLAYABLE`, or a media/link HTTP 403.
- `other`: no confirmed cause.

An explicit fault takes priority; connectivity/YouTube errors in the primary error
are distinguished from incidental older network lines in a crash log. Known
sensitive error lines are reduced to controlled cause codes before scrubbing, so
classification survives removal of login/credential details. Classifiers are
heuristics, not trusted crash attestation.

Responses: 200 `{"code":"T360-..."}` acknowledges durable local receipt, 400
malformed, 413 >64 KiB, 429 quota, 503 storage unavailable. The launcher never waits
for GitHub. Identical sanitized reports from the same installation reuse a receipt
and do not increment counters again; a changed diagnostic counts as another report.

## Triage and daily summary

Only `crash` and `startup` signatures may create triage. A signature is the confirmed
**category + normalized error checkpoint + app version**. Details such as timestamps
or addresses do not split the same checkpoint. At least **three distinct retained
installation IDs** are required in the 30-day window. Repeated reports from one or
two IDs never create a triage item. Expiry/disk eviction can reduce this population.

One issue per signature (including an existing closed issue) contains retained
report count, distinct installation count and latest sanitized log. Delivery uses
PATCH to replace the body, not a comment per report. Markers and body comparisons
recover lost HTTP responses without duplicate issues or duplicate updates.
Network/YouTube/other reports retain no diagnostic body or installation digest;
only deduplication metadata and category/version counters remain.

A **single separate summary issue** is updated once per UTC day, after the day
closes. It lists counters by completed UTC date, category and version for retained
days. Today's partial counts appear after the next UTC boundary. Empty histories
create no summary. Downtime is caught up from retained counters; bodies over the
GitHub size limit prioritize the most recent complete rows. Successful publication
state survives restart; failures retry, and an identical remote body is not
rewritten if the prior response was lost. No log or installation identifier is in
the summary. One background worker retries every 30 seconds; network calls do not
hold the HTTP/database lock.

The API contract uses GitHub's [repository metadata](https://docs.github.com/en/rest/repos/repos#get-a-repository)
and [list/create/update issue endpoints](https://docs.github.com/en/rest/issues/issues).
All tests use a local fake implementing those contracts, never the live GitHub API.

## Limits and privacy

Each IP has a random-salted HMAC key and up to five timestamps in RAM for one hour.
No IP or IP hash goes to disk/stdout. Restart resets that limiter. The 1,000 unique
reports per UTC day counter persists. SQLite is pruned every 30 seconds and on
receipt to 30 days/200 MiB, oldest report first; compact category/version counters
survive raw log eviction until expiry. Budget temporary space for VACUUM and keep
other files out of the data directory. GitHub outages retain eligible diagnostics
only within these limits. Private GitHub issues can outlive local retention; old
logs are replaced on update, but GitHub revision history may remain. Do not back
up raw reports beyond 30 days. See [PRIVACY.md](../../docs/PRIVACY.md).

```
python3 -m unittest discover -s tools/error-reports -v
python3 tests/update/error-reports-test.py client/build
python3 tests/update/timer-session-test.py client/build
python3 tests/update/exit-session-test.py client/build
```
