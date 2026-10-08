# Sprint log — Boot Platform: systemd-boot → microboot → AcreetBoot
Date: 2026-10-07 · Owner: Natalie (Co-Lead) · License: BSD-3 (first-party, zero OpenCore code)

## What this sprint decided
AcreetionOS installed hosts move to **systemd-boot** as the primary bootloader
(config-level switch in Horizon; GRUB survives only as the legacy-BIOS
fallback). AcreetBoot — our configurable multi-OS boot orchestrator — builds
directly on that foundation. GRUB is out of the default chain entirely.

## Shipped this sprint

### Horizon (acreetionos-gnome), commit `ba4d893`
- Calamares `bootloader.conf` → `efiBootLoader: "systemd-boot"`. The vendored
  module already implements `install_systemd_boot` + `create_systemd_boot_conf`
  (`/etc/kernel/cmdline` + kernel-install), so the switch was clean.
- `grubcfg` module dropped from install sequences and removed.
- zswap parameters now travel in `bootloader.conf` `kernelParams`
  (→ flattened into `/etc/kernel/cmdline`); backup path writes `/etc/kernel/cmdline`.
- Recovery backend: `bootctl update/install` + kernel-install regeneration;
  TUI/GUI relabeled (`rebuild-grub` kept as CLI alias).
- Live ISO: UEFI bootmode → `uefi-x64.systemd-boot.esp`; ISO-profile GRUB tree
  removed; CI no longer needs a GRUB build-host dep; site text updated.
- Checks: 17/17 unittests OK, `bash -n` on all touched scripts, py_compile OK,
  `git diff --check` clean.

### AcreetBoot (new repo, `acreetboot`), commits `ed8979e…d4f6d1c`
```
UEFI firmware
  └─ systemd-boot (primary; loader.conf, editor off)
      └─ 00-acreetboot-microboot.conf (DEFAULT)
           microkernel + initramfs from the XBOOTLDR GPT partition
           labeled ACREETBOOT-ZSWAP
           (lockdown=confidentiality, sig_enforce, runtime zswap=zstd)
           └─ manifest-verify → write UEFI BootNext → warm reboot
                └─ /EFI/acreetboot/AcreetBootMainMenu.efi
                    └─ multi-OS picker (our BSD-3 re-implementation of the
                       OpenLinuxBoot/OpenCanopy *architecture*)
```
- `efi/acreetbootdrv/AcreetMainMenu.c` — UEFI boot manager skeleton.
- `c/bootstage.c` — hardened pid-1 microboot stage (RO payload mount).
- `c/setbootnext.c` — efivarfs BootNext writer; **unit-tested against a fake
  efivarfs** (BootNext bytes verified: attrs 0x7 + LE index).
- `sdboot/` — microboot default entry, recovery entry, fast-path chainload.
- `miniboot/kernel-config.frag` — kexec/hibernate off, efivarfs+zstd builtins.
- `initramfs/init` — manifest verify → BootNext → warm reboot; strict fail.
- `acreetbootctl` — `status` + dry-run `plan` CLI.
- Tests: 2/2 green, gcc `-Wall -Wextra` clean. Milestones tracked in
  `docs/plan/00-PLAN.md`.

## Licensing posture
- Everything first-party: BSD-3.
- OpenCore: architecture cloned, nothing vendored. Apple PSCL components
  (HFS/APFS) are fetch-on-use opt-in and OFF by default — the warning is
  documented on both sides.
- EDK II scaffold BSD-3/Apache-2; systemd-boot used as the distro binary.

## Next
- 0.2 EDK2 submodule + first bootable `AcreetBootMainMenu.efi`.
- 0.6 `acreetbootctl install` wiring sd-boot entries + NVRAM.
- 0.7 QEMU/bhyve end-to-end harness.
