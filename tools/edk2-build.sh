#!/usr/bin/env bash
# AcreetBoot — EDK2 module build driver (milestone 0.2)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
#    tools/edk2-bootstrap.sh   (fetch + BaseTools, pinned)
# -> tools/edk2-build.sh        (this file: compile AcreetBootPkg, stage efi)
# -> test/test_ovmf_boot.sh     (QEMU/OVMF smoke, LoadImage+StartImage prob)
#
# Layout note: the AcreetBootPkg tree is symlinked into the vendored EDK2
# workspace (build/edk2/edk2-edk2-stable202608/AcreetBootPkg) because EDK2
# resolves DSC component paths workspace-relative. The symlink target is and
# stays efi/edk2/AcreetBootPkg in the repo — one source of truth.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

"$ROOT/tools/edk2-bootstrap.sh" > /dev/null 2>&1

EDK2ROOT="$BUILD/edk2/edk2-edk2-stable202608"
PKGREAL="$ROOT/efi/edk2/AcreetBootPkg"
SYMLINK="$EDK2ROOT/AcreetBootPkg"

# Refresh the symlink if missing or pointing somewhere else (idempotent).
if [ ! -L "$SYMLINK" ] || [ "$(readlink -f "$SYMLINK")" != "$PKGREAL" ]; then
  rm -f "$SYMLINK"
  ln -s "$PKGREAL" "$SYMLINK"
fi

# Toolchain contract (tools_def v4.01): the unified GCC tag is driven by
# these three env vars; empty = use plain `gcc`/host `make` from PATH.
export WORKSPACE="$EDK2ROOT"
export GCC_BIN=""
export GCC_HOST_BIN=""
export GCC_DLL=""

cd "$EDK2ROOT"
# edksetup.sh is not nounset-clean (PYTHON_COMMAND probed before defaulting);
# briefly drop -u around the source, still global scope so exports propagate.
set +u
. ./edksetup.sh > /dev/null 2>&1
set -u

build -t GCC -b RELEASE -a X64 \
  -p AcreetBootPkg/AcreetBootPkg.dsc

OUT="$EDK2ROOT/Build/AcreetBootPkg/RELEASE_GCC/X64"
mkdir -p "$BUILD/efi-output"
for mod in AcreetMainMenu AcreetBootStub; do
  EFI_PATH="$(find "$OUT/AcreetBootPkg/$mod" -name "${mod}.efi" | head -1)"
  [ -n "$EFI_PATH" ] && [ -f "$EFI_PATH" ] || { echo "FATAL: $mod.efi not produced" >&2; exit 1; }
  cp -f "$EFI_PATH" "$BUILD/efi-output/"
done

echo "staged:"
ls -la "$BUILD/efi-output/"
echo "edk2-build: OK"
