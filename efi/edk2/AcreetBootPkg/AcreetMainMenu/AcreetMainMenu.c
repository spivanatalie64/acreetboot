/* AcreetBootMainMenu.efi — acreetboot UEFI boot manager (feature/efi-menu)
 *
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Purpose
 *   UEFI boot manager loaded by systemd-boot via chainload (or booted
 *   straight from the firmware). Replaces the "boot everything with magic
 *   autodetect" model:
 *     - single, auditable menu source: boot-loader-spec \loader\entries
 *       (plus the \acreetboot\boot fallback row) — never "whatever
 *       firmware happened to register";
 *     - the menu is the shared text TUI (edk2picker spec, the systemd-boot
 *       clone): countdown, '*' default row, '>' live row;
 *     - optional external GUI picker later, text picker first.
 *
 * Build (milestone 0.3 / feature/efi-menu)
 *   Compiled by EDK2 stable202608 through AcreetBootPkg (DSC at ../..).
 *   Canonical home of this source: this directory, per the 0.2 restructure.
 *
 * Milestone 0.3 behavior (feature/efi-menu)
 *   - Real /loader/entries reader: FIRST volume carrying \loader\entries
 *     wins (cross-volume merge is scanner-lane follow-up). Parsed keys:
 *     title, version, linux, initrd (first line wins — multi-initrd
 *     concat is a documented follow-up), efi, options. Unknown keys are
 *     ignored by design: the spec grows, we act only on what we parse.
 *   - Fallback: zero entries anywhere = the hardcoded 0.2 payload shown
 *     through the SAME TUI — one rendering truth for both paths.
 *   - Dispatch: .efi-family chainload and EFI-stub/UKI Linux kernels via
 *     LoadImage + StartImage (both are PE32+). WanBoot/BSD/Multiboot
 *     lanes are struct-designed (matrix below), wired when their scanner
 *     lanes land. Raw (non-stub) vmlinuz boot is the LoadLinux follow-up.
 *     If a menu row cannot be dispatched, we bail with EFI_UNSUPPORTED —
 *     the chain above (systemd-boot) regains control; NO reset.
 *   - We NEVER call gRT->ResetSystem from this application. On success
 *     StartImage does not return; if it does, we report and bail.
 *   - Memory: every row owns its strings; frees are NULL-guarded and
 *     complete. Paths/titles fenced at ACREET_MAX_STR (no unbounded
 *     hostile-ESP allocations).
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileInfo.h>

#include "../AcreetTextMenu/AcreetTextMenu.h"

#define ACREETBOOT_VERSION "0.3.0"

/* 0.2 contract: the one payload path when NO loader entries exist. */
#define ACREET_FALLBACK_PAYLOAD L"\\acreetboot\\boot\\vmtest.efi"

STATIC CONST CHAR16 *mLoaderEntriesDir = L"\\loader\\entries";

/* Fences: hostile-ESP DoS guard. */
#define ACREET_MAX_STR   256
#define ACREET_MAX_FILE  8192

#define MENU_TIMEOUT_SEC           10
#define MENU_TIMEOUT_FALLBACK_SEC  2

#define ACREET_NONE ((UINTN)-1)

/* Menu entry model (identical intent to 0.2; see the matrix in the plan).
 * WanBoot/Windows fields are struct-designed here so the lane wires into
 * the SAME model — not re-shaped later. */
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
  CHAR16        *Title;
  CHAR16        *Kernel;
  CHAR16        *Initrd;
  CHAR16        *Cmdline;
  CHAR16        *EfiFile;
  CHAR16        *WanBootKernel;
  CHAR16        *WanBootInitrd;
  CHAR16        *GuestFw;
  CHAR16        *WinPartition;
  CHAR16        *Passthrough;
  CHAR16        *MbModules;
  CHAR16        *ZfsBe;
  AcreetEntryKind Kind;
  BOOLEAN        Auxiliary;
} AcreetMenuEntry;

/* --------------------------- string helpers ------------------------ */

/* ASCII → UTF-16 with loud degradation ('?' for out-of-range bytes).
 * Never silent reinterpretation. Caller frees. */
