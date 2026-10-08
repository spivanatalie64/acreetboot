# AcreetBoot — target-model microkernel config fragment
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# This is an overlay: start from your distro's linux kernel config
# (e.g. Arch's config under /usr/lib/modules/<ver>/build/.config), apply
# this fragment via `scripts/kconfig/merge_config.sh`, and build the
# bzImage as build/miniboot/vmlinuz-acreetboot. The result is the
# "microboot kernel": tiny attack surface, runtime zswap on for the tiny
# C exchange, no module playground, everything needed built-in.

# ---- size/attack-surface reductions -----------------------------------
CONFIG_MODULES=n
CONFIG_MODULE_SIG_FORCE=y            # sig_enforce via cmdline anyway
CONFIG_EMBEDDED=y
CONFIG_KEXEC=n
CONFIG_HIBERNATION=n
CONFIG_SUSPEND=n
CONFIG_SWAP=n
#CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS? — leave to distro default.

# ---- UEFI runtime services we rely on (BootNext, efivarfs) ------------
CONFIG_EFI=y
CONFIG_EFIVAR_FS=y
CONFIG_EFI_STUB=y
#CONFIG_EFI_VARS is legacy pstore stuff; keep efivarfs only.

# ---- what the initramfs needs (all built-in; no modules) --------------
CONFIG_DEVTMPFS=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_ZSTD=y
CONFIG_EXT4_FS=y
CONFIG_FS_POSIX_ACL=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
# ESP is vfat/FAT32:
CONFIG_VFAT_FS=y
CONFIG_NLS_CODEPAGE_437=y
CONFIG_NLS_ASCII=y

# ---- zswap (runtime compressed swap cache; the "zswap" in the name) ---
CONFIG_ZSWAP=y
CONFIG_ZSWAP_DEFAULT_ON=y
CONFIG_ZSWAP_COMPRESSOR_DEFAULT_ZSTD=y
CONFIG_ZSWAP_ZPOOL_DEFAULT_ZSMALLOC=y
CONFIG_ZSMALLOC=y

# ---- lockdown --------------------------------------------------------
CONFIG_SECURITY_LOCKDOWN_LSM=y
# severity chosen on the acreeboot cmdline (integrity|confidentiality)

# ---- squash the boring bits ------------------------------------------
CONFIG_DRM=n                 # no graphical fbcon needed (serial + vga text)
CONFIG_SOUND=n
CONFIG_WIRELESS=n
CONFIG_NFS_FS=n
CONFIG_CIFS=n
CONFIG_IPV6=n                # microboot is offline by design
#CONFIG_FTRACE=n etc. — tune expectations against the distro baseline and
# audit with `scripts/kconfig/fragment` diffing in test/test_configfrag.sh.
