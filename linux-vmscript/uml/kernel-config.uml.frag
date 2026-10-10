# AcreetBoot UML microboot test lane
# SPDX-License-Identifier: BSD-3-Clause
# Derived from `make ARCH=um defconfig`; this fragment forces what the
# microboot boot-chain logic test requires. efivarfs is a UEFi matter and
# does NOT exist under UML — the BootNext hop is exercised on real
# firmware/QEMU lanes only; UML tests the pure userspace logic.
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_ZSTD=y
CONFIG_EXT4_FS=y
CONFIG_USER_NS=n