STATIC
CHAR16 *
AcreetAsciiToChar16 (
  CONST CHAR8 *Src
  )
{
  CHAR16 *Out;
  UINTN   Len = 0;
  UINTN   Index;

  while (Src[Len] != '\0') {
    Len++;
  }
  if (Len > ACREET_MAX_STR) {
    Len = ACREET_MAX_STR;
  }

  Out = AllocatePool ((Len + 1) * sizeof (CHAR16));
  if (Out == NULL) {
    return NULL;
  }
  for (Index = 0; Index < Len; Index++) {
    UINT8 Byte = (UINT8)Src[Index];
    Out[Index] = ((Byte >= 0x20) && (Byte < 0x7F)) ? (CHAR16)Byte : L'?';
  }
  Out[Len] = L'\0';
  return Out;
}

/* ESP-relative (possibly '/'-separated, possibly bare) → volume-root
 * '\'-path. Caller frees. */
STATIC
CHAR16 *
AcreetNormalizePath (
  CONST CHAR16 *Raw
  )
{
  CHAR16 *Out;
  UINTN   In   = 0;
  UINTN   Outd = 0;

  if (Raw == NULL) {
    return NULL;
  }
  Out = AllocatePool ((ACREET_MAX_STR + 1) * sizeof (CHAR16));
  if (Out == NULL) {
    return NULL;
  }
  if ((Raw[0] == L'\\') || (Raw[0] == L'/')) {
    In = 1;
  }
  Out[Outd++] = L'\\';
  while ((Raw[In] != L'\0') && (Outd < ACREET_MAX_STR)) {
    CHAR16  Ch  = (Raw[In] == L'/') ? L'\\' : Raw[In];
    BOOLEAN Dup = (Outd > 1) && (Ch == L'\\') && (Out[Outd - 1] == L'\\');
    if (!Dup) {
      Out[Outd++] = Ch;
    }
    In++;
  }
  if ((Outd > 1) && (Out[Outd - 1] == L'\\')) {
    Outd--;
  }
  Out[Outd] = L'\0';
  return Out;
}

/* --------------------------- parser -------------------------------- */

typedef struct {
  CHAR16 *Title;
  CHAR16 *Version;
  CHAR16 *Linux;
  CHAR16 *Initrd;
  CHAR16 *Efi;
  CHAR16 *Options;
} ACREET_LOADER_FIELDS;

/* One KEY=VALUE line; '#' comments; first occurrence of a key wins. */
STATIC
BOOLEAN
AcreetParseLine (
  CONST CHAR8          *Line,
  ACREET_LOADER_FIELDS *F
  )
{
  CHAR8        Key[32];
  UINTN        K = 0;
  CONST CHAR8 *Val;
  CONST CHAR8 *Ptr = Line;

  while ((*Ptr == ' ') || (*Ptr == '\t')) {
    Ptr++;
  }
  if ((*Ptr == '#') || (*Ptr == '\0')) {
    return FALSE;
  }
  while ((Ptr[K] != '\0') && (Ptr[K] != '=') && (K < (sizeof (Key) - 1))) {
    Key[K] = Ptr[K];
    K++;
  }
  if (Ptr[K] != '=') {
    return FALSE; /* not KEY=VALUE: skip the stray line, never crash */
  }
  Key[K] = '\0';
  while ((K > 0) && ((Key[K - 1] == ' ') || (Key[K - 1] == '\t'))) {
    Key[--K] = '\0';
  }
  Val = &Ptr[K + 1];
  while ((*Val == ' ') || (*Val == '\t')) {
    Val++;
  }

  if (AsciiStriCmp (Key, "title") == 0) {
    if (F->Title == NULL) {
      F->Title = AcreetAsciiToChar16 (Val);
    }
  } else if (AsciiStriCmp (Key, "version") == 0) {
    if (F->Version == NULL) {
      F->Version = AcreetAsciiToChar16 (Val);
    }
  } else if (AsciiStriCmp (Key, "linux") == 0) {
    if (F->Linux == NULL) {
      CHAR16 *Raw = AcreetAsciiToChar16 (Val);
      if (Raw != NULL) {
        F->Linux = AcreetNormalizePath (Raw);
        FreePool (Raw);
      }
    }
  } else if (AsciiStriCmp (Key, "initrd") == 0) {
    if (F->Initrd == NULL) {
      CHAR16 *Raw = AcreetAsciiToChar16 (Val);
      if (Raw != NULL) {
        F->Initrd = AcreetNormalizePath (Raw);
        FreePool (Raw);
      }
    }
  } else if (AsciiStriCmp (Key, "efi") == 0) {
    if (F->Efi == NULL) {
      CHAR16 *Raw = AcreetAsciiToChar16 (Val);
      if (Raw != NULL) {
        F->Efi = AcreetNormalizePath (Raw);
        FreePool (Raw);
      }
    }
  } else if (AsciiStriCmp (Key, "options") == 0) {
    if (F->Options == NULL) {
      F->Options = AcreetAsciiToChar16 (Val);
    }
  } else {
    /* machine-id / sort-key / devicetree / future keys: skipped on
     * PURPOSE. Nothing to log: a boot menu does not chatter about the
     * spec fields it intentionally ignores. */
  }
  return TRUE;
}

