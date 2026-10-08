/* AcreetBootMainMenu.efi — acreetboot UEFI boot manager
 *
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Purpose
 *   UEFI boot manager loaded by systemd-boot via chainload (or booted
 *   straight from the firmware). Replaces the "boot everything with magic
 *   autodetect" model:
 *     - enforces a single, auditable menu source (acreetboot.toml + scan
 *       results), never just whatever firmware happens to have registered;
 *     - builds a hard menu from /loader/entries (BSD boot loader spec)
 *       plus explicit auto-detect fallback;
 *     - optional OpenCanopy-style external GUI later, text picker first.
 *
 * Build (skeleton)
 *   This file compiles against the vendored edk2 scaffold in ../edk2.
 *   See ../mk/acreetbootdrv.mk for the MdePkg/MdeModulePkg bases we expect.
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>

#define ACREETBOOT_VERSION "0.1.0"

/* Menu entry model. Internal representation deliberately matches what
 * acreetbootctl installs into /acreetboot/manifests so the source of truth
 * is always the signed manifest, never a runtime guess.
 */
typedef enum {
  EntryKindLinux,
  EntryKindEfi,
  EntryKindGrub,
  EntryKindSeparator,
} AcreetEntryKind;

typedef struct {
  CHAR16        *Title;          /* shown in the menu */
  CHAR16        *Kernel;         /* for EntryKindLinux: /boot/vmlinuz-… */
  CHAR16        *Initrd;
  CHAR16        *Cmdline;
  CHAR16        *EfiFile;        /* for EntryKindEfi/Grub: path to .efi   */
  AcreetEntryKind Kind;
  BOOLEAN        Auxiliary;      /* hidden until "show all" is toggled   */
} AcreetMenuEntry;

/* Selection loop: return the picked entry; -1 = boot 0x EFFI shell. */
static
UINTN
AcreetPick (
  AcreetMenuEntry *Entries,
  UINTN            Count
  )
{
  /* TODO phase 0.3: text picker with sane keybindings, async timeout.
   * Phase 0.4+: external GUI protocol (a sibling AcreetBootPickerGui.efi
   * like OpenCanopy — same protocol idea, ours to design). */
  UINTN Index;
  for (Index = 0; Index < Count; Index++) {
    if (Entries[Index].Kind == EntryKindSeparator) {
      continue;
    }
    Print (L"[%lu] %s\n", (UINT64)Index, Entries[Index].Title);
  }
  /* Default pick: whatever is legitimately first non-aux, never aux. */
  for (Index = 0; Index < Count; Index++) {
    if (!Entries[Index].Auxiliary) {
      return Index;
    }
  }
  return (UINTN)-1;
}

/* Loader-entry file parser lives in a sibling TU; stub for now so the
 * driver links without EDK2-specific file I/O pulled in prematurely. */
extern AcreetMenuEntry *LoadLoaderEntries (UINTN *CountOut, EFI_STATUS *Status);

EFI_STATUS
EFIAPI
AcreetMainMenuEntry (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS       Status;
  AcreetMenuEntry *Entries    = NULL;
  UINTN            EntryCount = 0;
  UINTN            Picked;

  Print (L"AcreetBoot " ACREETBOOT_VERSION " (main menu, BSD-3)\n");
  Print (L"Reading disk-native menus: /loader/entries + /acreetboot/manifests\n");

  Status = EFI_SUCCESS;
  Entries = LoadLoaderEntries (&EntryCount, &Status);
  if (EFI_ERROR (Status) || (Entries == NULL)) {
    Print (L"No loader entries found; booting last known good.\n");
    /* Phase 0.2 will do: LoadImage(OpenCore replacement) / EFI shell. */
    return Status;
  }

  Picked = AcreetPick (Entries, EntryCount);
  if (Picked != (UINTN)-1) {
    Print (L"Picked: %s\n", Entries[Picked].Title);
  }

  /* TODO phase 0.2: use mEfiLoadImageProtocol + gBS->StartImage to jump
   * into the target; never gRT->ResetSystem directly. */
  return Status;
}
