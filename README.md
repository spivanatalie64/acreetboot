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
Windows (via WanBoot.efi — a Linux-KVM host that boots the official
Windows files as a guest so Windows Update can never overwrite the chain),
BSD, GNU/Hurd, OpenSolaris/illumos, macOS-with-opt-in-Apple-parts, and
bare `.efi` loaders.

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
                    └─ multi-OS picker (Linux / Windows-via-WanBoot /
                       BSD / Hurd / illumos / macOS / other .efi)
```

A `fast path` entry (`10-acreetboot-direct.conf`) chainloads
`AcreetBootMainMenu.efi` directly and skips the micro-integrity hop;
a recovery entry (`01-acreetboot-recovery.conf`) drops to a busybox shell
inside the microboot with `acreetboot.recover=1`.

### Why this chain and not a stuck-on-GRUB variant

- **systemd-boot as the sole first stage.** GRUB is a feature-rich
  interpreter — modules, scriptlets, edit consoles — and every one of
  those is attack surface and a config-drain surface. sd-boot does one
  job (boot the entry we ship), is maintained by the distro/vendor as a
  binary (no embedded `.cfg` interpreter we must pin), has no interactive
  editor, and is the path forward for Secure Boot tooling. We build ON
  it rather than beside it.
- **microboot before any chainload.** From running Linux you cannot
  directly launch an EFI app — the only correct hop is the UEFI
  `BootNext` variable plus a warm reboot (the same mechanism systemd
  itself uses). So the microkernel's stage is: verify the payload
  manifest, write `BootNext`, reboot. In exchange we get an
  authenticity gate BEFORE `AcreetBootMainMenu.efi` ever runs, with the
  microkernel itself running `lockdown=confidentiality` +
  `module.sig_enforce=1` — the most hardened code in the chain.
- **Payload lives on its own XBOOTLDR partition (ACREETBOOT-ZSWAP).**
  systemd-boot only reads kernels from ESP or XBOOTLDR, so the payload
  partition is natively reachable with zero GRUB-style fs gymnastics —
  and it is NOT the ESP, so a compromised/rewritable ESP never
  silently replaces the verification stage itself.
- **Warm reboot is the honest cost.** The microkernel boots in a
  couple of seconds; we pay one firmware cycle per boot for a
  verify-before-chainload chain that GRUB/VFAT root kits have a much
  harder time crossing. Devices where seconds matter use the fast
  path — an explicit, visible entry, not a hidden default.
- **Loader-compiled for one picker.** All OS choice lives in
  `AcreetBootMainMenu.efi` (our BSD-3 code, one bounded config source,
  manifest-verified) instead of being split between GRUB scriptlets and
  whatever os-prober guessed — auditable, uniform, and portable to other
  AcreetionOS editions.
- **Windows via WanBoot.efi, not via `bootmgfw.efi`.** Windows Update
  overwrites `\EFI\Microsoft\Boot\bootmgfw.efi` and resets UEFI NVRAM
  `BootOrder`/`BootNext` on every feature update — any chain that
  touches the Microsoft ESP path breaks. WanBoot.efi lives at
  `\EFI\acreetboot\` (a path Windows does not know about), boots a
  minimal Linux environment, brings up KVM, and launches the official
  Windows boot files as a **guest**. Windows validates its own Secure
  Boot chain inside the VM; the host EFI chain is entirely
  AcreetionOS-owned and Windows can never overwrite it.

## Repository map

- `efi/edk2/AcreetBootPkg/` — our EDK2 package; boot-manager source lives at
  `efi/edk2/AcreetBootPkg/AcreetMainMenu/AcreetMainMenu.c`.
- `tools/` — UKI lane is Debian-kernel-based (`tools/uki-build.sh`,
  kernel fetched + sha-index-verified by `tools/debian-kernel-fetch.sh`,
  `ACREETBOOT_BUILD_UKI=1 ./build.sh` stages it; OVMF e2e auto-upgrades
  to the real kernel when a staged UKI exists). EDK II itself is
  never committed: `tools/edk2-bootstrap.sh` fetches `edk2-stable202608`
  checksum-pinned into `build/edk2/`, and `tools/edk2-build.sh` compiles +
  stages `build/efi-output/*.efi`. QEMU/OVMF end-to-end proof of the
  LoadImage+StartImage hand-off: `test/test_ovmf_boot.sh`.
- `efi/miniboot/` — microboot kernel config + initramfs payload sources.
- `efi/wanboot/` — WanBoot: KVM-host Linux kernel + initramfs that launches
  Windows as a KVM guest using its official boot files (overwrite-immune).
- `sdboot/` — systemd-boot `loader.conf` + entry templates (microboot default).
- `acreetbootctl/` — setup CLI (`status`, `plan`; `install`/`uninstall` next).
- `c/` — userspace C: `bootstage.c` (microboot init), `setbootnext.c`
  (efivarfs BootNext writer/inspector, unit-tested against a fake efivarfs).
- `initramfs/` — microboot userspace init.
- `test/` — test harness (`test/run_all.sh`).
- `docs/` — `plan/00-PLAN.md` (plan, licensing, hardening model, milestones)
  and `sprint/` logs.
- `linux-vmscript/` — QEMU/OVMF test harness (Linux side of the VM matrix).
- `grub-unsecure/` — BYO-compiled UNSECURE GRUB variant (specialized
  hardware): skipped by `./build.sh`, gated behind an explicit acceptance
  edit (grub-unsecure/ACCEPT.gate) + a `GRUB_SRC` source build.

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
