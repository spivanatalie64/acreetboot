#!/usr/bin/env bash
# UML boot test of the microboot init logic
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
BIN="${1:-/srv/cvm/uml/uml-microboot/uml-microboot}"
LOG="${2:-/srv/cvm/uml/umd-boottest.log}"
echo "VIRTUALIZATION=uml (no VT-x needed — kernel runs as a userspace process)"
# Init finds no payload => prints the loud reason, then (recover=1) drops to
# the busybox recovery shell: our deterministic boot-chain gate marker.
timeout 30 "$BIN" acreetboot.recover=1 mem=128M \
  </dev/null >"$LOG" 2>&1 || true
if grep -q "acreetboot: dropping to recovery shell" "$LOG"; then
  echo "PASS: microboot init reached its recovery shell over UML"
else
  echo "FAIL: recovery shell marker missing (see $LOG)"; tail -30 "$LOG"; exit 1
fi
