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
 *     - builds a hard menu from /loader/entries (boot loader spec) plus
 *       explicit auto-detect fallback;
 *     - optional OpenCanopy-style external GUI later, text picker first.
 *
 * Build (milestone 0.2)
 *   Compiled by EDK2 stable202608 through AcreetBootPkg in ../.. (the DSC
 *   lives at ../../AcreetBootPkg.dsc; this source's canonical home is
 *   here, inside the package, per the 0.2 restructure).
 *
 * Milestone 0.2 behavior (issue #1)
 *   Single hardcoded Linux payload, no picker UX (0.3 adds the scanner
 *   + picker). Boot hand-off is the correct pair:
 *     LocateProtocol(SimpleFileSystem) -> OpenVolume -> LoadImage
 *     -> LoadedImageProtocol -> StartImage.
 *   We NEVER call gRT->ResetSystem from this application: on success
 *   StartImage does not return; if it does, we report the ExitCode and
 *   bail out so the previous bootloader regains control.
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>

#define ACREETBOOT_VERSION "0.2.0"

/* Milestone 0.2: the one and only payload path. XBOOTLDR layout, ours:
 *   \acreetboot\boot\vmtest.efi — a PE32+ image (a UKI or an EFI stub),
 *   which firmware LoadImage handles natively. Raw (non-PE) vmlinuz
 *   hand-off is a later milestone (LoadLinux-style loader, 0.3+). */
STATIC CONST CHAR16 *mAcreetBootPayload = L"\\acreetboot\\boot\\vmtest.efi";

/* Menu entry model. Internal representation deliberately matches what
 * acreetbootctl installs into /acreetboot/manifests so the source of truth
 * is always the signed manifest, never a runtime guess.
 *
 * OS-family matrix (see docs/plan/00-PLAN.md + issues #2, #3):
 *   Linux        — LoadImage a vmlinuz/UKI directly.
 *   Windows      — dispatch via WanBoot.efi: a Linux-KVM host boots the
 *                  official Windows boot files (bootmgfw.efi / winload.efi)
 *                  as a KVM guest. WanBoot.efi lives at \EFI\acreetboot\,
 *                  NOT under \EFI\Microsoft\Boot\, so Windows Update can
 *                  never overwrite the host chain. Windows is the guest;
 *                  the host EFI chain is entirely AcreetionOS-owned.
 *   BSD          — chainload a BSD-family EFI loader (FreeBSD loader.efi,
 *                  OpenBSD BOOTX64.EFI, NetBSD bootx64.efi); ZFS boot-
 *                  environment aware.
 *   Multiboot    — direct multiboot2 hand-off (GNU Hurd, illumos-direct):
 *                  load kernel + module list, build MB2 info struct, jump.
 *   Efi          — generic chainload of a bare .efi (macOS, other loaders).
 *   Grub         — GRUB chainload fallback (GNU/Hurd classic, Guix).
 */
typedef enum {
  EntryKindLinux,
  EntryKindWindows,    /* WanBoot.efi → Linux-KVM host → Windows guest   */
  EntryKindBsd,        /* chainload BSD loader.efi / BOOTX64.EFI         */
  EntryKindMultiboot,  /* direct multiboot2 (Hurd, illumos-direct)       */
  EntryKindEfi,        /* generic .efi chainload (macOS, other)          */
  EntryKindGrub,       /* GRUB chainload fallback (GNU/Hurd, Guix)       */
  EntryKindSeparator,
} AcreetEntryKind;

typedef struct {
  CHAR16        *Title;          /* shown in the menu */
  CHAR16        *Kernel;         /* for EntryKindLinux: /boot/vmlinuz-… */
  CHAR16        *Initrd;
  CHAR16        *Cmdline;
  CHAR16        *EfiFile;        /* for Efi/Bsd/Grub: path to .efi to chainload */
  CHAR16        *WanBootKernel;  /* for EntryKindWindows: KVM-host Linux kernel */
  CHAR16        *WanBootInitrd;  /* for EntryKindWindows: KVM-host initramfs    */
  CHAR16        *GuestFw;        /* for EntryKindWindows: OVMF firmware for guest */
  CHAR16        *WinPartition;   /* for EntryKindWindows: Windows boot partition */
  CHAR16        *Passthrough;    /* for EntryKindWindows: VFIO profile or "virtio" */
  CHAR16        *MbModules;      /* for EntryKindMultiboot: module list (Hurd)   */
  CHAR16        *ZfsBe;          /* for EntryKindBsd: ZFS boot environment name  */
  AcreetEntryKind Kind;
  BOOLEAN        Auxiliary;      /* hidden until "show all" is toggled   */
} AcreetMenuEntry;

/* Boot one PE32+ payload: walk the volumes, LoadImage, StartImage.
 * Returns the StartImage exit code, or propagated EFI_ERROR if loading
 * failed on every volume. */
STATIC
EFI_STATUS
AcreetBootPayload (

  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                   *Handles       = NULL;
  UINTN                         HandleCount   = 0;
  UINTN                         Index;
  EFI_DEVICE_PATH_PROTOCOL     *FilePath;
  EFI_HANDLE                    NewImage      = NULL;
  UINTN                         ExitDataSize  = 0;
  CHAR16                       *ExitData      = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"AcreetBoot: no SimpleFileSystem volumes (%r)\n", Status);
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    FilePath = FileDevicePath (Handles[Index], (CHAR16 *)mAcreetBootPayload);
    if (FilePath == NULL) {
      continue;
    }

    Status = gBS->LoadImage (
                    FALSE,
                    gImageHandle,
                    FilePath,
                    NULL,
                    0,
                    &NewImage
                    );
    if (EFI_ERROR (Status)) {
      /* Expected on volumes without the payload; keep scanning. */
      continue;
    }

    /* GOTCHA — UnloadImage on failure is free; on success the image is
     * owned by us until StartImage, after which it belongs to the started
     * image and must NOT be touched from this context again. */
    Status = gBS->StartImage (NewImage, &ExitDataSize, &ExitData);
    /* Started image returned or failed: either way the LoadedImageProtocol
     * for NewImage is dead from here. Report and let the caller decide
     * (which is systemd-boot in the chain, not a firmware reboot). */
    gBS->FreePool (FilePath);
    if (Handles != NULL) {
      gBS->FreePool (Handles);
    }
    return Status;
  }

  gBS->FreePool (Handles);
  Print (L"AcreetBoot: payload %s not found on any volume\n", mAcreetBootPayload);
  return EFI_NOT_FOUND;
}