/* Free every allocated field (NULL-safe), zero the struct. */
STATIC
VOID
AcreetFreeFields (
  ACREET_LOADER_FIELDS *F
  )
{
  if (F->Title   != NULL) { FreePool (F->Title); }
  if (F->Version != NULL) { FreePool (F->Version); }
  if (F->Linux   != NULL) { FreePool (F->Linux); }
  if (F->Initrd  != NULL) { FreePool (F->Initrd); }
  if (F->Efi     != NULL) { FreePool (F->Efi); }
  if (F->Options != NULL) { FreePool (F->Options); }
  ZeroMem (F, sizeof (*F));
}

/* Free one row: all owned strings, then the struct itself (NULL-safe). */
STATIC
VOID
AcreetFreeEntry (
  AcreetMenuEntry *E
  )
{
  if (E == NULL) {
    return;
  }
  if (E->Title         != NULL) { FreePool (E->Title);         }
  if (E->Kernel        != NULL) { FreePool (E->Kernel);        }
  if (E->Initrd        != NULL) { FreePool (E->Initrd);        }
  if (E->Cmdline       != NULL) { FreePool (E->Cmdline);       }
  if (E->EfiFile       != NULL) { FreePool (E->EfiFile);       }
  if (E->WanBootKernel != NULL) { FreePool (E->WanBootKernel); }
  if (E->WanBootInitrd != NULL) { FreePool (E->WanBootInitrd); }
  if (E->GuestFw       != NULL) { FreePool (E->GuestFw);       }
  if (E->WinPartition  != NULL) { FreePool (E->WinPartition);  }
  if (E->Passthrough   != NULL) { FreePool (E->Passthrough);   }
  if (E->MbModules     != NULL) { FreePool (E->MbModules);     }
  if (E->ZfsBe         != NULL) { FreePool (E->ZfsBe);         }
  FreePool (E);
}

/* Parsed fields → one owned menu row. Ownership transfers: every field
 * the row keeps is nulled in F; the caller then frees only leftovers via
 * AcreetFreeFields (also safe when NULL after full transfer). */
STATIC
AcreetMenuEntry *
AcreetBuildEntry (
  ACREET_LOADER_FIELDS *F,
  CONST CHAR16         *ConfName
  )
{
  AcreetMenuEntry *E;
  UINTN            Len;
  UINTN            I;

  if ((F->Linux == NULL) && (F->Efi == NULL)) {
    /* The spec allows title-only configs; the menu does not showcase
     * unbootable rows: skip quietly, never invent an entry. */
    AcreetFreeFields (F);
    return NULL;
  }

  E = AllocatePool (sizeof (AcreetMenuEntry));
  if (E == NULL) {
    AcreetFreeFields (F);
    return NULL;
  }
  ZeroMem (E, sizeof (*E));

  /* Title fallback chain: title → version → .conf basename. */
  if (F->Title != NULL) {
    E->Title   = F->Title;
    F->Title   = NULL;
  } else if (F->Version != NULL) {
    E->Title   = F->Version;
    F->Version = NULL;
  } else {
    for (Len = 0; (ConfName[Len] != L'\0') && (Len < ACREET_MAX_STR); Len++) {
      /* measure */
    }
    if (Len > 5) {
      Len -= 5; /* strip ".conf" */
    }
    E->Title = AllocatePool ((Len + 1) * sizeof (CHAR16));
    if (E->Title != NULL) {
      for (I = 0; I < Len; I++) {
        E->Title[I] = ConfName[I];
      }
      E->Title[Len] = L'\0';
    } else {
      E->Title = AcreetAsciiToChar16 ("");
    }
  }

  if (F->Linux != NULL) {
    E->Kind    = EntryKindLinux;
    E->Kernel  = F->Linux;  F->Linux   = NULL;
    E->Initrd  = F->Initrd; F->Initrd  = NULL;
    E->Cmdline = F->Options; F->Options = NULL;
  } else {
    E->Kind    = EntryKindEfi;
    E->EfiFile = F->Efi;    F->Efi     = NULL;
  }
  AcreetFreeFields (F);
  return E;
}

