/** AcreetTextMenu.c — acreetboot text TUI menu (systemd-boot clone).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 *
 * See AcreetTextMenu.h for the behavior contract. Implementation notes:
 *   - Full-screen redraw on every selection/countdown change. At firmware
 *     speed and on serial consoles a full repaint is honest and cheap;
 *     diff-row painting is 0.4 polish, not pretend-nowhere.
 *   - The countdown ticks via a periodic timer event; the main loop
 *     WaitForEvent's on {key, timer} so an idle menu burns no CPU.
 *   - Count-down auto-boot targets the DEFAULT '*' row (sd-boot rule),
 *     NOT whatever the '>' selection currently looks at. Enter boots the
 *     '>' row. The two tracks never mix.
 *   - Titles are whatever SimpleTextOut renders (the loader-entries
 *     parser documents its non-ASCII range handling; not hidden here).
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "AcreetTextMenu.h"

EFI_GUID gAcreetTextMenuProtocolGuid = ACREET_TEXT_MENU_PROTOCOL_GUID;

#define MENU_INDENT     L"           "
#define MENU_LIVE_TITLE L"AcreetBoot Menu"
#define MENU_HELP_LINE  L"[t] Toggle timeout    [Esc] Wait forever    [Enter] Boot selection"

/* Attribute setter cub: attribute first, no return-value noise. */
#define MENU_SET_ATTRIBUTE(attr) \
  (gST->ConOut->SetAttribute (gST->ConOut, (attr)))

/* Countdown sentinel: wait forever (never auto-boot on its own). */
#define MENU_WAIT_FOREVER ((UINTN)-1)
/* "No valid selection" sentinel, same spirit as AcreetPick's contract. */
#define MENU_NO_SELECTION ((UINTN)-1)

STATIC
VOID
MenuHeader (
  AcreetTextMenuConfig *Cfg
  )
{
  Print (L"\n\n");
  if (Cfg->Banner != NULL) {
    Print (L"%s%s\n", MENU_INDENT, Cfg->Banner);
  }
  Print (L"%s%s\n", MENU_INDENT, MENU_LIVE_TITLE);
  if ((Cfg->Hint != NULL) && (Cfg->Hint[0] != L'\0')) {
    Print (L"%s%s\n", MENU_INDENT, Cfg->Hint);
  }
  Print (L"\n");
}

/* One full-frame render: header, rows, count, footer. SelIdx and the
 * countdown are the only moving parts. */
STATIC
VOID
MenuDraw (
  AcreetTextMenuConfig *Cfg,
  AcreetTextMenuItem   *Items,
  UINTN                 Count,
  UINTN                 SelIdx,
  BOOLEAN               CountingDown,
  UINTN                 SecondsLeft
  )
{
  UINTN Index;

  MENU_SET_ATTRIBUTE (EFI_LIGHTGRAY);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);

  MenuHeader (Cfg);

  for (Index = 0; Index < Count; Index++) {
    BOOLEAN IsSel  = (Index == SelIdx);
    BOOLEAN IsDflt = (CountingDown && Items[Index].Default &&
                      !Items[Index].Separator);

    if (Items[Index].Separator) {
      Print (L"%s  %s\n", MENU_INDENT, L"---");
      continue;
    }

    /* sd-boot coloring: live row (>) bright, '*' default marker yellow,
     * everything else muted. */
    if (IsSel) {
      MENU_SET_ATTRIBUTE (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_BLACK));
    } else if (IsDflt) {
      MENU_SET_ATTRIBUTE (EFI_TEXT_ATTR (EFI_YELLOW, EFI_BACKGROUND_BLACK));
    } else {
      MENU_SET_ATTRIBUTE (EFI_LIGHTGRAY);
    }
    Print (L"%s%s%s%s\n",
           MENU_INDENT,
           IsSel  ? L"> " : L"  ",
           IsDflt ? L"* " : L"  ",
           Items[Index].Title);
  }

  Print (L"\n");
  MENU_SET_ATTRIBUTE (EFI_LIGHTGRAY);
  Print (L"%sCount: %lu entries\n", MENU_INDENT, (UINT64)Count);

  if (SelIdx < Count) {
    if (CountingDown && (SecondsLeft != MENU_WAIT_FOREVER)) {
      Print (L"%sBooting '%s' in %lu s.\n",
             MENU_INDENT, Items[SelIdx].Title, (UINT64)SecondsLeft);
    } else {
      Print (L"%sBooting '%s' when Enter is pressed.\n",
             MENU_INDENT, Items[SelIdx].Title);
    }
  }
  Print (L"%s%s\n", MENU_INDENT, MENU_HELP_LINE);
  MENU_SET_ATTRIBUTE (EFI_LIGHTGRAY);
}

