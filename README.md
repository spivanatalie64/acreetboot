# AcreetBoot

AcreetBoot is AcreetionOS's configurable multi-operating-system boot
orchestrator. It is **GRUB-based**, uses **systemd-boot** as the direct
chainloader, and runs its own **BSD-3** UEFI boot manager (`AcreetBootMainMenu.efi`,
built on the EDK II scaffold) to present one polished picker for multiple
operating systems — Linux, Windows, macOS-with-opt-in-Apple-parts, and bare
`.efi` loaders.

We deliberately ship **zero OpenCore binaries** and **zero OpenCore code**;
`docs/plan/00-PLAN.md` documents the re-implementation of the architecture
instead (driver model, config parsing, kernel scanning, picker UX).

See `docs/plan/00-PLAN.md` for the full plan, boot-chain diagram,
licensing model, hardening model, and milestone list.

Quick situational map:

- `efi/acreetbootdrv/` — our UEFI boot-manager (menu entry supplier).
- `efi/miniboot/` — hardened microboot kernel config + initramfs payload.
- `grub/` — GRUB core and the `ACREETBOOT-ZSWAP` payload loading.
- `sdboot/` — systemd-boot loader.conf + chainload glue.
- `acreetbootctl/` — setup CLI (`acreetboot install|uninstall|status`).
- `c/` — small userspace C (manifest signer, partlabel helpers, tests).
- `test/` — unittest harness.
- `FreeBSD-vmscript/` — bhyve test harness.

## Status

v0 skeleton. Owner/Co-Lead: Natalie (AcreetionOS). License: BSD-3.
