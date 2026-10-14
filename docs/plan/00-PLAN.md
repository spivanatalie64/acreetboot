# AcreetBoot — Build-Out Plan

Status: v0 skeleton (working, growing). Owner: Natalie (AcreetionOS Co-Lead).
License: BSD-3 (see ../LICENSE). EDK2 is BSD-3/Apache-2 — we clone the
*architecture* of OpenCore/OpenLinuxBoot (driver model + config + kernel
scanning + picker UX) with our own code, and ship zero OpenCore binaries.

## Kernel / UKI lane (decision recorded)

- **Stable boot kernel = Debian (GKI model over a Debian kernel).** Google's
  GKI *model* — kernel image + separate modules tree, no device bits — is
  applied to the Debian lane: the fetched, sha256-index-verified kernel is
  `build/debian/vmlinuz-acr` (the name every static reader in the chain
  uses instead of the standard kernel name), and its module forest is
  staged as `build/debian/modules-acr`. Debian owns kernel maintenance;
  UKI packaging, manifests and AcreetBoot glue are ours.
- **Build-time discipline on every GKI/U components build**:
  1. initramfs imported INSIDE the UKI at build time
     (`tools/initramfs-build.sh`: `c/bootstage.c` compile static →
     cpio newc gzip-9 → `.initrd`), the chain artifacts ship with the
     kernel, not beside it;
  2. debugging blobs stripped at build time (`modules-acr`: `strip
     --strip-debug` per module through an honest xz decompress/recompress;
     4229/4229 stripped, 0 skipped at time of record; the kernel image
     itself is never touched — it is a compressed, signature-carrying
     boot artifact).
- UKI producer: `tools/uki-build.sh` (stage lane: `build/ukistage`, output
  `acr-<ver>.efi` + receipt `uki.env`; install lane on a Debian target:
  verbatim `/boot/efi/EFI/Linux` + `/etc/kernel/cmdline` per the
  prescribed script, output `debian-<ver>.efi`).
- **CVE scan is a default build phase**: `tools/cve-scan.py` reads what
  CISA publishes (KEV JSON feed = their database, plus the KEV catalog
  page HTML), writes receipts + reports under `build/cve/`, and fails
  loudly (exit 2) if CISA can't be reached — zero fake success. KEV lists
  exploited-in-the-wild CVEs without product versions, so hits are
  signals to evaluate against Debian's stable patch line, not automatic
  failures; an explicit offline-only bypass is `ACREETBOOT_CVE_SCAN=0`
  and it is logged when used.
- **AOSP is not needed** (dropped from the lane; history keeps the prior
  receipts: Google publishes GKI boot.img/bzImage, not UKIs; GKI x86_64
  LTS defconfigs lack CONFIG_EFI_STUB).
- Chain runtime: port of initramfs chain logic from bash to Lua + the C
  shim (`bootstage.c`) is queued — the security win is getting the trusted
  chain OUT of shell pitfalls (IFS/word-splitting/unquoted expansion),
  not "Lua is magic"; Lua needs the C shim for mounts/efivarfs.

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
                           │  BSD / GNU-Hurd / illumos / other .efi)
                           └─ boots the choice:
                                ├─ Linux      → LoadImage vmlinuz/UKI
                                ├─ Windows    → WanBoot.efi (see below)
                                ├─ BSD        → chainload loader.efi / BOOTX64.EFI
                                ├─ Hurd       → multiboot2 direct (Mach + servers)
                                ├─ illumos    → chainload loader64.efi or multiboot2
                                ├─ macOS      → chainload .efi (opt-in Apple parts)
                                └─ other      → generic .efi chainload
```

Fast path (optional): the `10-acreetboot-direct.conf` systemd-boot entry
chainloads `AcreetBootMainMenu.efi` directly and skips the micro-integrity
hop (no extra reboot). Recovery shell is `01-acreetboot-recovery.conf`.

### Windows boot path — WanBoot.efi (overwrite-immune)

Windows entries do **not** chainload `\EFI\Microsoft\Boot\bootmgfw.efi`
directly. Windows Update and feature updates overwrite that file and
reset UEFI NVRAM `BootOrder` / `BootNext`, breaking any chain that
touches the Microsoft ESP path. WanBoot.efi removes the fight:

```
AcreetBootMainMenu.efi
  └─ (Windows entry picked)
      └─ WanBoot.efi   (\EFI\acreetboot\WanBoot.efi — AcreetionOS path)
          └─ Linux microkernel + initramfs (KVM-capable, minimal)
              └─ KVM guest: official Windows boot files
                  (bootmgfw.efi → winload.efi → Windows kernel;
                   Windows validates its own Secure Boot chain inside
                   the guest; the guest's virtual ESP is the only thing
                   Windows can ever overwrite)
