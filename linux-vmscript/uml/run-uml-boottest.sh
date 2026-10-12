#!/usr/bin/env bash
# AcreetBoot UML boot test — verifies the FULL microboot init flow (no VT-x):
#   payload found via cmdline override -> no-manifest advisory -> setbootnext
#   writes BootNext onto an emulated efivars dir ON THE rw ROOTFS -> warm
#   poweroff; the host then verifies the exact var bytes from the image.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
W="${CVM_UML_WORK:-/srv/cvm/uml}"
BIN="${1:-$W/uml-microboot/uml-microboot}"
LOG="${2:-$W/umd-boottest.log}"
VARNAME="BootNext-8be4df61-93ea-11d0-8200-00c04fd430c8"

timeout 75 "$BIN" \
    "ubd0=$W/probe-root.img" \
    "ubd1=$W/payload.img" \
    root=/dev/ubda rw init=/init mem=128M \
    acreetboot.payload=/dev/ubdb \
    acreetboot.efivars=/mnt-efivars \
    </dev/null >"$LOG" 2>&1 || true

echo "== boot log tail =="
tail -12 "$LOG"
if grep -q "BootNext -> Boot0002; warm rebooting" "$LOG"; then
  echo "PASS(log): full microboot chain ran through BootNext under UML"
else
  echo "FAIL: BootNext marker missing"; exit 1
fi

debugfs -R "dump /mnt-efivars/$VARNAME $W/var.out" "$W/probe-root.img" >/dev/null 2>&1
hex="$(od -A n -t x1 -v "$W/var.out" 2>/dev/null | tr -d ' \n')"
if [ "$hex" = "070000000200" ]; then
  echo "PASS(bytes): BootNext var content verified on-disk (attrs 0x7, idx 0x0002)"
else
  echo "FAIL: unexpected BootNext bytes: '$hex'"; exit 1
fi
