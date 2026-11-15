#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host tests of HEN's code that does not need the console.
set -eu
cd "$(dirname "$0")"
nx=${LIBOUNCE:-../../../../api_reimplement/libounce-nx/nx}
out=${TEST_OUT:-build/tests}
mkdir -p "$out"
cxx=${CXX:-g++}
# libnx's HIPC parser reads unaligned fields, as allowed on AArch64. Keep the
# other ASan/UBSan checks.
flags="-std=gnu++20 -pthread -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-invalid-constexpr -fsanitize=address,undefined -fno-sanitize=alignment -D__SWITCH__ -D__SWITCH2__ -DLIBNX_HOST_TEST_TLS"
for test in mount_request sm_request reload lifecycle_server loader_gate; do
    $cxx $flags -Itests/host -I"$nx/include" -I../libkaihen/include "tests/${test}_test.cpp" -o "$out/$test"
    "$out/$test"
done
