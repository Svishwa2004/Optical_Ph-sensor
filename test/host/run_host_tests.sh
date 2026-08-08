#!/usr/bin/env bash
# Host-side regression tests for the pH sensor firmware. These run on a normal
# g++ (no ESP32 toolchain, no hardware) and guard the pure-arithmetic parts of
# src/main.cpp: the fluidic geometry / timing constants and the colour->pH math.
#
# Run from anywhere:   bash test/host/run_host_tests.sh
#
# test_fluidics.cpp does NOT copy the geometry block. It #includes geom_block.inc,
# which this script re-extracts from ../../src/main.cpp on every run, so the test
# always checks the shipped source and can never drift from a stale copy. The
# extraction range is the block between TUBE_ML_PER_CM and the RINSE_DRAIN_MS line;
# any new *testable* constant must be added inside that range in main.cpp.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../../src/main.cpp"

if [[ ! -f "$SRC" ]]; then
  echo "ERROR: cannot find firmware source at $SRC" >&2
  exit 2
fi

# Regenerate the extracted geometry block so the fluidics test runs against live source.
sed -n '/static constexpr float TUBE_ML_PER_CM/,/static const uint32_t RINSE_DRAIN_MS = DRAIN_MS;/p' \
  "$SRC" > "$HERE/geom_block.inc"

fail=0

# The extracted geom_block.inc carries main.cpp's mutable runtime state
# (baseFillMs, doseMs, rinseCycles, ...) that the geometry test deliberately does
# not touch, so -Wno-unused-variable keeps that compile quiet without weakening
# -Wall for real problems. The colormath test gets full -Wall.
compile_and_run() {
  local name="$1"; shift
  echo "=== $name ==="
  g++ -std=c++17 -Wall "$@" -o "$HERE/$name" "$HERE/$name.cpp"
  if ! "$HERE/$name"; then
    fail=1
  fi
  echo
}

compile_and_run test_fluidics -Wno-unused-variable
compile_and_run test_colormath

if [[ "$fail" -ne 0 ]]; then
  echo "HOST TESTS FAILED"
  exit 1
fi
echo "ALL HOST TESTS PASSED"
