# edk2picker — the text picker protocol abstraction

Spec home of the "shared text picker" for everything acreetboot puts
**above the firmware**: `AcreetMainMenu.efi`, the miniboot recovery
picker, any future pixel-GUI picker (OpenCanopy-shaped sibling).

**Canonical implementation:**
`efi/edk2/AcreetBootPkg/AcreetTextMenu/AcreetTextMenu.{h,c}`
(compiled into AcreetMainMenu via its INF `[Sources]`; not vendored
again — one implementation, many consumers, like every house source).

## API contract (v0.3)

```c
typedef struct { CHAR16 *Title; BOOLEAN Default; BOOLEAN Separator; } AcreetTextMenuItem;
typedef struct { CHAR16 *Banner; CHAR16 *Hint; UINTN DefaultIdx; UINTN TimeoutSec;
} AcreetTextMenuConfig;

UINTN AcreetTextMenuRun (Cfg, Items, Count);
```

Returns the picked index, or `(UINTN)-1` ("nothing picked — caller keeps
the chain alive, never resets").

## Rendered look (systemd-boot clone, 11-space silhouette)

```
           AcreetBoot 0.3.0
           AcreetBoot Menu
           (hint line)

           > * Debian GNU/Linux trixie        <- live + default
             * Debian GNU/Linux trixie        <- default, not selected
               Windows via CellBoot
             ---

           Count: 4 entries
           Booting 'Debian GNU/Linux trixie' in 8 s.
           [t] Toggle timeout    [Esc] Wait forever    [Enter] Boot selection
           v AcreetBoot 0.3.0 (BSD-3)
```

- `>` is the LIVE row (arrows moved it); `*` is the COUNTDOWN's boot
  target. Two different things, exactly sd-boot.
- Colors: live row = white; `*` = yellow; rest muted.
- `Count: N entries` so the audit trail is never ghost-hollow.

## Keybindings

| Key | Meaning (sd-boot-consistent) |
|-----|-------------------------------|
| Up / Down | move live row over real entries; separators are stones, they block, never "wrap" |
| Enter | boot the live row NOW (caller's job: LoadImage + StartImage) |
| t / T | toggles the countdown off/on; re-enabling restores the configured timeout (never instant-fire at 0) |
| Esc | wait-indefinitely (no countdown, and Esc alone never boots — an operator is at the console) |

## Countdown semantics

- Auto-boot target is the `*` DEFAULT row, **not** the live row. Arrow
  play does not cancel the countdown (sd-boot rule).
- Timeout `0` + a default = fire immediately, no event loop.
- No menu countdown ever times into an AUXILIARY/hidden row and never
  into a separator-only matrix; with zero selectable rows the menu
  returns `(UINTN)-1` immediately.

## UEFI protocol

Consumers that do not link the `.c` locate the protocol:

```
gAcreetTextMenuProtocolGuid =
  { 0x7b578f4c, 0x1a1e, 0x4c98, { 0x9f, 0x63, 0x2e, 0x41, 0xda, 0x0c, 0x91, 0x6f } }
```

struct `ACREET_TEXT_MENU_PROTOCOL { UINTN (*Run)(...) };` — see the header.

## Non-goals (v0.3)

- No editor, no shelllet, no `gRT->ResetSystem` anywhere: an aborted menu
  exits to the previous bootloader context, it does not own the machine.
- No pixel/GUI rendering — SimpleTextOut only; the GUI picker is a
  separate module later (0.4+), never pretended inside this one.
