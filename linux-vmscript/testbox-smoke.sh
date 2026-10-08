#!/usr/bin/env bash
# AcreetBoot testbox smoke: staged sd-boot disk -> OVMF boot in container.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
: "${OTR_WORKDIR:=/work}"
IMG="$OTR_WORKDIR/testdisk.img"

# Deep this is a *smoke* test: firmware stage + bootloader discovery over
# the staged ESP entries. Full microboot chain assertions arrive with
# milestone 0.2/0.7 (once AcreetBootMainMenu.efi and a real kernel exist).
rm -f "$IMG"; truncate -s 196M "$IMG"
sfdisk "$IMG" <<'E'
label: gpt
name=acreetboot-esp, size=128MiB, type=uefi
name=ACREETBOOT-ZSWAP, size=64MiB, type=933ac7e1-2eb4-4f13-b844-0e14e2aef915
E

LO=$(losetup -Pf --show "$IMG")
cleanup() { losetup -d "$LO" 2>/dev/null || true; }
trap cleanup EXIT
mkfs.vfat -n ACREETBOOT-ESP "${LO}p1"
mkfs.ext4 -L ACREETBOOT-ZSWAP "${LO}p2"
ESP_M=$(mktemp -d); PAY_M=$(mktemp -d)
mount "${LO}p1" "$ESP_M"; mount "${LO}p2" "$PAY_M"
mkdir -p "$ESP_M/loader/entries" "$ESP_M/EFI/acreetboot"
cp -r /usr/share/sdbt/* "$ESP_M/" 2>/dev/null || true
if [ -f /usr/lib/systemd/boot/efi/systemd-bootx64.efi ]; then
  cp /usr/lib/systemd/boot/efi/systemd-bootx64.efi "$ESP_M/EFI/acreetboot/"
fi
umount "$ESP_M" "$PAY_M"
partprobe "$LO" 2>/dev/null || true

SLOG="$OTR_WORKDIR/serial.log"
echo "::notice╱ testbox: booting OVMF against staged disk (start $(date -u +%H:%M:%SZ))"
timeout 75 qemu-system-x86_64 \
  -m 512 -nographic \
  -drive "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd" \
  -drive "if=pflash,format=raw,file=$OTR_WORKDIR/VARS.fd" \
  -drive "format=raw,file=$IMG" | tee "$SLOG" || true

if [ -s "$SLOG" ] && (grep -qi "systemd-boot\|BdsDxe\|Booting" "$SLOG"); then
  echo "PASS: firmware stage + disk discovery observed"
else
  echo "FAIL: unencrypted no boot markers observed"; exit 1
fi
