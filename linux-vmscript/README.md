# linux-vmscript — QEMU/OVMF test harness (Linux side of the VM matrix)

Milestone 0.7 (docs/plan/00-PLAN.md): end-to-end boot-chain testing.
FreeBSD-vmscript/ covers bhyve; this tree covers QEMU with OVMF/edk2
firmware — matching acreetboot's real-world firmware target.

What `run-vm.sh` does:
  1. builds a test disk image: ESP (FAT32) + XBOOTLDR partition
     labeled ACREETBOOT-ZSWAP holding the microboot payload;
  2. stages sd-boot + loader entries from ../sdboot;
  3. boots the VM headless (serial console) with OVMF;
  4. asserts "acreetboot:" markers in the serial log.

Host needs: qemu-system-x86_64, edk2-ovmf, sfdisk, systemd (for sd-boot
binary). Without them the script errors honestly — no fake green.
