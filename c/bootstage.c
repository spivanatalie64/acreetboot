/* AcreetBoot — microboot lockdown pre-boot stage
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Runs as pid 1 inside the microbow microkernel (minimal config, built by
 * build.sh -> build/miniboot/vmlinuz-acreetboot). Kernel cmdline includes:
 *   lockdown=confidentiality module.sig_enforce=1 zswap.enabled=1
 * Reached via systemd-boot -> entry 00-acreetboot-microboot.conf, which
 * loads us from the dedicated XBOOTLDR GPT partition labeled
 * "ACREETBOOT-ZSWAP" (ext4 by default):
 *   linux  /vmlinuz-acreetboot
 *   initrd /initramfs-acreetboot.cpio.zst
 * (The naming echoes "the compressed payload hop"; it is NOT Linux's
 *   zswap runtime feature — see docs/plan/00-PLAN.md hardening notes.)
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>

#define XSSPASS          "ACREETBOOT-ZSWAP"   /* payload partition label */
#define ESP_DIR          "/esp"
#define PAYLOAD_DIR      "/payload"

static void
die (const char *why)
{
  fprintf (stderr, "acreet-boot: catastrophic stop: %s (%s)\n",
           why, strerror (errno));
  _exit (0x50 /* deperating, not spinning */);
}

/* Payload partition is found by partition label, never by /dev/sdX guess. */
static int
find_payload_by_label (char *out, size_t outsz)
{
  char path[256];
  snprintf (path, sizeof (path), "/dev/disk/by-partlabel/%s", XSSPASS);
  if (access (path, R_OK) == 0) { strncpy (out, path, outsz); return 0; }
  snprintf (path, sizeof (path), "/dev/disk/by-label/%s", XSSPASS);
  if (access (path, R_OK) == 0) { strncpy (out, path, outsz); return 0; }
  return -1;
}

int
main (void)
{
  char payloadDev[256];
  FILE           *cmd;

  if (getpid () != 1) { fprintf (stderr, "acreet-boot: must be pid 1\n"); return 111; }

  /* Read the kernel cmdline; recover flag decides strictness. */
  int recover = 0;
  if ((cmd = fopen ("/proc/cmdline", "r")) != NULL) {
    char buf[4096] = {0};
    size_t n = fread (buf, 1, sizeof (buf) - 1, cmd);
    (void)n;
    fclose (cmd);
    if (strstr (buf, "acreetboot.recover=1") != NULL) recover = 1;
  }

  if (mount ("proc",  "/proc", "proc",   0, NULL) < 0) die ("mount /proc");
  if (mount ("sysfs", "/sys",  "sysfs",  0, NULL) < 0) die ("mount /sys");
  if (mount ("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0) die ("mount /dev");

  if (find_payload_by_label (payloadDev, sizeof (payloadDev)) < 0) die ("find payload");
  mkdir (PAYLOAD_DIR, 0700);
  if (mount (payloadDev, PAYLOAD_DIR, "ext4", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC,
             NULL) < 0) die ("mount payload (RO)");

  /* TODO phase 0.5: verify SHA-256 manifest covering
   *     /esp/EFI/AcreetBoot/systemd-bootx64.efi
   *     /esp/EFI/AcreetBoot/AcreetBootMainMenu.efi
   *   against /payload/manifest.sha256 (root owner, 0600).
   *   strict-fail == poweroff; recover mode == soft-fail. */

  /* Chain target lives on a normal ESP; we leave it for firmware.
   * Set UEFI BootNext here, drop the mounts, warm reboot. */
  if (!recover) {
    /* Done by setbootnext in initramfs; see initramfs/init + c/setbootnext.c. */
  }

  reboot (RB_AUTOBOOT);
  return 0; /* not reached */
}