/* Boot-loader-spec suffix check: ONLY a true ".conf" suffix qualifies.
 * "entry.conf.sha256" or any other mutation never becomes a menu row. */
STATIC
BOOLEAN
AcreetIsConf (
  CONST CHAR16 *Name,
  UINTN         Len
  )
{
  if (Len < 5) {
    return FALSE;
  }
  return (Name[Len - 5] == L'.' &&
          Name[Len - 4] == L'c' && Name[Len - 3] == L'o' &&
          Name[Len - 2] == L'n' && Name[Len - 1] == L'f');
}

/* ------------------------- volume + chainload ----------------------- */

/* Boot one PE32+ payload path: walk the volumes, LoadImage, StartImage.
 * 0.2 contract, verbatim behavior for the fallback row and any entry
 * whose payload is reachable on the same volume. */
STATIC
EFI_STATUS
AcreetBootImage (
  CONST CHAR16 *Path
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE               *Handles      = NULL;
  UINTN                     HandleCount  = 0;
  UINTN                     Index;
  EFI_DEVICE_PATH_PROTOCOL *FilePath;
  EFI_HANDLE                NewImage     = NULL;
  UINTN                     ExitDataSize = 0;
  CHAR16                   *ExitData     = NULL;

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
    FilePath = FileDevicePath (Handles[Index], (CHAR16 *)Path);
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
      FreePool (FilePath);
      continue; /* expected on volumes without the payload */
    }

    /* GOTCHA — the image is ours until StartImage, after which it is the
     * started image's; never LoadImage-touch it again from this context. */
    Status = gBS->StartImage (NewImage, &ExitDataSize, &ExitData);
    FreePool (FilePath);
    if (Handles != NULL) {
      FreePool (Handles);
    }
    if (EFI_ERROR (Status)) {
      Print (L"AcreetBoot: '%s' exited %r\n", Path, Status);
    }
    return Status;
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }
  Print (L"AcreetBoot: payload %s not found on any volume\n", Path);
  return EFI_NOT_FOUND;
}

/* ---------------------- one .conf body → fields --------------------- */

/* '\r' → space in-place (Body is ours): CRLF files lose only the CR,
 * CR-only files keep their line boundaries at '\n' or the NUL. Values no
 * longer end in an invisible byte; the '?' glyph can never appear from
 * line endings. Lines are parsed from the first buffer to '\0'. */
STATIC
VOID
AcreetParseConfBody (
  CONST CHAR8          *Body,
  ACREET_LOADER_FIELDS *F
  )
{
  CHAR8       *Mut = (CHAR8 *)Body;
  CONST CHAR8 *Ptr = Body;

  while (*Mut != '\0') {
    if (*Mut == '\r') {
      *(CHAR8 *)Mut = ' ';
    }
    Mut++;
  }

  while (*Ptr != '\0') {
    CONST CHAR8 *Line = Ptr;
    CHAR8       *End  = (CHAR8 *)Ptr;

    while ((*End != '\0') && (*End != '\n')) {
      End++;
    }
    if (*End == '\n') {
      *(CHAR8 *)End = '\0'; /* body is ours: terminate, advance past */
      Ptr = End + 1;
    } else {
      Ptr = End;
    }
    AcreetParseLine (Line, F);
  }
}

/* ---------------------------- file read ----------------------------- */

/* Read up to ACREET_MAX_FILE bytes; returns NUL-terminated CHAR8 or
 * NULL. Caller frees. */
