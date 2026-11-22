#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
kit=$1
[ -n "$kit" ] || exit 2
kit=$(cd "$kit" && pwd -P)
case "$(uname -s)" in
  Darwin) platform=darwin; data_base="$HOME/Library/Application Support/Moader Setup" ;;
  Linux) platform=linux; data_base="$HOME/.local/share/Moader Setup" ;;
  *) exit 2 ;;
esac
case "$(uname -m)" in
  arm64|aarch64) arch=arm64 ;;
  x86_64|amd64) arch=x64 ;;
  *) arch=unsupported ;;
esac
mkdir -p "$data_base"
show_error() {
  message=$1
  file="$data_base/launch-error.html"
  cat > "$file" <<EOF
<!doctype html><meta charset="utf-8"><title>Moader Setup needs attention</title>
<style>body{font:17px system-ui;background:#121918;color:#f3f6ed;padding:8vw;max-width:720px;line-height:1.55}h1{font-size:29px}p{color:#c5d0c8}</style>
<h1>Moader Setup needs attention</h1><p>$message</p>
<p>Keep the extracted kit. The setup files and existing reports were not deleted.</p>
EOF
  if [ "${MOADER_SETUP_DETACH:-0}" = 1 ]; then
    printf '%s\n' "$message" >&2
    exit 1
  fi
  if [ "$platform" = darwin ]; then open "$file" >/dev/null 2>&1 || :; elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$file" >/dev/null 2>&1 || :; elif command -v explorer.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then explorer.exe "$(wslpath -w "$file")" >/dev/null 2>&1 || :; fi
  exit 1
}
[ "$arch" != unsupported ] || show_error "This computer architecture is not supported by the included controller runtime."
node="$kit/host-runtimes/node-v24.22.3/$platform-$arch/node"
[ -f "$node" ] || show_error "The included Node 24 runtime for this computer is missing. Extract the complete backend kit again."
if [ "${MOADER_SETUP_FORCE_COPY:-0}" = 1 ] ||
   ! mkdir -p "$kit/state/setup" 2>/dev/null ||
   ! : > "$kit/state/setup/.write-check" 2>/dev/null; then
  id=$({ printf '%s' "$kit"; cat "$kit/config/release.json" "$kit/setup/controller.cjs" "$kit/setup/launch.sh" \
    "$kit/dist/Moader.nro" "$kit/dist/moader.nso" \
    "$kit/vendor/kaihen-mo/hen/main" "$kit/vendor/kaihen-mo/ro/main" \
    "$kit/vendor/kaihen-mo/bootstrap.bin"; } |
    (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16)
  target="$data_base/kits/$id"
  if [ ! -f "$target/setup/controller.cjs" ]; then
    mkdir -p "$data_base/kits"
    staging="$data_base/kits/.copy-$id-$$"
    mkdir "$staging"
    cp -R "$kit/." "$staging/" || show_error "The kit could not be copied to a writable app-data folder."
    chmod -R u+w "$staging" || show_error "The kit copy could not be made writable."
    mv "$staging" "$target" || show_error "The writable kit copy could not be completed."
  fi
  kit="$target"
  node="$kit/host-runtimes/node-v24.22.3/$platform-$arch/node"
else
  rm -f "$kit/state/setup/.write-check"
fi
mkdir -p "$kit/state/setup"
chmod u+x "$node" 2>/dev/null || :
version=$("$node" --version 2>/dev/null || :)
[ "$version" = v24.22.3 ] || show_error "The included Node runtime failed its version check."
actual=$("$node" -p 'process.platform + "-" + process.arch' 2>/dev/null || :)
[ "$actual" = "$platform-$arch" ] || show_error "The included Node runtime does not match this computer."
if [ "${MOADER_SETUP_DETACH:-0}" = 1 ]; then
  log="$kit/state/setup/launch-$$.log"
  MOADER_SETUP_NO_OPEN=1 nohup "$node" "$kit/setup/controller.cjs" > "$log" 2>&1 </dev/null &
  launch_pid=$!
  attempt=0
  while [ "$attempt" -lt 150 ]; do
    ready=$(sed -n '/^Moader Setup is ready at http:\/\/127\.0\.0\.1:/s/^Moader Setup is ready at //p' "$log" | tail -n 1)
    if [ -n "$ready" ]; then
      printf 'Moader Setup is ready at %s\n' "$ready"
      exit 0
    fi
    if ! kill -0 "$launch_pid" 2>/dev/null; then
      show_error "The setup controller stopped before it opened. Check its saved launch report."
    fi
    attempt=$((attempt + 1))
    sleep 0.1
  done
  show_error "The setup controller did not open in time. Check its saved launch report."
fi
if [ "$platform" = darwin ]; then
  electron="$kit/host-runtimes/electron-v45.4.1/$platform-$arch/Electron.app/Contents/MacOS/Electron"
else
  electron="$kit/host-runtimes/electron-v45.4.1/$platform-$arch/electron"
fi
[ -f "$electron" ] || show_error "The included Moader Setup desktop runtime is missing. Extract the complete backend kit again."
chmod u+x "$electron" 2>/dev/null || :
exec "$electron" "$kit/setup/desktop-app"
