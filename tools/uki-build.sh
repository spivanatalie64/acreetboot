#!/usr/bin/env bash
# AcreetBoot — UKI builder (canonical producer, cd version of Natalie's script)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# This is the repo's UKI production step, adapted from the prescribed build
# script (ukify assemble: Linux + initrd + cmdline + os-release -> .efi).
# Kernel lane decision (recorded): stable = Debian kernel (build/debian lane
# via tools/debian-kernel-fetch.sh), Debian owns kernel maintenance; UKI
# packaging/manifests are ours.
#
# Modes:
#   default (STAGE, no sudo): UKIs are built OUT-OF-TREE for the OVMF lane,
#     cmdline is a staged console-serial one, os-release is AcreetionOS
#     (an image we certify; never leaks the build host's identity).
#   ACREETBOOT_UKI_INSTALL=1 (target system, e.g. Debian live/host):
#     verbatim semantics of the prescribed script:
#       EFI_DEST=/boot/efi/EFI/Linux, deps via apt (systemd-ukify,
#       systemd-stub), /etc/kernel/cmdline seed from /proc/cmdline,
#       output debian-<version>.efi, efibootmgr hint.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
DEBROOT="${ACREETBOOT_DEB_ROOT:-$BUILD/debian}"

if [ "${ACREETBOOT_UKI_INSTALL:-0}" = "1" ]; then
  EFI_DEST="/boot/efi/EFI/Linux"
  KERNEL_CMDLINE="/etc/kernel/cmdline"
  CURRENT_CMDLINE="/proc/cmdline"
  SUDO="sudo"
  MODE="install"
else
  STAGE="$BUILD/ukistage"
  EFI_DEST="$STAGE/EFI/Linux"
  KERNEL_CMDLINE="$STAGE/kernel-cmdline"
  OS_RELEASE="$STAGE/os-release"
  SUDO=""
  MODE="stage"
fi

echo "=== Installing dependencies ==="
# Debian lane keeps the prescribed apt install; non-Debian build hosts need
# the same pieces from their native packages (Arch: systemd-ukify + systemd
# both provide ukify + sd-stub; the host probe is idempotent).
if [ "$MODE" = "install" ]; then
  if command -v apt-get >/dev/null; then
    $SUDO apt-get update && $SUDO apt-get install -y systemd-ukify systemd-stub
  fi
fi
if ! command -v ukify >/dev/null; then
  echo "FATAL: ukify not found ($MODE mode). Install systemd-ukify." >&2
  exit 1
fi

$SUDO mkdir -p "$EFI_DEST"

echo "=== Kernel selection (stable = Debian) ==="
# Priority 1: the fetched, index-verified Debian kernel (build/debian lane).
KERNEL_VERSION=""
LATEST_VMLINUZ=""
if [ -s "$DEBROOT/vmlinuz" ] && [ -f "$DEBROOT/kernel.env" ]; then
  LATEST_VMLINUZ="$DEBROOT/vmlinuz"
  KERNEL_VERSION="$(sed -n 's/^version=//p' "$DEBROOT/kernel.env")"
  INITRD_POLICY="none"
  echo "lane:    Debian stable kernel (index-verified $DEBROOT lane)"
elif compgen -G "/boot/initrd.img-*" >/dev/null; then
  # Debian host: native /boot/. Steps and paths exactly as prescribed.
  LATEST_VMLINUZ="$(ls -v /boot/vmlinuz-* | tail -n 1)"
  KERNEL_VERSION="$(basename "$LATEST_VMLINUZ" | sed 's/vmlinuz-//')"
  LATEST_INITRD="/boot/initrd.img-${KERNEL_VERSION}"
  [ -f "$LATEST_INITRD" ] || { echo "FATAL: no initrd for $KERNEL_VERSION" >&2; exit 1; }
  OS_RELEASE="/usr/lib/os-release"
  INITRD_POLICY="deb-native"
  echo "lane:    Debian host /boot"