/* Live-selection move helper for the arrows: steps over separators and
 * never selects one; vertical-only in v0.3 (no wrap: sd-boot doesn't wrap
 * either). Returns TRUE if a real entry got selected or deselected. */
STATIC
BOOLEAN
MenuMove (
  AcreetTextMenuItem *Items,
  UINTN              *Sel,
  INTN                 Dir,  /* -1 up, +1 down */
  UINTN                Count
  )
{
  INTN S = (INTN)(*Sel);

  if (Count == 0) {
    return FALSE;
  }

  if (S == (INTN)MENU_NO_SELECTION) {
    /* From "nothing", Up lands on the last selectable row, Down on the
     * first; plain linear scans, no fancy wrap tricks. */
    UINTN Probe;
    UINTN StepCount;

    if (Dir < 0) {
      for (Probe = Count - 1, StepCount = 0;
           StepCount < Count;
           Probe--, StepCount++)
      {
        if (!Items[Probe].Separator) {
          *Sel = Probe;
          return TRUE;
        }
        if (Probe == 0) {
          break;
        }
      }
    } else {
      for (Probe = 0; Probe < Count; Probe++) {
        if (!Items[Probe].Separator) {
          *Sel = Probe;
          return TRUE;
        }
      }
    }
    return FALSE;
  }

  do {
    S += Dir;
    if (S < 0 || S >= (INTN)Count) {
      return FALSE; /* hit the edge: stay put, repaint is harmless */
    }
  } while (Items[S].Separator);

  *Sel = (UINTN)S;
  return TRUE;
}

/* Handles one keystroke. Return values:
 *   MENU_KEY_NAV        — repaint with the (possibly changed) state
 *   MENU_KEY_BOOT_SEL   — Enter on a valid selection: boot it now
 *   MENU_KEY_IGNORED    — unknown key: repaint nothing */
typedef enum {
  MENU_KEY_NAV,
  MENU_KEY_BOOT_SEL,
  MENU_KEY_IGNORED
} MENU_KEY_RESULT;

STATIC
MENU_KEY_RESULT
MenuHandleKey (
  EFI_INPUT_KEY       *Key,
  AcreetTextMenuItem  *Items,
  UINTN                Count,
  UINTN               *Sel,
  BOOLEAN             *CountingDown,
  UINTN               *SecondsLeft,
  UINTN                CfgDefaultSec
  )
{
  switch (Key->ScanCode) {
  case SCAN_UP:
  case SCAN_DOWN:
    MenuMove (Items, Sel, (Key->ScanCode == SCAN_UP) ? -1 : 1, Count);
    return MENU_KEY_NAV;

  case SCAN_ESC:
    *CountingDown = FALSE;
    *SecondsLeft = MENU_WAIT_FOREVER;
    return MENU_KEY_NAV;

  default:
    break;
  }

  switch (Key->UnicodeChar) {
  case CHAR_CARRIAGE_RETURN:
    return MENU_KEY_BOOT_SEL;

  case L't':
  case L'T':
    *CountingDown = !*CountingDown;
    /* Re-enabling restores the configured timeout (never an instant 0). */
    *SecondsLeft = *CountingDown ? CfgDefaultSec : MENU_WAIT_FOREVER;
    return MENU_KEY_NAV;

  default:
    return MENU_KEY_IGNORED;
  }
}