STATIC
CHAR8 *
AcreetReadFile (
  EFI_FILE_PROTOCOL *File
  )
{
  EFI_STATUS     Status;
  EFI_FILE_INFO *Info     = NULL;
  UINTN          InfoSize = 0;
  UINT64         FileSize;
  UINTN          ReadSize;
  CHAR8         *Buf;
  UINTN          BufSize;

  InfoSize = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL; /* not a regular readable file: skip, never crash */
  }
  Info = AllocatePool (InfoSize);
  if (Info == NULL) {
    return NULL;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return NULL;
  }
  FileSize = Info->FileSize;
  FreePool (Info);

  BufSize = (FileSize < ACREET_MAX_FILE) ? (UINTN)FileSize
                                         : (UINTN)ACREET_MAX_FILE;
  Buf = AllocatePool (BufSize + 1);
  if (Buf == NULL) {
    return NULL;
  }
  ReadSize = BufSize;
  Status = File->Read (File, &ReadSize, Buf);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return NULL;
  }
  Buf[ReadSize] = '\0';
  return Buf;
}

/* ------------------- \loader\entries scanner ------------------------ */

/* Scan ONE volume's \loader\entries; appends rows into (*Entries,
 * *Count, *Cap). Returns TRUE only when at least one row joined. A
 * volume without the dir quietly reports FALSE — the walk continues. */
STATIC
BOOLEAN
AcreetScanVolumeEntries (
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Sfs,
  AcreetMenuEntry  ***Entries,
  UINTN              *Count,
  UINTN              *Cap
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *Root   = NULL;
  EFI_FILE_PROTOCOL  *Dir    = NULL;
  VOID               *DirBuf = NULL;

  /* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL exposes OpenVolume ONLY: the root
   * EFI_FILE_PROTOCOL (which Open/OpenVolume-walk APIs) comes from it. */
  Status = Sfs->OpenVolume (Sfs, &Root);
  if (EFI_ERROR (Status) || (Root == NULL)) {
    return FALSE;
  }

  Status = Root->Open (Root, &Dir, (CHAR16 *)mLoaderEntriesDir,
                       EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return FALSE;
  }
  /* Root stays open for the Dir handle's lifetime — closing the parent
   * under a child handle was the crash this comment replaces. */

  /* Directory Read is POSITIONAL: each call buffers as many EFI_FILE_INFO
   * records as fit, advances the read position, and ends via
   * EFI_END_OF_FILE. A one-shot Read+walk stops at the first record —
   * that assumption was the original scanner bug. Loop until EOF; the
   * '.'/'..'/non-.conf cruft is skipped structurally by the suffix
   * filter, not special-cased. */
  for (;;) {
    UINTN Chunk;
    UINTN InChunk;

    Chunk  = 1024;
    DirBuf = AllocatePool (Chunk);
    if (DirBuf == NULL) {
      break;
    }
    Status = Dir->Read (Dir, &Chunk, DirBuf);
    if ((Chunk == 0) || (Status == EFI_END_OF_FILE)) {
      /* Position at end. Some drivers report Success with a 0-byte
       * read instead of EFI_END_OF_FILE — same truth either way, so we
       * terminate on both; the earlier forever-0 spin is this exact
       * case, closed now. */
      FreePool (DirBuf);
      DirBuf = NULL;
      break;
    }

    for (InChunk = 0; InChunk < Chunk; ) {
      EFI_FILE_INFO *Info = (EFI_FILE_INFO *)((UINT8 *)DirBuf + InChunk);

      /* Record integrity — the driver's Size is authoritative (fat
       * drivers ship self-sized records: here e.g. 82 + 2×name length).
       * Never gate against host-side sizeof: layouts differ. Bounds:
       * the Size field itself must exist and the record must lie in
       * the chunk we actually got. */
      if ((Info->Size < 8) || ((Chunk - InChunk) < Info->Size)) {
        break;
      }

      {
        CHAR16 *Name = Info->FileName;
        UINTN   Len  = 0;

        while ((Name[Len] != L'\0') && (Len < ACREET_MAX_STR)) {
          Len++;
        }

        if (AcreetIsConf (Name, Len)) {
          EFI_FILE_PROTOCOL   *ConfFile = NULL;
          ACREET_LOADER_FIELDS Fields;

          ZeroMem (&Fields, sizeof (Fields));
          Status = Dir->Open (Dir, &ConfFile, Name, EFI_FILE_MODE_READ, 0);
          if (!EFI_ERROR (Status)) {
            CHAR8 *Body = AcreetReadFile (ConfFile);
            ConfFile->Close (ConfFile);
            if (Body != NULL) {
              AcreetMenuEntry *Row;

              AcreetParseConfBody (Body, &Fields);
              FreePool (Body);
              if ((Fields.Linux != NULL) || (Fields.Efi != NULL)) {
                Row = AcreetBuildEntry (&Fields, Name);
                if (Row != NULL) {
                  if (*Count == *Cap) {
                    UINTN NewCap = ((*Cap == 0) ? 8 : (*Cap * 2));
                    VOID  *Grown = ReallocatePool (
                                     (*Cap) * sizeof (AcreetMenuEntry *),
                                     NewCap * sizeof (AcreetMenuEntry *),
                                     (VOID *)(*Entries));
                    if (Grown != NULL) {
                      *Cap     = NewCap;
                      *Entries = Grown;
                    }
                  }
                  if (*Count == *Cap) {
                    AcreetFreeEntry (Row); /* fence hit: drop, never half */
                    break;
                  }
                  (*Entries)[(*Count)++] = Row;
                }
              } else {
                AcreetFreeFields (&Fields);
              }
            }
          }
        }
      }

      InChunk += Info->Size;
    }

    FreePool (DirBuf);
    DirBuf = NULL;
  }

  if (DirBuf != NULL) {
    FreePool (DirBuf);
  }
  Dir->Close (Dir);
  Root->Close (Root);
  return (*Count > 0);
}

