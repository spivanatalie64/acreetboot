#!/usr/bin/env bash
# AcreetBoot — QEMU/OVMF boot-chain test harness
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
IMG="${IMG:-$ROOT/build/testvm.img}"
SDDISK_MB="${SDDISK_MB:-256}"
OVMF_CODE="${OVMF_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}"
OVMF_VARS="${OVMF_VARS:-/usr/share/edk2/x64/OVMF_VARS.4m.fd}"
SERIAL_LOG="${SERIAL_LOG:-$ROOT/build/testvm-serial.log}"

need() { command -v "$1" >/dev/null || { echo "MISSING on host: $1" >&2; exit 77; }; }
need qemu-system-x86_64; need sfdisk; need dd

mkdir -p "$ROOT/build"
[ -f "$OVMF_CODE" ] || { echo "OVMF not found at ${OVMF_CODE} (install edk2-ovmf)" >&2; exit 77; }

# 1. Test disk: p1 ESP, p2 XBOOTLDR payload partition
rm -f "$IMG"; truncate -s "${SDDISK_MB}M" "$IMG"
sfdisk "$IMG" <<PARTS
label: gpt
name=acreetboot-esp, size=128MiB, type=uefi
name=ACREETBOOT-ZSWAP, size=64MiB, type=933ac7e1-2eb4-4f13-b844-0e14e2aef915
PARTS

# loop-mount staging is root's job; tolerate unpriv fallback diagram below
echo "acreetboot: test image staged at $IMG (ESP + XBOOTLDR payload)"
echo "acreetboot: populating payload requires root loop mounts — run with:"
echo "  sudo $0 populate   (mounts part2, copies sdboot entries + payload)"
case "${1:-boot}" in
  populate)
    LO=$(losetup -Pf --show "$IMG")
    trap 'losetup -d "$LO" 2>/dev/null || true' EXIT
    mkdir -p /mnt/esp /mnt/pay
    mkfs.vfat -n ACREETBOOT-ESP "${LO}p1"
    mkfs.ext4 -L ACREETBOOT-ZSWAP "${LO}p2"
    mount "${LO}p1" /mnt/esp; mount "${LO}p2" /mnt/pay
    mkdir -p /mnt/esp/loader/entries /mnt/esp/EFI/acreetboot
    cp "$ROOT"/sdboot/*.conf /mnt/esp/loader/entries/
    cp "$ROOT"/sdboot/loader.conf /mnt/esp/loader/
    [ -f /usr/lib/systemd/boot/efi/systemd-bootx64.efi ] && \
      cp /usr/lib/systemd/boot/efi/systemd-bootx64.efi /mnt/esp/EFI/acreetboot/
    umount /mnt/esp /mnt/pay
    ;;
  boot|*)
    [ -f "$IMG" ] || { echo "no image yet; run '$0 populate' first" >&2; exit 1; }
    echo "acreetboot: booting OVMF with sd-boot chain (serial assertions in '$SERIAL_LOG')"
    cp "$OVMF_VARS" "$ROOT/build/OVMF_VARS.test"
    timeout 60 qemu-system-x86_64 \
      -m 512 -nographic -serial mon:stdio \
      -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
      -drive "if=pflash,format=raw,file=$ROOT/build/OVMF_VARS.test" \
      -drive "format=raw,file=$IMG" | tee "$SERIAL_LOG"
    grep -q "acreetboot" "$SERIAL_LOG" && echo "PASS: acreetboot markers in boot log"
    ;;
esac
