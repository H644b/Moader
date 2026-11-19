#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host regression of the production bootstrap mount-serving loop and PM worker.
set -eu
cd "$(dirname "$0")"
nx=${LIBOUNCE:-../../../../api_reimplement/libounce-nx/nx}
out=${TEST_OUT:-build/tests}
mkdir -p "$out"
cxx=${CXX:-g++}
flags="-std=gnu++20 -pthread -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-invalid-constexpr -fsanitize=address,undefined -fno-sanitize=alignment -D__SWITCH__ -D__SWITCH2__ -DLIBNX_HOST_TEST_TLS"
$cxx $flags -I../hen/tests/host -I"$nx/include" -I../libkaihen/include tests/launch_mount_test.cpp -o "$out/launch_mount"
"$out/launch_mount"
