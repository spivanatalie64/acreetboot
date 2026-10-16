/** AcreetTextMenu.h — acreetboot text TUI menu (systemd-boot clone) — the
 *                    "text picker protocol abstraction" (edk2picker).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 *
 * Canonical home: efi/edk2/AcreetBootPkg/AcreetTextMenu/ (compiled into
 * AcreetMainMenu via its INF [Sources]). The edk2picker/ directory at the
 * repo root holds the SPEC (edk2picker/README.md) and points here — this
 * is the one implementation the boot chain shares above the firmware:
 * miniboot recovery shells and AcreetMainMainMenu both speak this API.
 *
 * Behavior contract (clone of the systemd-boot interactive menu):
 *   - draws a plain-text menu on the UEFI console: banner, entry count,
 *     entry rows with a '*' marker on the DEFAULT row and a '>' highlight
 *     on the CURRENT row (the two are different, exactly like sd-boot);
 *   - Up / Down move the selection ('>' row), skipping separator rows;
 *   - Enter boots the current selection and RETURNS its index;
 *   - 't' toggles the countdown on/off; Esc = wait forever (never boots
 *     on its own — an operator is at the console by definition);
 *   - the countdown auto-boots the DEFAULT entry at zero;
 *   - no editor, no shell, no gRT->ResetSystem: on "nothing selected"
 *     returns (UINTN)-1 and the CALLER decides what happens next.
 *
 * Anti-open-limitation honesty: this draws with SimpleTextOut only. A
 * pixel-accurate GUI (OpenCanopy-like external picker) is a sibling
 * module later (0.4+), not a pretend "later inside this file".
 */

#ifndef ACREET_TEXT_MENU_H_
#define ACREET_TEXT_MENU_H_

#include <Uefi.h>

/* Defined (one instance) in AcreetTextMenu.c: */
extern EFI_GUID gAcreetTextMenuProtocolGuid;

/* Protocol abstraction: other modules (miniboot recovery, future GUI
 * picker) locate this protocol and call Run through the installed
 * function pointer instead of linking the .c directly. */
#define ACREET_TEXT_MENU_PROTOCOL_GUID \
  { 0x7b578f4c, 0x1a1e, 0x4c98, { 0x9f, 0x63, 0x2e, 0x41, 0xda, 0x0c, 0x91, 0x6f } }

typedef struct {
  CHAR16  *Title;      /* displayed row text */
  BOOLEAN Default;     /* '*' marker: auto-boot target of the countdown */
  BOOLEAN Separator;   /* rendered as a blank rule: non-selectable */
} AcreetTextMenuItem;

typedef struct {
  CHAR16  *Banner;     /* e.g. L"AcreetBoot 0.3.0 (BSD-3)" */
  CHAR16  *Hint;       /* second banner line, e.g. sub-title */
  UINTN   DefaultIdx;  /* initial '>' selection; (UINTN)-1 = none */
  UINTN   TimeoutSec;  /* 0: boot default instantly; (UINTN)-1: wait forever */
} AcreetTextMenuConfig;

typedef struct _ACREET_TEXT_MENU_PROTOCOL {
  UINTN (*Run)(
    AcreetTextMenuConfig *Cfg,
    AcreetTextMenuItem   *Items,
    UINTN                 Count
    );
} ACREET_TEXT_MENU_PROTOCOL;

/* The one exported call. Returns the index the operator (or the countdown)
 * picked; (UINTN)-1 = nothing picked (caller keeps the chain alive). */
UINTN
AcreetTextMenuRun (
  AcreetTextMenuConfig *Cfg,
  AcreetTextMenuItem   *Items,
  UINTN                 Count
  );

#endif /* ACREET_TEXT_MENU_H_ */
