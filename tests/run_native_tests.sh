#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
export TMPDIR="${TMPDIR:-$PWD/.cache}"
mkdir -p "$TMPDIR"
BUILD="$(mktemp -d "$TMPDIR/fastcon-tests.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -Werror -Itests/stubs -Icomponents/fastcon)
"$CXX" "${FLAGS[@]}" tests/test_protocol.cpp components/fastcon/protocol.cpp components/fastcon/utils.cpp -o "$BUILD/protocol"
"$BUILD/protocol"
printf 'Protocol tests passed\n'
"$CXX" "${FLAGS[@]}" -DUSE_FASTCON_DISCOVERY tests/test_discovery.cpp components/fastcon/*.cpp -o "$BUILD/discovery"
"$BUILD/discovery"
"$CXX" "${FLAGS[@]}" -fsyntax-only components/fastcon/*.cpp
printf 'Explicit-key C++ syntax check passed (ESPHome/BLE test adapters)\n'