UINTN
AcreetTextMenuRun (
  AcreetTextMenuConfig *Cfg,
  AcreetTextMenuItem   *Items,
  UINTN                 Count
  )
{
  EFI_STATUS Status;
  UINTN      Sel;
  UINTN      DfltIdx;
  BOOLEAN    CountingDown;
  UINTN      SecondsLeft;
  EFI_EVENT  TimerEvt = NULL;
  UINTN      Result   = MENU_NO_SELECTION;
  UINTN      Index;

  /* A console-less menu is a black screen; refuse politely instead and
   * let the caller fall back to its direct hand-off. */
  if ((gST == NULL) || (gST->ConOut == NULL) || (gST->ConIn == NULL) ||
      (Cfg == NULL) || (Items == NULL) || (Count == 0))
  {
    return MENU_NO_SELECTION;
  }

  Sel        = (Cfg->DefaultIdx < Count) ? Cfg->DefaultIdx : MENU_NO_SELECTION;
  DfltIdx    = MENU_NO_SELECTION;
  for (Index = 0; Index < Count; Index++) {
    if (Items[Index].Default && !Items[Index].Separator) {
      DfltIdx = Index; /* LAST '*'-marked row wins, sd-boot-consistent */
      break;
    }
  }

  CountingDown = (Cfg->TimeoutSec != MENU_WAIT_FOREVER);
  if (CountingDown) {
    SecondsLeft = Cfg->TimeoutSec;
  } else {
    SecondsLeft = MENU_WAIT_FOREVER;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvt);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (TimerEvt, TimerPeriodic,
                            EFI_TIMER_PERIOD_SECONDS (1));
  }
  if (EFI_ERROR (Status)) {
    if (TimerEvt != NULL) {
      gBS->CloseEvent (TimerEvt);
      TimerEvt = NULL;
    }
    /* No timer = no countdown = interactive-only menu. */
  }

  MenuDraw (Cfg, Items, Count, Sel, CountingDown, SecondsLeft);

  for (;;) {
    EFI_EVENT Events[2];
    UINTN     EvCount = 0;
    UINTN     WaitIdx = 0;
    BOOLEAN   DfltBoot = (DfltIdx != MENU_NO_SELECTION);

    /* Timeout 0 with a default row = boot default now, no event loop. */
    if (CountingDown && DfltBoot && (Cfg->TimeoutSec == 0)) {
      Result = DfltIdx;
      break;
    }

    Events[EvCount++] = gST->ConIn->WaitForKey;
    if (TimerEvt != NULL) {
      Events[EvCount++] = TimerEvt;
    }

    Status = gBS->WaitForEvent (EvCount, Events, &WaitIdx);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (WaitIdx == 0) {
      EFI_INPUT_KEY    Key;
      EFI_STATUS       KStatus;

      KStatus = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (EFI_ERROR (KStatus)) {
        /* WaitForKey fired but the stroke vanished: repaint nothing,
         * re-wait. Never synthesize a phantom selection. */
        continue;
      }

      switch (MenuHandleKey (&Key, Items, Count, &Sel, &CountingDown,
                             &SecondsLeft, Cfg->TimeoutSec)) {
      case MENU_KEY_BOOT_SEL:
        if (Sel < Count) {
          Result = Sel;
        }
        goto ExitLoop;

      case MENU_KEY_NAV:
        MenuDraw (Cfg, Items, Count, Sel, CountingDown, SecondsLeft);
        break;

      case MENU_KEY_IGNORED:
      default:
        break;
      }
    } else {
      /* Timer tick. Countdown counts toward the DEFAULT row; pressing
       * arrows does not cancel the countdown (matching sd-boot). */
      if (CountingDown && (SecondsLeft != MENU_WAIT_FOREVER) && DfltBoot) {
        if (SecondsLeft == 0) {
          Result = DfltIdx;
          break;
        }
        SecondsLeft--;
        MenuDraw (Cfg, Items, Count, Sel, CountingDown, SecondsLeft);
      }
    }
  }

ExitLoop:

  if (TimerEvt != NULL) {
    gBS->SetTimer (TimerEvt, TimerCancel, 0);
    gBS->CloseEvent (TimerEvt);
  }

  return Result;
}
