# AcreetBoot

[![tests](https://img.shields.io/badge/tests%20green-2%2F2-blue)]() [![license](https://img.shields.io/badge/license-BSD--3-green)]()

AcreetBoot is AcreetionOS's configurable multi-operating-system boot
orchestrator. It uses **systemd-boot as the primary bootloader** — no GRUB
anywhere in the default chain — and adds a hardened **microboot** stage on
top: a minimized Linux kernel running with `lockdown=confidentiality` and
module signature enforcement, loaded from a dedicated XBOOTLDR partition,
which verifies the payload manifest and hands off to our own **BSD-3**
UEFI boot manager (`AcreetBootMainMenu.efi`, built on the EDK II scaffold)
to present one polished picker for multiple operating systems — Linux,
Windows, macOS-with-opt-in-Apple-parts, and bare `.efi` loaders.

We deliberately ship **zero OpenCore binaries** and **zero OpenCore code**;
`docs/plan/00-PLAN.md` documents our re-implementation of the *architecture*
instead (driver model, config parsing, kernel scanning, picker UX).

## Boot chain (default: hardened)

```
UEFI firmware
  └─ systemd-boot (primary; loader.conf, editor off)
      └─ 00-acreetboot-microboot.conf (DEFAULT)
           microkernel + initramfs from the XBOOTLDR GPT partition
           labeled ACREETBOOT-ZSWAP
           (lockdown=confidentiality, module.sig_enforce=1,
            runtime zswap=zstd, ESP mounted read-only in the stage)
           └─ manifest-verify → write UEFI BootNext → warm reboot
                └─ /EFI/acreetboot/AcreetBootMainMenu.efi
                    └─ multi-OS picker (Linux / Windows / macOS / other .efi)
```

A `fast path` entry (`10-acreetboot-direct.conf`) chainloads
`AcreetBootMainMenu.efi` directly and skips the micro-integrity hop;
a recovery entry (`01-acreetboot-recovery.conf`) drops to a busybox shell
inside the microboot with `acreetboot.recover=1`.

## Repository map

- `efi/acreetbootdrv/` — our UEFI boot-manager (menu entry supplier).
- `efi/edk2/` — vendored EDK II scaffold for building the `.efi` binaries.
- `efi/miniboot/` — microboot kernel config + initramfs payload sources.
- `sdboot/` — systemd-boot `loader.conf` + entry templates (microboot default).
- `acreetbootctl/` — setup CLI (`status`, `plan`; `install`/`uninstall` next).
- `c/` — userspace C: `bootstage.c` (microboot init), `setbootnext.c`
  (efivarfs BootNext writer/inspector, unit-tested against a fake efivarfs).
- `initramfs/` — microboot userspace init.
- `test/` — test harness (`test/run_all.sh`).
- `docs/` — `plan/00-PLAN.md` (plan, licensing, hardening model, milestones)
  and `sprint/` logs.
- `linux-vmscript/` — QEMU/OVMF test harness (Linux side of the VM matrix).
- `FreeBSD-vmscript/` — bhyve test harness.

## Build

```
git clone https://github.com/spivanatalie64/acreetboot.git
cd acreetboot
./build.sh        # syntax-checks, builds C tools, runs the test suite
```

## Status

v0 skeleton — chain glue + first-party UEFI boot manager source in place;
milestones (EDK2 first boot, kernel scanner, manifest sealing, CLI install)
tracked in `docs/plan/00-PLAN.md`. Owner/Co-Lead: Natalie (AcreetionOS).
License: BSD-3.