/* All rows, all volumes: FIRST volume with \loader\entries wins. */
STATIC
AcreetMenuEntry **
LoadLoaderEntries (
  UINTN       *CountOut,
  EFI_STATUS  *Status
  )
{
  EFI_STATUS        VolStatus;
  EFI_HANDLE       *Handles      = NULL;
  UINTN             HandleCount  = 0;
  UINTN             VolIdx;
  AcreetMenuEntry **Entries      = NULL;
  UINTN             EntryCount   = 0;
  UINTN             EntryCap     = 0;
  BOOLEAN           Found        = FALSE;

  *CountOut = 0;
  *Status   = EFI_NOT_FOUND;

  VolStatus = gBS->LocateHandleBuffer (
                     ByProtocol,
                     &gEfiSimpleFileSystemProtocolGuid,
                     NULL,
                     &HandleCount,
                     &Handles
                     );
  if (EFI_ERROR (VolStatus)) {
    Print (L"AcreetBoot: SimpleFileSystem scan failed (%r)\n", VolStatus);
    return NULL;
  }

  for (VolIdx = 0; (VolIdx < HandleCount) && !Found; VolIdx++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Sfs;

    VolStatus = gBS->HandleProtocol (
                      Handles[VolIdx],
                      &gEfiSimpleFileSystemProtocolGuid,
                      (VOID **)&Sfs
                      );
    if (EFI_ERROR (VolStatus) || (Sfs == NULL)) {
      continue;
    }
    Found = AcreetScanVolumeEntries (Sfs, &Entries, &EntryCount, &EntryCap);
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if (!Found) {
    if (Entries != NULL) {
      UINTN I;

      for (I = 0; I < EntryCount; I++) {
        AcreetFreeEntry (Entries[I]);
      }
      FreePool (Entries);
    }
    return NULL;
  }

  *Status   = EFI_SUCCESS;
  *CountOut = EntryCount;
  return Entries;
}

/* Free a parsed row array. */
STATIC
VOID
AcreetFreeMenu (
  AcreetMenuEntry **Entries,
  UINTN             Count
  )
{
  UINTN I;

  if (Entries == NULL) {
    return;
  }
  for (I = 0; I < Count; I++) {
    AcreetFreeEntry (Entries[I]);
  }
  FreePool (Entries);
}

/* --------------------------- entry point ---------------------------- */

EFI_STATUS
EFIAPI
AcreetMainMenuEntry (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS        Status;
  AcreetMenuEntry **Entries    = NULL;
  UINTN             EntryCount = 0;
  UINTN             Picked;
  CONST CHAR16     *BootPath;

  STATIC ACREET_TEXT_MENU_PROTOCOL mTextMenu = { AcreetTextMenuRun };
  EFI_HANDLE ProtoHandle = NULL;

  Print (L"AcreetBoot " ACREETBOOT_VERSION " (main menu, BSD-3)\n");

  /* Publish the shared text-picker protocol (edk2picker contract): other
   * chain modules locate + call Run through this interface instead of
   * linking the implementation. */
  Status = gBS->InstallProtocolInterface (
                  &ProtoHandle,
                  &gAcreetTextMenuProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mTextMenu
                  );
  if (EFI_ERROR (Status)) {
    Print (L"AcreetBoot: text-menu protocol install failed (%r)\n", Status);
    /* Non-fatal — the app owns and drives the TUI directly as well. */
  }

  Entries = LoadLoaderEntries (&EntryCount, &Status);

  if (EntryCount == 0) {
    /* 0.2 fallback contract, EXACTLY one row, SAME TUI: */
    AcreetTextMenuItem Fallback[1];
    AcreetTextMenuConfig FallbackCfg;

    Fallback[0].Title     = L"Payload: \\acreetboot\\boot\\vmtest.efi";
    Fallback[0].Default   = TRUE;
    Fallback[0].Separator = FALSE;

    FallbackCfg.Banner     = L"AcreetBoot " ACREETBOOT_VERSION;
    FallbackCfg.Hint       = L"AcreetionOS boot orchestrator (fallback row)";
    FallbackCfg.DefaultIdx = 0;
    FallbackCfg.TimeoutSec = MENU_TIMEOUT_FALLBACK_SEC;

    Picked = AcreetTextMenuRun (&FallbackCfg, Fallback, 1);
    if (Picked == ACREET_NONE) {
      /* Nothing picked (console-less or impossible menu state): hand
       * the reboot decision to the chain above, NEVER reset from here. */
      return EFI_ABORTED;
    }
    Print (L"Boot hand-off: LoadImage + StartImage of %s\n", ACREET_FALLBACK_PAYLOAD);
    Status = AcreetBootImage (ACREET_FALLBACK_PAYLOAD);
    Print (L"AcreetBoot: payload did not hand off (%r)\n", Status);
    return Status;
  }

  {
    AcreetTextMenuItem  *Rows;
    AcreetTextMenuConfig Cfg;
    UINTN                I;

    Rows = AllocatePool (EntryCount * sizeof (*Rows));
    if (Rows == NULL) {
      /* Menu OOM: no guessing, no silent payload reuse — bail to the
       * chain above with the truth in the status code. */
      AcreetFreeMenu (Entries, EntryCount);
      return EFI_OUT_OF_RESOURCES;
    }

    for (I = 0; I < EntryCount; I++) {
      Rows[I].Title     = Entries[I]->Title;
      Rows[I].Default   = FALSE;
      Rows[I].Separator = FALSE;
    }
    /* v0.3 default = first parsed row. The signed-manifest default is
     * acreetbootctl's contract: when it exists it writes '/acreetboot/
     * manifests' and the scanner honors it — nothing re-guessed here. */
    Rows[0].Default = TRUE;

    Cfg.Banner     = L"AcreetBoot " ACREETBOOT_VERSION;
    Cfg.Hint       = L"AcreetionOS boot orchestrator — feature/efi-menu";
    Cfg.DefaultIdx = 0;
    Cfg.TimeoutSec = MENU_TIMEOUT_SEC;

    Picked = AcreetTextMenuRun (&Cfg, Rows, EntryCount);
    FreePool (Rows);

    if (Picked == ACREET_NONE) {
      /* A populated menu declined to pick (Esc/aborted): no hardcoded
       * payload is EVER substituted — the fallback row exists only for
       * the zero-entries path, that distinction is the whole point. */
      AcreetFreeMenu (Entries, EntryCount);
      return EFI_ABORTED;
    }

    Print (L"Picked: %s\n", Entries[Picked]->Title);

    switch (Entries[Picked]->Kind) {
    case EntryKindEfi:
      BootPath = Entries[Picked]->EfiFile;
      Status   = AcreetBootImage (BootPath);
      break;

    case EntryKindLinux:
      /* EFI-stub/UKI kernels are PE32+ so LoadImage works today; raw
       * vmlinuz (non-stub kernels) is the documented LoadLinux lane. */
      BootPath = Entries[Picked]->Kernel;
      Status   = AcreetBootImage (BootPath);
      break;

    default:
      Print (L"AcreetBoot: kind %lu lane not wired yet (0.2.5 via issue #2)\n",
             (UINT64)Entries[Picked]->Kind);
      Status = EFI_UNSUPPORTED;
      break;
    }
  }

  AcreetFreeMenu (Entries, EntryCount);
  return Status;
}
