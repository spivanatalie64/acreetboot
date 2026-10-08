/* AcreetBoot — efivarfs BootNext writer & inspector
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Usage:
 *   setbootnext [--efivars DIR] [--remove] [--print] BOOT_INDEX_HEX-or-DESC
 *   setbootnext [--efivars DIR] --scan            # list BootXXXX vars + desc
 *
 * Writes the UEFI global variable "BootNext" (GUID
 * 8be4df61-93ea-11d0-8200-00c04fd430c8) with UINT16 little-endian boot
 * option index. Designed to run from the microboot initramfs, and to be
 * unit-testable against a FAKE efivarfs directory for our test harness
 * (the --efivars override exists for exactly that: receipt-grade testing,
 * no firmware required).
 */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GLOBAL_VAR_GUID "8be4df61-93ea-11d0-8200-00c04fd430c8"
#define DEFAULT_EFIVARS "/sys/firmware/efi/efivars"
#define EFI_VAR_ATTR_NV_BS_RT 0x00000007u  /* NV | BS | RT */

static int
write_u16_var (const char *dir, const char *name, uint16_t value)
{
  char path[512];
  unsigned char buf[6];
  FILE *fp;

  snprintf (path, sizeof (path), "%s/%s-%s", dir, name, GLOBAL_VAR_GUID);
  /* efivarfs refuses rewrites that change the size, so delete first. */
  unlink (path);  /* ok to fail: var may simply not exist yet */

  buf[0] = (unsigned char)(EFI_VAR_ATTR_NV_BS_RT & 0xFF);
  buf[1] = (unsigned char)((EFI_VAR_ATTR_NV_BS_RT >>  8) & 0xFF);
  buf[2] = (unsigned char)((EFI_VAR_ATTR_NV_BS_RT >> 16) & 0xFF);
  buf[3] = (unsigned char)((EFI_VAR_ATTR_NV_BS_RT >> 24) & 0xFF);
  buf[4] = (unsigned char)(value & 0xFF);
  buf[5] = (unsigned char)((value >> 8) & 0xFF);

  fp = fopen (path, "wb");
  if (fp == NULL) return -1;
  if (fwrite (buf, 1, sizeof (buf), fp) != sizeof (buf)) {
    fclose (fp);
    unlink (path);
    return -1;
  }
  if (fclose (fp) != 0) { unlink (path); return -1; }
  return 0;
}

static FILE *
open_boot_var (const char *dir, char *namebuf, size_t bufsz, uint16_t idx)
{
  char path[512];
  snprintf (namebuf, bufsz, "Boot%04X", idx);
  snprintf (path, sizeof (path), "%s/%s-%s", dir, namebuf, GLOBAL_VAR_GUID);
  return fopen (path, "rb");
}

/* Parse EFI_LOAD_OPTION: [u32 attrs][u16 file-path-list-len][UTF16 desc]\0\0 …
 * Print just the description to stdout (decode UTF-16LE naively ASCII-safe). */
static int
print_boot_desc (const char *dir, uint16_t idx)
{
  char namebuf[16];
  FILE *fp = open_boot_var (dir, namebuf, sizeof (namebuf), idx);
  unsigned char hdr[6];
  int ch, pairs;

  if (fp == NULL) return -1;
  if (fread (hdr, 1, sizeof (hdr), fp) != sizeof (hdr)) { fclose (fp); return -1; }
  pairs = (int)hdr[4] | ((int)hdr[5] << 8);
  (void)pairs;
  /* skip attributes(4)+len(2), walk UTF-16LE pairs until NUL NUL */
  fgetc (fp); fgetc (fp);
  while ((ch = fgetc (fp)) != EOF) {
    int lo = ch, hi = fgetc (fp);
    if (lo == 0 && hi == 0) break;
    if (hi == 0 && lo >= 0x20 && lo < 0x7F) putchar (lo);
    else putchar ('?');
  }
  putchar ('\n');
  fclose (fp);
  return 0;
}

static int
scan_boot_vars (const char *dir)
{
  DIR *d = opendir (dir);
  struct dirent *e;
  unsigned idx;

  if (!d) return -1;
  while ((e = readdir (d)) != NULL) {
    if (sscanf (e->d_name, "Boot%4x-" GLOBAL_VAR_GUID, &idx) == 1) {
      printf ("Boot%04X: ", idx);
      if (print_boot_desc (dir, idx) < 0) printf ("<unreadable>\n");
    }
  }
  closedir (d);
  return 0;
}

int
main (int argc, char **argv)
{
  const char *dir = DEFAULT_EFIVARS;
  uint16_t idx;
  int i, do_remove = 0, do_scan = 0;

  for (i = 1; i < argc; i++) {
    if (strcmp (argv[i], "--efivars") == 0 && i + 1 < argc) dir = argv[++i];
    else if (strcmp (argv[i], "--remove") == 0) do_remove = 1;
    else if (strcmp (argv[i], "--scan") == 0)   do_scan   = 1;
    else if (strcmp (argv[i], "--print") == 0 && i + 1 < argc) {
      idx = (uint16_t)strtoul (argv[++i], NULL, 16);
      return print_boot_desc (dir, idx) < 0 ? 1 : 0;
    } else break;
  }

  if (do_scan) return scan_boot_vars (dir) < 0 ? 1 : 0;

  if (do_remove) {
    char path[512];
    snprintf (path, sizeof (path), "%s/BootNext-%s", dir, GLOBAL_VAR_GUID);
    unlink (path);
    return 0;
  }

  if (i >= argc) {
    fprintf (stderr, "usage: setbootnext [--efivars DIR] [--scan|--remove] "
                     "<BootXXXX-index-in-hex>\n");
    return 2;
  }
  idx = (uint16_t)strtoul (argv[i], NULL, 16);
  return write_u16_var (dir, "BootNext", idx) == 0 ? 0 : 1;
}
