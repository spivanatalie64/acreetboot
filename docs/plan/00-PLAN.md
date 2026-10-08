# AcreetBoot — Build-Out Plan

Status: v0 skeleton (working, growing). Owner: Natalie (AcreetionOS Co-Lead).
License: BSD-3 (see ../LICENSE). EDK2 is BSD-3/Apache-2 — we clone the
*architecture* of OpenCore/OpenLinuxBoot (driver model + config + kernel
scanning + picker UX) with our own code, and ship zero OpenCore binaries.

## Boot chain (default, hardened)

```
UEFI firmware
  └─ GRUB core (/EFI/acreetboot/grub)          [grub/grub.cfg, no editor]
       └─ microboot Linux kernel + initramfs
            (payload living compressed on the dedicated ACREETBOOT-ZSWAP
             partition; kernel runs with lockdown=confidentiality,
             module.sig_enforce=1, runtime zswap enabled)
            └─ microboot init: ESP mounted read-only, payload manifest
               verified, UEFI BootNext written, warm reboot
                 └─ UEFI firmware (consumes BootNext)
                      └─ systemd-boot (sdboot/systemd-bootx64.efi)
                           └─ chainload AcreetBootMainMenu.efi   ("the binary")
                                └─ AcreetBootMainMenu.efi (edk2picker/)
                                     ├─ acreetbootdrv-equivalent kernel scanner
                                     ├─ Linux / Windows / macOS / EFI shell entries
                                     └─ boots the choice
```

Fast path (optional, from the GRUB menu or when microboot
`ACREETBOOT_MODE=fast`): GRUB chainloads systemd-boot directly and skips the
micro-integrity hop.

## Layout

| dir          | what lives here                                             |
|--------------|-------------------------------------------------------------|
| `efi/miniboot` | GUard-mode microboot kernel config + initramfs source      |
| `efi/edk2`    | vendored/patched EDK2 scaffold (submodule or fetched tarball) |
| `efi/acreetbootdrv` | our UEFI boot-manager main + kernel scanner (BSD-3)   |
| `edk2picker` | text picker protocol abstraction shared by the above         |
| `c/`         | userspace C (manifest signer, partlabel stubs, tests shims)   |
| `grub/`      | GRUB core cfg + `ACREETBOOT-ZSWAP` partition load logic       |
| `sdboot/`    | systemd-boot loader.conf + chainload entry templates          |
| `acreetbootctl` | CLI: install / uninstall / status / set-bootnext / verify   |
| `initramfs/` | microboot userspace (busybox-ish minimal init)                |
| `test/`      | unit + integration harness                                   |
| `build/`     | build outputs (never committed)                              |
| `docs/`      | boot chain, security model, threat model, plan docs          |
| `man/`       | man pages                                                    |
| `FreeBSD-vmscript/` | bhyve test harness for non-Linux OS boot testing       |

## Licensing

- All first-party code here: BSD-3 (LICENSE at repo root).
- Three external pieces, all permissively licensed:
  - **EDK II** (BSD-3 + Apache-2.0 patent grant) — build scaffolding for
    UEFI modules.
  - **systemd-boot** (LGPL-2.1+) — used as a *binary* from the distro, not
    modified; shipped as part of the OS, not as acreetboot property.
  - **GRUB** (GPL-3.0+ for its own code) — GRUB is invoked as a system
    bootloader already on the host; acreetboot ships *configuration* for it,
    not GRUB itself. The config files we author are BSD-3.
- **Nothing from OpenCore is vendored.** We re-implement the good parts
  (driver loading flow, kernel scanning, configurable picker) as first-party
  BSD-3 code. If OpenCore's cleaner UX is desired later, they can be filed
  as an upstream-contribution suggestion, not bundled.

## Hardening model

1. GRUB config `set editor=no`, no interactive `c`/`e`, no USB writes.
2. Microboot kernel: `lockdown=confidentiality`, `module.sig_enforce=1`,
   `zswap.enabled=1` for its own tiny runtime swap, read-only ESP mount.
3. Manifest verification of the `systemd-bootx64.efi` + `AcreetBootMainMenu.efi`
   payload before any chainload happens. Failure = poweroff (no details on
   screen) by default, soft-fail only when `acreetboot.recover=1` is passed.
4. `ACREETBOOT-ZSWAP` partition is labeled so only acreetboot service tooling
   mounts it (never in fstab, never auto-mounted by systemd-diskspace).

## Non-goals

- Not an ISO builder: it configures batches of already-installed OSes.
- Does not replace pacman/Freeman; it sits below them.
- Does not support Apple's Secure Boot enrolment for v0 — documented future.

## Milestones

- [x] 0.1 Repo scaffold, plan, license, layout.
- [ ] 0.2 EDK2 submodule + first `AcreetBootMainMenu.efi` that boots a single
      Linux kernel (no picker).
- [ ] 0.3 Scanner: populate menu from `/loader/entries` + `/boot/vmlinuz-*`.
- [ ] 0.4 Microboot stage (config, kernel config, init, BootNext C tool).
- [ ] 0.5 GRUB hardened core + `ACREETBOOT-ZSWAP` partition tools.
- [ ] 0.6 acreetbootctl CLI.
- [ ] 0.7 bhyve + QEMU end-to-end test harness.
