#!/usr/bin/env bash
# AcreetBoot — initramfs builder for the GKI-model UKI lane
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# GKI-model decision (recorded): the initramfs is IMPORTED INTO the UKI at
# build time. The board is /init = c/bootstage.c compiled statically (the
# hardened pid-1 microboot stage). Deterministic packing: sorted file list,
# cpio newc, gzip -9. Staged to build/ukistage/initramfs-acr.cpio.gz.
#
# Chain context note: in the VM/e2e lane there is no ACREETBOOT-ZSWAP
# payload partition, so /init honestly exits with the "acreet-boot:
# catastrophic stop" line (kernel then panics on dead init — bounded by
# panic=-1 + -no-reboot in the test lane). That line is part of the e2e
# assertion set: it proves OUR chain artifacts actually run inside the
# kernel we shipped.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
STAGE="$(mktemp -d "$BUILD/initramfs-stage.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

command -v gcc   >/dev/null || { echo "FATAL: gcc missing" >&2; exit 1; }
command -v strip >/dev/null || { echo "FATAL: strip missing" >&2; exit 1; }
command -v cpio  >/dev/null || { echo "FATAL: cpio missing" >&2; exit 1; }
command -v gzip  >/dev/null || { echo "FATAL: gzip missing" >&2; exit 1; }

mkdir -p "$BUILD/ukistage"

echo "==> building static bootstage (pid-1 stage)"
gcc -O2 -std=c99 -Wall -Wextra -static -s \
    -o "$STAGE/init" "$ROOT/c/bootstage.c"
file "$STAGE/init"

# Static binary sanity: pid-1 stage, deterministic banner strings inside.
grep -qa "acreet-boot" "$STAGE/init" || { echo "FATAL: bootstage banner missing" >&2; exit 1; }

mkdir -p "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/payload" "$STAGE/esp"
ls -la "$STAGE"

echo "==> packing initramfs (cpio newc, gzip -9)"
( cd "$STAGE" && find . -mindepth 0 | LC_ALL=C sort | cpio -o -H newc --owner=0:0 2>/dev/null ) \
  | gzip -9 > "$BUILD/ukistage/initramfs-acr.cpio.gz"

SIZE="$(stat -c%s "$BUILD/ukistage/initramfs-acr.cpio.gz")"
SHA="$(sha256sum "$BUILD/ukistage/initramfs-acr.cpio.gz" | awk '{print $1}')"
printf 'format=cpio-newc,gzip-9\nsize=%s\nsha256=%s\ninit=bootstage-static\ncontent=dev,proc,sys,payload,esp,init\n' \
  "$SIZE" "$SHA" > "$BUILD/ukistage/initramfs.env"
echo "staged: build/ukistage/initramfs-acr.cpio.gz ($SIZE bytes, sha256 $SHA)"
