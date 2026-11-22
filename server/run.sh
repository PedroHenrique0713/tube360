#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$(readlink -f -- "$0")")"
# O systemd de usuário não carrega o shell interativo nem os shims do pyenv.
export PATH="$HOME/.pyenv/shims:$HOME/.local/bin:$PATH"
if [[ -d "$HOME/.nvm/versions/node" ]]; then
  for node_dir in "$HOME"/.nvm/versions/node/*/bin; do
    export PATH="$node_dir:$PATH"
  done
fi
exec "${TUBE360_PYTHON:-python}" -u tube360_server.py
