#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
source_kit=$1
base="$HOME/.local/share/Moader Setup/wsl-kits"
mkdir -p "$base"
id=$({ printf '%s' "$source_kit"; cat "$source_kit/config/release.json" \
  "$source_kit/setup/controller.cjs" "$source_kit/dist/Moader.nro" \
  "$source_kit/dist/moader.nso" "$source_kit/vendor/kaihen-mo/hen/main" \
  "$source_kit/vendor/kaihen-mo/ro/main" \
  "$source_kit/vendor/kaihen-mo/bootstrap.bin"; } |
  sha256sum | cut -c1-16)
target="$base/$id"
if [ ! -f "$target/setup/controller.cjs" ]; then
  temporary="$base/.copy-$id-$$"
  mkdir "$temporary"
  cp -R "$source_kit/." "$temporary/"
  chmod -R u+w "$temporary"
  mv "$temporary" "$target"
fi
MOADER_SETUP_DETACH=1 exec /bin/sh "$target/setup/launch.sh" "$target"
