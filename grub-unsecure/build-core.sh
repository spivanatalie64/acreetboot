#!/usr/bin/env bash
# Compile-flag gate: refuses to build unsecure unless user edited ACCEPT.gate
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
GATE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/ACCEPT.gate"
SENTINEL="I-ACCEPT-THE-UNSECURE-ACREETBOOT-CHAIN"

grep -qs "$SENTINEL" "$GATE" || {
  echo "GATE CLOSED — you have NOT accepted the unsecure chain." >&2
  echo "To accept explicitly:" >&2
  echo "  echo \"$SENTINEL\" >> grub-unsecure/ACCEPT.gate" >&2
  exit 1
}
[ -n "${GRUB_SRC:-}" ] && [ -d "$GRUB_SRC/grub-core" ] || {
  echo "GRUB_SRC not set — this is a BYO-COMPILE variant." >&2
  echo "e.g.: git clone https://git.savannah.gnu.org/git/grub.git && export GRUB_SRC=\$PWD" >&2
  exit 2
}
command -v grub-mkstandalone >/dev/null || { echo "need grub package" >&2; exit 3; }

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/grub-unsecure"
grub-mkstandalone -O x86_64-efi \
  --modules="all_video boot btrfs cat chain configfile echo efifwsetup exfat ext2 f2fs fat font linux search search_file search_label search_fs_uuid test xfs" \
  -o "$ROOT/build/grub-unsecure/core.efi" \
  "boot/grub/grub.cfg=$ROOT/grub-unsecure/grub.cfg.unsecure"
echo "built: build/grub-unsecure/core.efi  (UNSECURE variant, user-compiled)"
