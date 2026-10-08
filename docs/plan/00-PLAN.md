# AcreetBoot — Build-Out Plan

Status: v0 skeleton (working, growing). Owner: Natalie (AcreetionOS Co-Lead).
License: BSD-3 (see ../LICENSE). EDK2 is BSD-3/Apache-2 — we clone the
*architecture* of OpenCore/OpenLinuxBoot (driver model + config + kernel
scanning + picker UX) with our own code, and ship zero OpenCore binaries.

## Boot chain (default, hardened)

```
UEFI firmware
  └─ systemd-boot (primary, /EFI/BOOT/BOOTX64.EFI via bootctl install)
       └─ default entry: microboot kernel + initramfs
            (loaded from the dedicated XBOOTLDR GPT partition labeled
             ACREETBOOT-ZSWAP; kernel runs lockdown=confidentiality,
             module.sig_enforce=1, runtime zswap enabled)
            └─ microboot init: verify payload manifest, write UEFI
               BootNext -> "AcreetBoot main menu" entry, warm reboot
                 └─ UEFI firmware (consumes BootNext)
                      └─ /EFI/acreetboot/AcreetBootMainMenu.efi
                           ├─ kernel scanner (Linux / Windows / macOS /
                           │  / other .efi)
                           └─ boots the choice
```

Fast path (optional): the `10-acreetboot-direct.conf` systemd-boot entry
chainloads `AcreetBootMainMenu.efi` directly and skips the micro-integrity
hop (no extra reboot). Recovery shell is `01-acreetboot-recovery.conf`.

### Why it is shaped this way

- **systemd-boot sole first stage:** a minimal, vendor-maintained binary
  with no embedded interpreter, no edit console, no module soup — the
  slim attack surface we actually want, and the foundation acreetboot
  builds ABOVE rather than a bootloader we beta-sit inside.
- **`BootNext` is the only correct Linux → EFI-app hop:** a running
  Linux cannot directly launch an EFI binary; UEFI `BootNext` + warm
  reboot is the standard mechanism (systemd itself uses it). We use it
  deliberately so the microkernel can *verify before chainloading*.
- **Payload on a separate XBOOTLDR partition:** sd-boot reads kernels
  from ESP or XBOOTLDR natively; keeping the microboot payload on a
  labeled non-ESP partition means the verifying stage cannot be silently
  rewritten by anything that only has the ESP.
- **Polish lives at ONE layer:** the picker is `AcreetBootMainMenu.efi`
  (BSD-3, bounded config via manifests) — multi-OS choice gets the nice
  UX without inheriting the interpretive complexity GRUB would drag in.
- **Honestness about the cost:** hardened mode pays one extra firmware
  cycle per boot. That is the whole tradeoff, and it is a first-class
  menu choice, never a hidden default.

## Layout

| dir          | what lives here                                             |
|--------------|-------------------------------------------------------------|
| `efi/miniboot` | GUard-mode microboot kernel config + initramfs source      |
| `efi/edk2`    | vendored/patched EDK2 scaffold (submodule or fetched tarball) |
| `efi/acreetbootdrv` | our UEFI boot-manager main + kernel scanner (BSD-3)   |
| `edk2picker` | text picker protocol abstraction shared by the above         |
| `c/`         | userspace C (manifest signer, partlabel stubs, tests shims)   |
| `sdboot/`    | systemd-boot loader.conf + entries (microboot default) |
| `sdboot/`    | systemd-boot loader.conf + chainload entry templates          |
| `acreetbootctl` | CLI: install / uninstall / status / set-bootnext / verify   |
| `initramfs/` | microboot userspace (busybox-ish minimal init)                |
| `test/`      | unit + integration harness                                   |
| `build/`     | build outputs (never committed)                              |
| `docs/`      | boot chain, security model, threat model, plan docs          |
| `man/`       | man pages                                                    |
| `linux-vmscript/` | QEMU/OVMF end-to-end test harness (Linux side) |
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

1. systemd-boot `editor no`; microboot recovery entry is auxiliary
   (hidden by default).
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

- [x] 0.1 Repo scaffold, plan, license, layout, microboot chain glue.
- [x] 0.1.5 systemd-boot entry set (microboot default, recovery, fast path).
- [ ] 0.2 EDK2 submodule + first `AcreetBootMainMenu.efi` that boots a single
      Linux kernel (no picker).
- [ ] 0.3 Scanner: populate menu from `/loader/entries` + `/boot/vmlinuz-*`.
- [ ] 0.4 Microboot stage (config, kernel config, init, BootNext C tool).
- [ ] 0.5 Manifest sealing (MAC/sign-key) + XBOOTLDR partition tooling.
- [ ] 0.6 acreetbootctl CLI (`install`/`uninstall` wiring all of the above).
- [ ] 0.6.5 Alternative hops (TRL today, config-shape documented):
      a) UKI+Secure Boot path (removes microboot hop entirely, needs MOK enrollment infra);
      b) Linux-only kexec chain (microboot kexec's straight to user kernel — no EFI app, no extra reboot);
      c) grub-unsecure/ BYO-COMPILE variant for specialized hardware: explicit
      unsecure GRUB chain (no manifest verify, no XBOOTLDR payload partition),
      gated behind a typed acceptance line (grub-unsecure/ACCEPT.gate) + a
      mandatory GRUB_SRC source build — never prebuilt, never in ./build.sh.
- [ ] 0.7 End-to-end VM harness: `linux-vmscript/` (QEMU/OVMF) + `FreeBSD-vmscript/` (bhyve). `linux-vmscript/run-vm.sh` stages ESP + XBOOTLDR payload partitions and boots OVMF headless with serial assertions.
