#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo 'Usage: tools/start-seedsow-gui.sh ABSOLUTE_PINNED_SEEDSOW_GUI_CHECKOUT' >&2
  exit 2
fi

moader_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
gui_root="$(cd "$1" && pwd -P)"
node24="${MOADER_NODE24:-$(command -v node || true)}"
if [[ -z "$node24" || ! -x "$node24" ]]; then
  echo 'POSIX Node.js 24 is required; set MOADER_NODE24 to its executable path.' >&2
  exit 1
fi
version="$("$node24" --version)"
if [[ ! "$version" =~ ^v24\. ]]; then
  echo "POSIX Node.js 24 is required; found $version." >&2
  exit 1
fi

"$node24" "$moader_root/tools/prepare-seedsow-gui.cjs" --check "$gui_root" >/dev/null
port="${NS2_GUI_PORT:-31338}"
if "$node24" -e '
  const net = require("node:net");
  const socket = net.connect({host: "127.0.0.1", port: Number(process.argv[1])});
  socket.setTimeout(800);
  socket.once("connect", () => { socket.destroy(); process.exit(0); });
  socket.once("timeout", () => { socket.destroy(); process.exit(1); });
  socket.once("error", () => process.exit(1));
' "$port"; then
  echo "A GUI already occupies 127.0.0.1:$port. Inspect that controller; this launcher will not attach to it." >&2
  exit 1
fi

export MOADER_ROOT="$moader_root"
export MOADER_NODE24="$node24"
export PATH="$(dirname "$node24"):$PATH"
exec "$gui_root/start-gui.sh"
