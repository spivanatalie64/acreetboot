# grub-unsecure — BYO-compiled GRUB variant of AcreetBoot

FOR SPECIALIZED HARDWARE AND EXPERIMENTERS ONLY. Not shipped, not built by
any AcreetionOS pipeline, not even by this repo's ./build.sh. You must
compile it yourself and make one explicit edit — see the GATE below.

What you give up (say goodbye to):
- The microboot integrity hop (systemd-boot → microboot kernel → BootNext).
  Nothing verifies the sd-boot + AcreetBootMainMenu payload before it runs.
- The non-ESP XBOOTLDR payload partition; the whole chain lives on the ESP,
  which is remounted fw-rw on every firmware write (VFAT + EFI self-tools).
- `lockdown=confidentiality` guarantees for the boot chain's own stage.

What you keep:
- systemd-boot stays as the FIRST stage (we never abandon the distro shim);
- sd-boot entries from ../sdboot still work unchanged;
- `AcreetBootMainMenu.efi` still appears through the fast-path chainload;
- chains faster: no warm reboot, GRUB does the chainload directly.

GATE (mandatory user edit, one line, no defaults):
    echo "I-ACCEPT-THE-UNSECURE-ACREETBOOT-CHAIN" >> grub-unsecure/ACCEPT.gate
Then:

    ./grub-unsecure/build-core.sh            # builds build/grub-unsecure/core.efi
    sudo ./grub-unsecure/install-variant.sh  # installs NVRAM entry + cfg
