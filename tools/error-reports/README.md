# Tube360 error receiver

Python 3.10+; standard library only. Run one process/replica so GitHub delivery
and the in-memory IP limiter have a single owner. Nothing here deploys to a VPS.

```
export GITHUB_TOKEN=...   # fine-grained token: Issues read/write, only this repository
export GITHUB_REPO=revive360/tube360
export REPORT_DATA=/var/lib/tube360-reports
python3 tools/error-reports/receiver.py
```

Create the `crash-report` label in the target repository first. The receiver binds
to 127.0.0.1:8080 by default. Terminate HTTPS at your reverse proxy for
`https://tube360.hypermind.space/api/report`. Forward only POST `/api/report`;
limit request bodies to 64 KiB, disable access/error logs containing client IPs,
and do not cache responses. Never expose the HTTP listener to the internet.

For one local reverse proxy, set `REPORT_TRUSTED_PROXIES=127.0.0.1` and make the
proxy **overwrite** `X-Real-IP` with its peer IP (not forward the incoming header).
Only explicitly listed peer addresses are trusted; otherwise headers are ignored.
With a CDN, configure the proxy's trusted CDN ranges before extracting the client
address. Use the exact proxy/container peer address for Docker, not every address.
`REPORT_BIND`, `REPORT_PORT`, and `REPORT_DATA` are optional environment settings.
The Dockerfile runs as an unprivileged user; mount a writable `/data` volume and
publish its port only to loopback (`-p 127.0.0.1:8080:8080`).

Accepted JSON has exactly six string fields: `version`, `build`, `hardware`,
`screen`, `error`, `log`. Responses: 200 `{"code":"T360-..."}`, 400 malformed,
413 >64 KiB, 429 quota, 503 storage unavailable. Codes acknowledge durable local
receipt; GitHub delivery retries from the SQLite outbox every 30 seconds. The
launcher does not wait for GitHub. Identical bodies reuse the same code for 30 days;
error + version groups one issue, including closed issues. Delivery markers avoid
duplicating comments when a GitHub response is lost. GitHub issues/comments are
public and remain until maintainers remove them; the 30-day limit applies to the
receiver's copy, not GitHub. Do not back up raw reports beyond this period.

Each IP has a salted HMAC key and up to five timestamps, held only in RAM for one
hour. No IP or IP hash is written to disk or stdout. Restart resets that limiter;
the 1,000 unique reports per UTC day counter survives restart. The SQLite store is
pruned every 30 seconds and on receipt to 30 days / 200 MiB, oldest first. Budget
additional temporary disk space for SQLite VACUUM; do not share the data directory
with other files. GitHub/network outages leave reports queued within those limits.

Run the tests (only loopback, disposable token, fake GitHub REST API):

```
python3 -m unittest discover -s tools/error-reports -v
```
