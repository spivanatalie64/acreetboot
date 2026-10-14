#!/usr/bin/env bash
# AcreetBoot — OVMF smoke test for milestone 0.2 (issue #1)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Proves the full LoadImage + StartImage hand-off:
#   OVMF firmware -> boots \EFI\BOOT\BOOTX64.EFI = AcreetMainMenu.efi
#                 -> menu resolves hardcoded payload \acreetboot\boot\vmtest.efi
#                 -> LoadImage + StartImage = AcreetBootStub.efi
#                 -> stub prints ACREET-PAYLOAD-OK on the serial log.
#
# Honestness: TCG software emulation — no VT-x needed, real-time performance
# is irrelevant for a hand-off-proof test. Timeouts keep it bounded.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
STAGE="$BUILD/ovmf-stage"

command -v qemu-system-x86_64 mformat mcopy cp >/dev/null || { echo "FATAL: qemu/mtools missing" >&2; exit 1; }
OVMF_CODE="/usr/share/OVMF/x64/OVMF_CODE.4m.fd"
OVMF_VARS="/usr/share/OVMF/x64/OVMF_VARS.4m.fd"
[ -f "$OVMF_CODE" ] || { echo "FATAL: $OVMF_CODE not found (install ovmf)" >&2; exit 1; }

"$ROOT/tools/edk2-build.sh" > /dev/null || { echo "FATAL: edk2 build failed" >&2; exit 1; }

rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -f "$OVMF_VARS" "$STAGE/VARS.4m.fd"
cp -f "$BUILD/efi-output/AcreetMainMenu.efi" "$STAGE/BOOTX64.EFI"
cp -f "$BUILD/efi-output/AcreetBootStub.efi" "$STAGE/vmtest.efi"

# 16 MiB FAT ESP staged with mtools (no mount loop needed, no root).
IMG="$STAGE/esp.img"
dd if=/dev/zero of="$IMG" bs=1M count=16 status=none
mformat -i "$IMG" -C -T 16000
mmd -i "$IMG" ::/acreetboot ::/acreetboot/boot ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$STAGE/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$STAGE/vmtest.efi"     ::/acreetboot/boot/vmtest.efi
mtype -i "$IMG" ::/acreetboot/boot/vmtest.efi > /dev/null || { echo "FATAL: staging failed" >&2; exit 1; }

SERIAL="$STAGE/serial.log"
timeout 60 qemu-system-x86_64 \
  -machine q35 -m 128 -nographic \
  -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,file="$STAGE/VARS.4m.fd" \
  -drive if=none,file="$IMG",format=raw,id=esp \
  -device virtio-scsi-pci,id=scsi \
  -device scsi-hd,drive=esp,bootindex=1 \
  -serial file:"$SERIAL" \
  < /dev/null > /dev/null 2>&1 || true

echo "---- serial log (AcreetBoot lines) ----"
grep -aE "AcreetBoot|ACREET" "$SERIAL" | head -20 || { echo "NO AcreetBoot output on serial"; exit 1; }
echo "---------------------------------------"

grep -aq "ACREET-PAYLOAD-OK" "$SERIAL" && echo "OVMF SMOKE: GREEN — LoadImage+StartImage hand-off proven" || {
  echo "OVMF SMOKE: RED — payload marker missing (menu reached, hand-off did not start)"
  tail -40 "$SERIAL"
  exit 1
}
