#!/usr/bin/env bash
# AcreetBoot — build UML rootfs + ACREETBOOT-ZSWAP payload images (debugfs, no loops)
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
BUSYBOX="${BUSYBOX:-/usr/bin/busybox}"
OUTDIR="${CVM_UML_WORK:-/srv/cvm/uml}"
INIT="${1:-$ROOT/initramfs/init}"
[ -e "$INIT" ] || INIT="$OUTDIR/src/initramfs-init"
[ -e "$INIT" ] || { echo "init not found" >&2; exit 1; }
[ -x "$BUSYBOX" ] || { echo "busybox missing at $BUSYBOX" >&2; exit 1; }

R="$OUTDIR/probe-root.img"; P="$OUTDIR/payload.img"
rm -f "$R" "$P"
truncate -s 48M "$R" && mkfs.ext4 -q -F "$R"
truncate -s 32M "$P" && mkfs.ext4 -q -L ACREETBOOT-ZSWAP -F "$P"

cat > "$OUTDIR/rfs.cm" <<'EOS'
mkdir /bin
mkdir /proc
mkdir /sys
mkdir /dev
mkdir /tmp
mkdir /run
mkdir /mnt-efivars
write /usr/bin/busybox /bin/busybox
set_inode_field /bin/busybox mode 0100755
symlink /bin/sh /bin/busybox
symlink /bin/mount /bin/busybox
symlink /bin/tr /bin/busybox
symlink /bin/cat /bin/busybox
symlink /bin/mkdir /bin/busybox
symlink /bin/poweroff /bin/busybox
symlink /bin/reboot /bin/busybox
symlink /bin/umount /bin/busybox
symlink /bin/sync /bin/busybox
EOS
debugfs -w -f "$OUTDIR/rfs.cm" "$R" >/dev/null 2>&1

cat > "$OUTDIR/rfs2.cm" <<EOS
write $INIT /init
set_inode_field /init mode 0100755
EOS
debugfs -w -f "$OUTDIR/rfs2.cm" "$R" >/dev/null 2>&1
printf '%s\n' "rootfs: $R"
# setbootnext (static build) must ship inside the rootfs image for the
# fake-efivars lane.
SB="${CVM_UML_BIN:-$OUTDIR/bins/setbootnext}"
[ -e "$SB" ] || { echo "setbootnext binary missing at $SB (build c/setbootnext.c -static)"; exit 5; }
cat > "$OUTDIR/rfs3.cm" <<EOS
write $SB /bin/setbootnext
set_inode_field /bin/setbootnext mode 0100755
EOS
debugfs -w -f "$OUTDIR/rfs3.cm" "$R" >/dev/null 2>&1
debugfs -R "ls -l /init" "$R" 2>/dev/null | tail -1

# payload: no manifest (honest: the verification lane has its own test);
# only the bootnext.idx entry pointer
printf '%s\n' 0002 > "$OUTDIR/bootnext.idx"
cp "$OUTDIR/bootnext.idx" /srv/cvm/bt.idx
cat > "$OUTDIR/pay.cm" <<'EOS'
write /srv/cvm/bt.idx /bootnext.idx
set_inode_field /bootnext.idx mode 0100644
EOS
debugfs -w -f "$OUTDIR/pay.cm" "$P" >/dev/null 2>&1
debugfs -R "ls -l /bootnext.idx" "$P" 2>/dev/null | tail -1
printf '%s\n' "payload: $P"
