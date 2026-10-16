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

# Payload policy: real UKI when staged (acr-* GKI stage lane or debian-*
# install lane), two-line stub otherwise. Hand-off proof is the same either
# way; the GKI lane also runs OUR initramfs /init inside the kernel.
UKI_EFI="$(ls -v "$BUILD/ukistage/EFI/Linux/"acr-*.efi "$BUILD/ukistage/EFI/Linux/"debian-*[0-9].efi 2>/dev/null | tail -n 1 || true)"
if [ -n "$UKI_EFI" ]; then
  cp -f "$UKI_EFI" "$STAGE/vmtest.efi"
  PAYLOAD_KIND="UKI ($(basename "$UKI_EFI"))"
else
  cp -f "$BUILD/efi-output/AcreetBootStub.efi" "$STAGE/vmtest.efi"
  PAYLOAD_KIND="stub"
fi

# ESP sized for the payload: the kernel UKI lane needs ~192 MiB; the stub
# lane stays happy on 16 MiB.
if [ -n "${UKI_EFI:-}" ]; then
  ESP_MB=256
else
  ESP_MB=16
fi
IMG="$STAGE/esp.img"
dd if=/dev/zero of="$IMG" bs=1M count="$ESP_MB" status=none
mformat -i "$IMG" -C -T $((ESP_MB * 1000))
mmd -i "$IMG" ::/acreetboot ::/acreetboot/boot ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$STAGE/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$STAGE/vmtest.efi"     ::/acreetboot/boot/vmtest.efi
mtype -i "$IMG" ::/acreetboot/boot/vmtest.efi > /dev/null || { echo "FATAL: staging failed" >&2; exit 1; }

SERIAL="$STAGE/serial.log"
QEMU_EXTRA=()
if [ -n "${UKI_EFI:-}" ]; then
  # Kernel lane: not enough stub chatter to care; exit after reboot so we
  # don't spin in a panic=-1 reboot loop the full 60s.
  QEMU_EXTRA=(-no-reboot -m 256)
fi
timeout 90 qemu-system-x86_64 \
  -machine q35 -m ${QEMU_MEM:-128} -nographic \
  ${QEMU_EXTRA[@]+"${QEMU_EXTRA[@]}"} \
  -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,file="$STAGE/VARS.4m.fd" \
  -drive if=none,file="$IMG",format=raw,id=esp \
  -device virtio-scsi-pci,id=scsi \
  -device scsi-hd,drive=esp,bootindex=1 \
  -serial file:"$SERIAL" \
  < /dev/null > /dev/null 2>&1 || true

echo "---- serial log (payload: $PAYLOAD_KIND) ----"
grep -aE "AcreetBoot|ACREET" "$SERIAL" | head -20 || { echo "NO AcreetBoot output on serial"; exit 1; }
echo "---------------------------------------"

if [ -n "${UKI_EFI:-}" ]; then
  if grep -aq "Linux version" "$SERIAL"; then
    if grep -aq "acreet-boot" "$SERIAL"; then
      echo "OVMF SMOKE: GREEN — kernel + OUR initramfs /init both ran ($PAYLOAD_KIND)"
    else
      echo "OVMF SMOKE: GREEN — real-kernel hand-off proven ($PAYLOAD_KIND; kernel spoke, acreetboot /init banner absent — check initramfs lane)"
    fi
    exit 0
  else
    echo "OVMF SMOKE: RED — UKI staged but kernel never spoke on serial"
    tail -40 "$SERIAL"
    exit 1
  fi
fi

grep -aq "ACREET-PAYLOAD-OK" "$SERIAL" && echo "OVMF SMOKE: GREEN — LoadImage+StartImage hand-off proven" || {
  echo "OVMF SMOKE: RED — payload marker missing (menu reached, hand-off did not start)"
  tail -40 "$SERIAL"
  exit 1
}
