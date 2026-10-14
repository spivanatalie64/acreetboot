#!/usr/bin/env bash
# AcreetBoot — Debian stable kernel fetcher (build-time lane)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Kernel lane decision (recorded): the *stable boot kernel* for AcreetBoot
# UKIs is the Debian kernel, download from the signed pool, so the artifact
# itself is verified against deb.debian.org's index before it is every used.
# ukify wraps it; Debian owns the kernel maintenance. Our lane never touches
# the kernel: fetch, verify, extract vmlinuz. Idempotent.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
DEBROOT="${ACREETBOOT_DEB_ROOT:-$BUILD/debian}"
MIRROR="${ACREETBOOT_DEB_MIRROR:-https://deb.debian.org/debian}"
SUITE="${ACREETBOOT_DEB_SUITE:-trixie}"
mkdir -p "$DEBROOT"

command -v curl xz bsdtar sha256sum >/dev/null || { echo "FATAL: need curl, xz, bsdtar, sha256sum" >&2; exit 1; }

echo "==> resolving linux-image-amd64 (suite: $SUITE)"
IDX="$DEBROOT/Packages.xz"
if [ ! -s "$IDX" ]; then
  curl -fsSL -o "$IDX" "$MIRROR/dists/$SUITE/main/binary-amd64/Packages.xz" \
    || { echo "FATAL: index fetch failed" >&2; exit 1; }
fi

# Parse stanzas; ship only stable kernel images from the SIGNED pool
# (no -unsigned, -dbg, no meta packages). Highest version wins.
xz -dc "$IDX" | awk '
  /^$/            { if (pkg!="" && fn!="" && sha!="") print pkg "\t" fn "\t" sha;
                    pkg=""; fn=""; sha=""; next }
  /^Package: /    { pkg=substr($0,10) ; next }
  /^Filename: /   { fn=substr($0,11)  ; next }
  /^SHA256: /     { sha=substr($0,9)   ; next }
' > "$DEBROOT/candidates.tsv"

BEST="$(awk -F'\t' '
  $1 ~ /^linux-image-[0-9][^\t]*-amd64$/ &&
  $1 !~ /-rt-|-cloud/ &&
  $2 !~ /-unsigned/ && $2 !~ /-dbg/ && $2 !~ /-rt-/ && $2 !~ /-cloud/ &&
  $2 ~ /linux-image-.*amd64\.deb$/ { print }
' "$DEBROOT/candidates.tsv" | sort -t"	" -k1,1V | tail -1)"

if [ -z "$BEST" ]; then
  echo "FATAL: no linux-image-*-amd64 stanza found in $SUITE index" >&2
  exit 1
fi

PKG="$(echo "$BEST" | cut -f1)"
URL="$(echo "$BEST" | cut -f2)"
SHA_SAVED="$(echo "$BEST" | cut -f3)"
VERSION="$(basename "$URL" | sed 's/linux-image-//;s/_.*//')"
VERSION="$(basename "$URL" | sed 's/linux-image-//;s/_.*//')"
echo "==> picked: $PKG ($VERSION)"
echo "    url:    $MIRROR/$URL"

FULL_DEB="$DEBROOT/$(basename "$URL")"
NEED=1
if [ -f "$FULL_DEB" ] && [ -f "$DEBROOT/$VERSION.sha256" ]; then
  GOT="$(sha256sum "$FULL_DEB" | awk '{print $1}')"
  if [ "$GOT" = "$(cat "$DEBROOT/$VERSION.sha256")" ]; then NEED=0; fi
fi
if [ "$NEED" = 1 ]; then
  echo "==> downloading $(basename "$URL")"
  curl -fsSL -o "$FULL_DEB" "$MIRROR/$URL" || { echo "FATAL: deb fetch failed" >&2; rm -f "$FULL_DEB"; exit 1; }
  GOT="$(sha256sum "$FULL_DEB" | awk '{print $1}')"
  if [ "$GOT" != "$SHA_SAVED" ]; then
    echo "FATAL: sha256 mismatch against repo index!" >&2
    echo "  index:   $SHA_SAVED" >&2
    echo "  fetched: $GOT" >&2
    rm -f "$FULL_DEB"
    exit 1
  fi
  printf '%s\n' "$GOT" > "$DEBROOT/$VERSION.sha256"
fi
echo "ok: sha256 verified against index ($GOT)"

EXTRACT="$DEBROOT/extract"
if [ ! -s "$DEBROOT/vmlinuz" ]; then
  rm -rf "$EXTRACT" && mkdir -p "$EXTRACT"
  bsdtar -xf "$FULL_DEB" -C "$EXTRACT" --include="data.tar.*"
  DATA="$(find "$EXTRACT" -name "data.tar.*" | head -1)"
  [ -n "$DATA" ] || { echo "FATAL: no data.tar in deb" >&2; exit 1; }
  tar -xf "$DATA" -C "$EXTRACT"
  VK="$(find "$EXTRACT/boot" -name "vmlinuz-*" | head -1)"
  [ -n "$VK" ] || { echo "FATAL: vmlinuz not found in deb" >&2; exit 1; }
  cp -f "$VK" "$DEBROOT/vmlinuz"
fi
printf 'suite=%s\nversion=%s\nsha256=%s\nurl=%s\n' "$SUITE" "$VERSION" "$GOT" "$MIRROR/$URL" > "$DEBROOT/kernel.env"
echo "fetched: $DEBROOT/vmlinuz ($VERSION) — kernel.env written"
