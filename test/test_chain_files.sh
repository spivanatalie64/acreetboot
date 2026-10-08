#!/usr/bin/env bash
# Structural test: all boot-chain config files exist and carry correct marks.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
for f in grub/grub.cfg sdboot/loader.conf sdboot/10-acreetboot.conf \
         miniboot/kernel-config.frag initramfs/init \
         efi/acreetbootdrv/AcreetMainMenu.c c/bootstage.c c/setbootnext.c \
         acreetbootctl/acreetbootctl.py docs/plan/00-PLAN.md; do
  [ -s "$root/$f" ] || { echo "MISSING/EMPTY: $f"; exit 1; }
done
grep -q "ACREETBOOT-ZSWAP" "$root/grub/grub.cfg"
grep -q "lockdown=confidentiality" "$root/grub/grub.cfg"
grep -q "chainloader" "$root/grub/grub.cfg"
grep -q "AcreetBootMainMenu.efi" "$root/sdboot/10-acreetboot.conf"
echo "chain files present"
