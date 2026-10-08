#!/usr/bin/env bash
# AcreetBoot — build orchestrator (v0 skeleton entry point)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Mammoth? No. What this does today (v0):
#   1. syntax-check every shell script and C header presence
#   2. build the userspace C tools (setbootnext; bootstage links against a
#      minimal libc userspace later — phase 0.4)
#   3. run the unit test suite
# Phases 0.2+ add: efi module compile via EDK2, microkernel kernel build,
# dist bundle staging. Milestones track in docs/plan/00-PLAN.md.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD/bin"

echo "==> acreetboot build (host: $(uname -srm))"

for f in grub/grub.cfg sdboot/loader.conf sdboot/10-acreetboot.conf; do
  [ -f "$ROOT/$f" ] || { echo "MISSING: $f"; exit 1; }
done
for s in initramfs/init; do bash -n "$ROOT/$s" && echo "ok: bash -n $s"; done

if command -v gcc >/dev/null; then
  gcc -O2 -std=c99 -Wall -Wextra -DPOSIX_C_SOURCE=200809L \
      -o "$BUILD/bin/setbootnext" "$ROOT/c/setbootnext.c"
  echo "ok: built build/bin/setbootnext"
else
  echo "no gcc on host; skipping C tools (build will be incomplete)"
fi

test/run_all.sh "$ROOT/build/bin/setbootnext" && echo "ALL TESTS GREEN"