```

WanBoot.efi lives under `\EFI\acreetboot\`, not under
`\EFI\Microsoft\Boot\` — Windows does not know this path exists. The
host EFI chain (sd-boot → microboot → AcreetBootMainMenu → WanBoot) is
entirely AcreetionOS-owned. Windows runs as a KVM guest using its own
official, unmodified boot files. See issue #3.

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
| `efi/wanboot` | WanBoot: KVM-host Linux kernel config + initramfs (Windows-guest launcher) |
| `efi/edk2`    | our AcreetBootPkg (DSC/INF); the EDK2 tree itself is fetched + sha256-pinned by `tools/edk2-bootstrap.sh` into build/ (never committed) |
| `efi/edk2/AcreetBootPkg` | our UEFI boot-manager main (AcreetMainMenu.c) + OVMF payload stub (AcreetBootStub.c), BSD-3 |
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

- [x] 0.2.2 GKI-model discipline over the Debian lane: `vmlinuz-acr`
      naming (static kernel reader compatibility), initramfs imported
      into the UKI at build time (static bootstage /init, cpio+gzip-9),
      module debuginfo stripped at build time (4229/4229, xz-honest),
      CISA KEV scan as default build phase (feed + page receipts under
      build/cve/). OVMF e2e upgraded: kernel AND our /init both proven
      on serial (`Linux version` + `acreet-boot` banner).

- [x] 0.2.1 UKI lane: Debian stable kernel (index-sha256-verified, signed
      pool) wrapped by ukify; OVMF e2e upgraded — the menu boots the REAL
      kernel (`Linux version` banner asserted on serial, TCG, `-no-reboot`).
      Kernel cmd in e2e starts with `console=ttyS0`; panic-on-noroot is
      expected and bounded. Superseded by 0.2.2 GKI-model discipline.

- [x] 0.1 Repo scaffold, plan, license, layout, microboot chain glue.
- [x] 0.1.5 systemd-boot entry set (microboot default, recovery, fast path).
- [x] 0.2 EDK2 sha256-pinned fetch (stable202608 + brotli + mipisyst submodules)
      + `AcreetMainMenu.efi` boots a single hardcoded PE32+ payload via
      LoadImage + StartImage — proven end-to-end in QEMU/OVMF (test/
      test_ovmf_boot.sh, `ACREET-PAYLOAD-OK` marker). Payload model is
      PE32+/UKI (firmware LoadImage handles UKIs natively); the OVMF test
      uses a two-line stub stand-in until a real UKI is staged. [issue #1]
- [ ] 0.2.5 Multi-OS-family entry model + dispatch: BSD (chainload loader.efi),
      GNU/Hurd + illumos (multiboot2 direct), GNU fallback (GRUB chainload).
      Entry struct designed for the full OS matrix. [issue #2]
- [ ] 0.2.6 WanBoot.efi: Windows boot via Linux-KVM host (overwrite-immune —
      Windows is a KVM guest, host chain is AcreetionOS-owned). [issue #3]
- [ ] 0.3 Scanner: populate menu from `/loader/entries` + `/boot/vmlinuz-*`
      + BSD `loader.conf` + illumos boot environments + Windows BCD.
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
- [x] 0.7-CI GHA intercept into us_ssh: cvm-relay-deploy + cvm-vmtest GREEN.
      Relay container live at us.iso.acreetionos.org:8477 (HMAC-authed,
      append-only receipts). testbox: Debian + qemu + ovmf; VM boots inside
      docker in TCG (us_ssh has no /dev/kvm - nested virt is architecturally
      unavailable on that host).
- [ ] 0.7 End-to-end VM harness: `linux-vmscript/` (QEMU/OVMF) + `FreeBSD-vmscript/` (bhyve). `linux-vmscript/run-vm.sh` stages ESP + XBOOTLDR payload partitions and boots OVMF headless with serial assertions.
