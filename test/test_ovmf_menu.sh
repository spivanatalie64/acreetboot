#!/usr/bin/env bash
# AcreetBoot — OVMF menu test for feature/efi-menu (milestone 0.3)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Proves the text TUI menu lives (systemd-boot clone) end-to-end:
#   OVMF -> \EFI\BOOT\BOOTX64.EFI = AcreetMainMenu.efi
#        -> scanner parses \loader\entries\*.conf (2 rows on stage)
#        -> TUI renders banner + row titles + count on serial
#        -> countdown (10s) auto-boots the '*' DEFAULT row
#        -> LoadImage + StartImage = AcreetBootStub.efi -> ACREET-PAYLOAD-OK
#
# Honesty: TCG again, one full-screen repaint; the serial log is the proof.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
STAGE="$BUILD/ovmf-menu-stage"

command -v qemu-system-x86_64 mformat mcopy mmd cp >/dev/null || { echo "FATAL: qemu/mtools missing" >&2; exit 1; }
OVMF_CODE="/usr/share/OVMF/x64/OVMF_CODE.4m.fd"
OVMF_VARS="/usr/share/OVMF/x64/OVMF_VARS.4m.fd"
[ -f "$OVMF_CODE" ] || { echo "FATAL: $OVMF_CODE not found (install ovmf)" >&2; exit 1; }

"$ROOT/tools/edk2-build.sh" > /dev/null || { echo "FATAL: edk2 build failed" >&2; exit 1; }

rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -f "$OVMF_VARS" "$STAGE/VARS.4m.fd"
cp -f "$BUILD/efi-output/AcreetMainMenu.efi" "$STAGE/BOOTX64.EFI"
cp -f "$BUILD/efi-output/AcreetBootStub.efi" "$STAGE/vmtest.efi"

IMG="$STAGE/esp.img"
ESP_MB=32
dd if=/dev/zero of="$IMG" bs=1M count="$ESP_MB" status=none
mformat -i "$IMG" -C -T $((ESP_MB * 1000))
mmd -i "$IMG" ::/acreetboot ::/acreetboot/boot ::/loader ::/loader/entries ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$STAGE/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$STAGE/vmtest.efi"    ::/acreetboot/boot/vmtest.efi

# Two staged loader entries: default row (Debian, boots our real pipeline
# payload = the stub) + a second .efi row (proves efi= parsing works).
cat > "$STAGE/01-debian.conf" <<'EOF'
title=Debian GNU/Linux trixie
version=13.1-akernel-1
linux=acreetboot/boot/vmtest.efi
options=console=ttyS0
EOF
cat > "$STAGE/02-efi-chainload.conf" <<'EOF'
title=EFI chainload row (missing payload by design)
efi=loader/chainload/missing.efi
EOF
mcopy -i "$IMG" "$STAGE/01-debian.conf"       ::/loader/entries/01-debian.conf
mcopy -i "$IMG" "$STAGE/02-efi-chainload.conf" ::/loader/entries/02-efi-chainload.conf

SERIAL="$STAGE/serial.log"
timeout 90 qemu-system-x86_64 \
  -machine q35 -m 128 -nographic \
  -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,file="$STAGE/VARS.4m.fd" \
  -drive if=none,file="$IMG",format=raw,id=esp \
  -device virtio-scsi-pci,id=scsi \
  -device scsi-hd,drive=esp,bootindex=1 \
  -serial file:"$SERIAL" \
  < /dev/null > /dev/null 2>&1 || true

echo "---- serial menu assertions ----"
fail=0
grep -aq "AcreetBoot 0.3.0 (main menu, BSD-3)" "$SERIAL" \
  || { echo "MISS: menu banner";  fail=1; }
grep -aq "Debian GNU/Linux trixie"  "$SERIAL" \
  || { echo "MISS: title= row ('Debian GNU/Linux trixie')"; fail=1; }
grep -aq "EFI chainload row"        "$SERIAL" \
  || { echo "MISS: efi= row title"; fail=1; }
grep -aq "Count: 2 entries"         "$SERIAL" \
  || { echo "MISS: entry count row"; fail=1; }
grep -aq "Picked: Debian GNU/Linux trixie" "$SERIAL" \
  || { echo "MISS: countdown auto-picked the '*' default row"; fail=1; }
grep -aq "ACREET-PAYLOAD-OK"        "$SERIAL" \
  || { echo "MISS: hand-off marker"; fail=1; }
echo "-----------------------------------"

if [ "$fail" -eq 1 ]; then
  echo "OVMF MENU: RED — assertions incomplete"
  grep -aA2 -B2 "Acreet" "$SERIAL" | head -60 || tail -60 "$SERIAL"
  exit 1
fi
echo "OVMF MENU: GREEN — scanner + text TUI (feature/efi-menu) proven end-to-end"
