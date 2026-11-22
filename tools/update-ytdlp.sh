#!/usr/bin/env bash
# Keeps yt-dlp current: YouTube changes often and the fixes ship in new yt-dlp releases.
# Run hourly by tube360-update.timer. Upgrades about once a day (20 h apart) and only while nothing is open:
# the server must restart to load the new version, and a restart cuts the video on the TV.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
# same interpreter the server uses (server/run.sh)
export PATH="$HOME/.pyenv/shims:$HOME/.local/bin:$PATH"
python=${TUBE360_PYTHON:-python3}
[ -x "$root/.venv/bin/python" ] && [ -z "${TUBE360_PYTHON:-}" ] && python=$root/.venv/bin/python
state=${XDG_CACHE_HOME:-$HOME/.cache}/tube360
mkdir -p "$state"
stamp=$state/ytdlp-checked pending=$state/ytdlp-restart-pending
port=${TUBE360_PORT:-8091}

version() { "$python" -c 'import yt_dlp.version as v; print(v.__version__)'; }
# Busy (exit 0) while a video is open on the console, or when the server state cannot be read: when in doubt, never
# restart. "playing" counts even if the console stopped polling, since a network blip does not end the stream.
busy() {
  local state
  state=$(curl -s -m 5 "http://127.0.0.1:$port/api/remote/state" || true)
  printf '%s' "$state" | "$python" -c 'import json, sys
try:
    s = json.load(sys.stdin)
except ValueError:
    sys.exit(0)
sys.exit(0 if s.get("playing") else 1)'
}
restart() {
  if busy; then echo "new yt-dlp waiting: a video is open (or the server did not answer)"; return; fi
  systemctl --user restart tube360.service
  rm -f "$pending"
  echo "server restarted with yt-dlp $(version)"
}

if [ -f "$pending" ]; then restart; exit 0; fi
[ -f "$stamp" ] && [ -z "$(find "$stamp" -mmin +1200)" ] && exit 0
if busy; then echo "yt-dlp check postponed: a video is open (or the server did not answer)"; exit 0; fi
before=$(version)
# --user only when yt-dlp lives in the user site (pyenv/system Python), never inside a venv
user=$("$python" -c 'import site, sys, yt_dlp; print("--user" if sys.prefix == sys.base_prefix and yt_dlp.__file__.startswith(site.getusersitepackages()) else "")')
# [default] brings the yt-dlp-ejs version this release expects (YouTube's JS challenge solver)
"$python" -m pip install --quiet --disable-pip-version-check --upgrade $user "yt-dlp[default]"
touch "$stamp"
after=$(version)
[ "$before" = "$after" ] && { echo "yt-dlp $after is current"; exit 0; }
echo "yt-dlp $before -> $after"
touch "$pending"
restart
