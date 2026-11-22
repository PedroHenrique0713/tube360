#!/usr/bin/env bash
# Installs the server and the Cast receiver as systemd user services (start on login).
#   tools/install-services.sh            name shown in the YouTube app's Cast menu: Tube360
#   TUBE360_NAME="Living Room" tools/install-services.sh
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
node=$(command -v node) || { echo "node (>= 18) not found in PATH" >&2; exit 1; }
units=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user
mkdir -p "$units"
for f in tube360.service tube360-cast.service; do
  sed -e "s|@ROOT@|$root|g" -e "s|@NODE@|$node|g" -e "s|@NAME@|${TUBE360_NAME:-Tube360}|g" "$root/deploy/$f.in" > "$units/$f"
done
[ -f "$root/server/config.json" ] || cp "$root/server/config.example.json" "$root/server/config.json"
systemctl --user daemon-reload
systemctl --user enable --now tube360.service tube360-cast.service
echo "Tube360 running. Phone remote: http://$(hostname -I | awk '{print $1}'):8091"
