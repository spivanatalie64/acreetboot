#!/usr/bin/env bash
# AcreetBoot — EDK2 bootstrap (milestone 0.2)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Fetches a PINNED edk2 tarball (codeload gzip of the tag), checksum-verifies
# it, extracts to build/edk2/edk2/, and builds BaseTools. Nothing lands in
# git — only this script and the pinned hash do.
#
# To bump the pin: update EDK2_TAG + EDK2_SHA256 below. The sha256 is of the
# codeload-generated tarball for the tag, recorded once, verified every run.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD/edk2"

EDK2_TAG="edk2-stable202608"
EDK2_SHA256="bac40812213576c3025cf780c33d882f9e087a44201b1cd28a07f6d1eb54b76a"
# Codeload tarballs do NOT carry git submodules; BaseTools needs the brotli
# submodule that the edk2 tag itself pins (gitlink e230f474…), so we fetch
# that pinned commit too.
BROTLI_SHA="e230f474b87134e8c6c85b630084c612057f253e"
BROTLI_SHA256="a9ba3940267de5dd73581a47c2e81b3eb1e1df6a704138c599020d66f3677a92"
# MdePkg.dec unconditionally includes the mipisyst submodule headers, so
# that pinned gitlink gets fetched too.
MIPI_SHA="370b5944c046bab043dd8b133727b2135af7747a"
MIPI_SHA256="9fda3b9a78343ab2be6f06ce6396536e7e065abac29b47c8eb2e42cbb4c4f00b"
MIPI_URL="https://codeload.github.com/MIPI-Alliance/public-mipi-sys-t/tar.gz/${MIPI_SHA}"
TARBALL="$BUILD/edk2-${EDK2_TAG}.tar.gz"
BROTLI_TARBALL="$BUILD/edk2/brotli-${BROTLI_SHA}.tar.gz"
URL="https://codeload.github.com/tianocore/edk2/tar.gz/refs/tags/${EDK2_TAG}"
BROTLI_URL="https://codeload.github.com/google/brotli/tar.gz/${BROTLI_SHA}"

if ! command -v sha256sum wget tar >/dev/null; then
  echo "FATAL: need sha256sum, wget, tar" >&2; exit 1
fi

echo "==> verifying pinned EDK2 tree (${EDK2_TAG})"
if [ ! -f "$TARBALL" ]; then
  echo "==> downloading ${URL}"
  wget -q -O "$TARBALL" "$URL" || { echo "FATAL: download failed" >&2; exit 1; }
fi

GOT="$(sha256sum "$TARBALL" | awk '{print $1}')"
if [ "$GOT" != "$EDK2_SHA256" ]; then
  echo "FATAL: checksum mismatch!" >&2
  echo "  expected ${EDK2_SHA256}" >&2
  echo "  got      ${GOT}" >&2
  rm -f "$TARBALL"
  exit 1
fi
echo "ok: sha256 ${GOT}"

SRCDIR="$BUILD/edk2/edk2-${EDK2_TAG}"
if [ ! -d "$SRCDIR" ]; then
  echo "==> extracting ${EDK2_TAG}"
  tar -xzf "$TARBALL" -C "$BUILD/edk2/"
fi
[ -d "$SRCDIR" ] || { echo "FATAL: extraction produced nothing at $SRCDIR" >&2; exit 1; }

echo "==> fetching pinned brotli submodule (${BROTLI_SHA})"
if [ ! -f "$BROTLI_TARBALL" ]; then
  wget -q -O "$BROTLI_TARBALL" "$BROTLI_URL" || { echo "FATAL: brotli download failed" >&2; rm -f "$BROTLI_TARBALL"; exit 1; }
fi
BGOT="$(sha256sum "$BROTLI_TARBALL" | awk '{print $1}')"
if [ "$BGOT" != "$BROTLI_SHA256" ]; then
  echo "FATAL: brotli checksum mismatch!" >&2
  echo "  expected ${BROTLI_SHA256}" >&2
  echo "  got      ${BGOT}" >&2
  rm -f "$BROTLI_TARBALL"
  exit 1
fi
echo "ok: brotli sha256 ${BGOT}"
BROTLI_DIR="$SRCDIR/BaseTools/Source/C/BrotliCompress/brotli"
if [ ! -f "$BROTLI_DIR/c/common/constants.h" ]; then
  rm -rf "$BROTLI_DIR"
  tar -xzf "$BROTLI_TARBALL" -C "$SRCDIR/BaseTools/Source/C/BrotliCompress/"
  mv "$SRCDIR/BaseTools/Source/C/BrotliCompress/brotli-${BROTLI_SHA}" "$BROTLI_DIR"
fi
[ -f "$BROTLI_DIR/c/common/constants.h" ] || { echo "FATAL: brotli tree is not in place" >&2; exit 1; }

echo "==> fetching pinned mipisyst submodule (${MIPI_SHA})"
MIPI_TARBALL="$BUILD/edk2/mipisyst-${MIPI_SHA}.tar.gz"
if [ ! -f "$MIPI_TARBALL" ]; then
  wget -q -O "$MIPI_TARBALL" "$MIPI_URL" || { echo "FATAL: mipisyst download failed" >&2; rm -f "$MIPI_TARBALL"; exit 1; }
fi
MGOT="$(sha256sum "$MIPI_TARBALL" | awk '{print $1}')"
if [ "$MGOT" != "$MIPI_SHA256" ]; then
  echo "FATAL: mipisyst checksum mismatch!" >&2
  echo "  expected ${MIPI_SHA256}" >&2
  echo "  got      ${MGOT}" >&2
  rm -f "$MIPI_TARBALL"
  exit 1
fi
echo "ok: mipisyst sha256 ${MGOT}"
MIPI_DIR="$SRCDIR/MdePkg/Library/MipiSysTLib/mipisyst"
if [ ! -f "$MIPI_DIR/library/include/mipi_syst/api.h" ]; then
  rm -rf "$MIPI_DIR"
  tar -xzf "$MIPI_TARBALL" -C "$SRCDIR/MdePkg/Library/MipiSysTLib/"
  mv "$SRCDIR/MdePkg/Library/MipiSysTLib/public-mipi-sys-t-${MIPI_SHA}" "$MIPI_DIR"
fi
[ -f "$MIPI_DIR/library/include/mipi_syst/api.h" ] || { echo "FATAL: mipisyst tree is not in place" >&2; exit 1; }

if [ ! -x "$SRCDIR/BaseTools/Source/BuildTool" ] || [ ! -f "$SRCDIR/BaseTools/Source/Python/build/build.py" ]; then
  echo "==> building EDK2 BaseTools"
  ( cd "$SRCDIR" && make -C BaseTools -j"$(nproc --ignore 2 2>/dev/null || echo 8)" )
fi

echo "==> exporting build env (edksetup.sh) — caller consumes in subshell via . ./build/edk2/edk2/edksetup.sh"
echo "bootstrap: OK (${EDK2_TAG} @ build/edk2/edk2)"