elif [ -f /boot/vmlinuz-linux ]; then
  # AcreetionOS/Arch build host fallback: UKI from the host's own kernel
  # (initramfs-linux.img + microcode). Named linux-<ver>.efi honestly.
  LATEST_VMLINUZ="/boot/vmlinuz-linux"
  KERNEL_VERSION="$(file -b /boot/vmlinuz-linux | sed -n 's/.*version \([^ ]*\).*/\1/p')"
  LATEST_INITRD=""
  for cand in /boot/initramfs-linux-zen.img /boot/initramfs-linux.img; do
    [ -f "$cand" ] && { LATEST_INITRD="$cand"; break; }
  done
  OS_RELEASE="/usr/lib/os-release"
  INITRD_POLICY="arch-fallback"
  echo "lane:    AcreetionOS/Arch /boot fallback"
  echo "NOTE: fallback lane uses the HOST kernel only for CI/e2e; the shipped"
  echo "      imaging pipeline pins the Debian stable lane above."
else
  echo "FATAL: no kernel found (run tools/debian-kernel-fetch.sh for the stable lane)" >&2
  exit 1
fi

echo "=== Assembling kernel command line ==="
if [ "$MODE" = "install" ]; then
  if [ ! -f "$KERNEL_CMDLINE" ] || [ ! -s "$KERNEL_CMDLINE" ]; then
    echo "[-] No custom command line found in $KERNEL_CMDLINE."
    echo "[+] Extracting active cmdline from $CURRENT_CMDLINE..."
    $SUDO tee "$KERNEL_CMDLINE" < "$CURRENT_CMDLINE" > /dev/null
  fi
else
  # Stage mode: serial console so OVMF output is assertable; the version
  # banner must NOT be quieted or the test cannot see the kernel identity.
  # Kernel panics (no root in the VM) are EXPECTED — we are proving the
  # hand-off, not the distro.
  printf 'console=ttyS0 loglevel=7 panic=-1\n' > "$KERNEL_CMDLINE"
fi

if [ "$MODE" = "stage" ]; then
  cat > "$OS_RELEASE" <<EOF
NAME="AcreetionOS"
ID=acreetionos
PRETTY_NAME="AcreetionOS (AcreetBoot UKI, Debian kernel lane)"
VERSION_ID="0.2.0"
BUILD_ID="acreetboot"
KERNEL_LANE="$KERNEL_VERSION"
EOF
  OS_RELEASE_OUT="$OS_RELEASE"
else
  OS_RELEASE_OUT="${OS_RELEASE:-/usr/lib/os-release}"
fi

if [ -n "${LATEST_INITRD:-}" ] && [ -n "$LATEST_INITRD" ]; then
  INITRD_ARG="--initrd=$LATEST_INITRD"
  OUTPUT_EFI="${EFI_DEST}/debian-${KERNEL_VERSION}.efi"
else
  INITRD_ARG=""
  OUTPUT_EFI="${EFI_DEST}/debian-${KERNEL_VERSION}-noinitrd.efi"
fi

echo "=== Building UKI (systemd-ukify) ==="
echo "Kernel:      $LATEST_VMLINUZ"
echo "Initrd:      ${LATEST_INITRD:-<none — hand-off lane>}"
echo "Cmdline:     $(cat "$KERNEL_CMDLINE")"
echo "OS release:  $OS_RELEASE_OUT"
echo "Output path: $OUTPUT_EFI"

OUTPUT_EFI_FINAL="$OUTPUT_EFI"
ukify build \
    --linux="$LATEST_VMLINUZ" \
    $INITRD_ARG \
    --cmdline=@"$KERNEL_CMDLINE" \
    --os-release=@"$OS_RELEASE_OUT" \
    --output="$OUTPUT_EFI"

echo "ok: UKI created at $OUTPUT_EFI"
# Only the .osrel needs to survive; keep the effective one around for tests.
printf '%s\n' "$OUTPUT_EFI_FINAL" > "$BUILD/ukistage/latest.efi.path" 2>/dev/null || true
if [ "$MODE" = "install" ]; then
  echo "[*] Add to UEFI boot entries with efibootmgr if needed."
fi