/* Loader-entry file parser lives in a sibling TU; stub for now so the
 * driver links without EDK2-specific file I/O pulled in prematurely.
 * Phase 0.3 replaces this with the real /loader/entries reader. */
STATIC
AcreetMenuEntry *
LoadLoaderEntries (
  UINTN       *CountOut,
  EFI_STATUS  *Status
  )
{
  *CountOut = 0;
  *Status   = EFI_NOT_FOUND;
  return NULL;
}

/* Selection loop: return the picked entry; (UINTN)-1 = boot nothing. */
STATIC
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

  Entries = LoadLoaderEntries (&EntryCount, &Status);
  if (EFI_ERROR (Status) || (Entries == NULL)) {
    /* Milestone 0.2 path: no loader entries parsed yet — boot the one
     * hardcoded payload. Phase 0.3 makes LoadLoaderEntries real. */
    Picked = (UINTN)-1;
  } else {
    Picked = AcreetPick (Entries, EntryCount);
    if (Picked != (UINTN)-1) {
      Print (L"Picked: %s\n", Entries[Picked].Title);
    }
  }

  if (Picked == (UINTN)-1) {
    Print (L"Boot hand-off: LoadImage + StartImage of %s\n", mAcreetBootPayload);
    Status = AcreetBootPayload ();
    /* StartImage should not return for a real kernel hand-off. If it
     * does, surface the exact status; the controlling bootloader context
     * above us decides what happens next — no gRT->ResetSystem here. */
    Print (L"AcreetBoot: payload did not hand off (%r)\n", Status);
    return Status;
  }

  /* Unreachable until 0.3 wires picked-family dispatch. */
  return EFI_UNSUPPORTED;
}
