#!/usr/bin/env bash
# AcreetBoot test harness — runs every test_*.sh test.
# SPDX-License-Identifier: BSD-3-Clause
set -uo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${1:-$here/../build/bin/setbootnext}"
# Test scripts run from test/ — absolutize $BINARY (realpath is idiomatic here
# and tolerant of a missing target without touching argument contracts).
command -v realpath >/dev/null && BINARY="$(realpath -m -- "$BINARY")" || BINARY="$(cd "$here/.." && readlink -f -- "$BINARY")"
pass=0; fail=0
cd "$here"
for t in test_*.sh; do
  if ACREETBOOT_TEST_BIN="$BINARY" bash "$t"; then pass=$((pass+1)); echo "PASS $t";
  else fail=$((fail+1)); echo "FAIL $t"; fi
done
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
