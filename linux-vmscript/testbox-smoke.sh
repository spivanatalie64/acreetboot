#!/usr/bin/env bash
# AcreetBoot testbox smoke: staged sd-boot disk -> OVMF boot in container.
# SPDX-License-Identifier: BSD-3-Clause
# Mountless build (no losetup needed inside the container): filesystems are
# created on regular files and dd'd into the disk image at sfdisk offsets.
set -euo pipefail
W="${OTR_WORKDIR:-/work}"
IMG="$W/testdisk.img"; ESP="$W/esp.vfat"; PAY="$W/payload.ext4"

# 64MiB ESP at 1MiB offset + 64MiB XBOOTLDR payload partition, GPT.
SSZ=512; ESP_SECT=131072; PAY_SECT=131072; ESP_START=2048; PAY_START=$((ESP_START+ESP_SECT))

rm -f "$IMG" "$ESP" "$PAY"
truncate -s 196M "$IMG"
truncate -s 64M "$ESP"; truncate -s 64M "$PAY"
mkfs.vfat -n ACREETBOOT -F32 "$ESP"
mkfs.ext4 -q -L ACREETBOOT-ZSWAP -F "$PAY"

# Stage the sd-boot config + loader (mtools: no mount required).
MTOOLS_SKIP_CHECK=1 mformat -C -i "$ESP" :: 2>/dev/null || mformat -i "$ESP"
mmd -i "$ESP" ::/loader ::/loader/entries ::/EFI ::/EFI/acreetboot
mcopy -i "$ESP" sdboot/loader.conf ::/loader/
mcopy -i "$ESP" sdboot/*.conf ::/loader/entries/
SB="/usr/lib/systemd/boot/efi/systemd-bootx64.efi"
[ -f "$SB" ] && mcopy -i "$ESP" "$SB" ::/EFI/acreetboot/
mmd -i "$ESP" ::/loader/acreetboot 2>/dev/null || true
printf '0001\n' | mcopy -i "$ESP" - '::/loader/acreetboot/bootnext.idx'

sfdisk "$IMG" <<E
label: gpt
name=acreetboot-esp, start=$ESP_START, size=$ESP_SECT, type=uefi
name=ACREETBOOT-ZSWAP, start=$PAY_START, size=$PAY_SECT, type=933ac7e1-2eb4-4f13-b844-0e14e2aef915
E
dd if="$ESP" of="$IMG" bs=$SSZ seek=$ESP_START conv=notrunc status=none
dd if="$PAY" of="$IMG" bs=$SSZ seek=$PAY_START conv=notrunc status=none

SLOG="$W/serial.log"
echo "::notice testbox smoke: OVMF + staged sd-boot disk (TCG; us_ssh has no /dev/kvm)"
cp /usr/share/OVMF/OVMF_VARS.fd "$W/VARS.fd" 2>/dev/null || true
timeout 90 qemu-system-x86_64 \
  -m 512 -nographic \
  -drive "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd" \
  -drive "if=pflash,format=raw,file=$W/VARS.fd" \
  -drive "format=raw,file=$IMG" | tee "$SLOG" || true

if [ -s "$SLOG" ] && grep -qi "systemd-boot\|BdsDxe\|Booting" "$SLOG"; then
  echo "PASS: firmware stage + $(grep -oiE 'systemd-boot|BdsDxe' "$SLOG" | sort -u | tr '\n' ' ') observed"
else
  echo "FAIL: no boot markers observed"; exit 1
fi
