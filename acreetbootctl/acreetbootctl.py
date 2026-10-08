#!/usr/bin/env python3
# AcreetBoot — setup / status CLI
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
"""acreetbootctl — provision or inspect the AcreetBoot boot chain on a host.

Subcommands (v0.2 target; v0.1 ships `status` and dry-run `plan`):
    status          show firmware boot entries + staged bits on the ESP
    plan            print the install plan without mutating anything
    install         stage payload, write NVRAM entries, embed manifest key
    set-bootnext    set UEFI BootNext to the AcreetBoot systemd-boot entry
    verify          re-run the payload manifest check from userspace
    uninstall       remove what acreetboot previously wrote (receipts kept)

Nothing here is executed at import time; everything runs only through
main() so the module is importable by unit tests.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ACREETBOOT_VERSION = "0.1.0"
GLOBAL_VAR_GUID = "8be4df61-93ea-11d0-8200-00c04fd430c8"
# Where the payload partition's label is expected:
PAYLOAD_LABEL = "ACREETBOOT-ZSWAP"
ESP_MAINMENU_RELPATH = "/EFI/acreetboot/AcreetBootMainMenu.efi"
ESP_SDBOOT_RELPATH = "/EFI/acreetboot/systemd-bootx64.efi"


@dataclass
class BootEntry:
    index: str
    title: str
    active: bool


def _run(args: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, **kw)


def detect_esp() -> Path | None:
    """Find ESP mount point via findmnt, /boot/efi, /efi, /boot."""
    r = _run(["findmnt", "-nro", "TARGET", "-t", "vfat",
              "--fstab", "--real", "-o", "TARGET,SOURCE"])
    if r.returncode == 0:
        for line in r.stdout.splitlines():
            path = line.split()[0]
            if Path(path, "EFI").is_dir():
                return Path(path)
    for cand in ("/boot/efi", "/efi", "/boot"):
        if Path(cand).is_mount() and Path(cand, "EFI").is_dir():
            return Path(cand)
    return None


def read_boot_entries() -> list[BootEntry]:
    r = _run(["efibootmgr"])
    entries: list[BootEntry] = []
    for line in r.stdout.splitlines():
        sl = line.strip()
        if sl.startswith("Boot") and "*" in sl or "*" not in sl and sl.startswith("Boot"):
            # format: Boot0001* Windows Boot Manager\tHD(1,...)/\EFI\...
            if sl.startswith("Boot") and len(sl) > 9:
                idx = sl[4:8]
                active = "*" in sl[8:9]
                title = sl.split(maxsplit=1)[1].split("\t")[0] if len(sl.split(maxsplit=1)) > 1 else ""
                entries.append(BootEntry(idx, title, active))
    return entries


def cmd_status(args: argparse.Namespace) -> int:
    esp = detect_esp()
    print(f"acreetboot {ACREETBOOT_VERSION} status")
    print(f"  ESP detected        : {esp or 'NOT FOUND'}")
    if esp:
        main_menu = esp / ESP_MAINMENU_RELPATH.lstrip("/")
        sd_boot = esp / ESP_SDBOOT_RELPATH.lstrip("/")
        print(f"  AcreetBootMainMenu  : {'present' if main_menu.is_file() else 'absent'}")
        print(f"  systemd-boot shim   : {'present' if sd_boot.is_file() else 'absent'}")
    print("  Firmware boot entries:")
    for e in read_boot_entries():
        star = "*" if e.active else " "
        print(f"    Boot{e.index}{star} {e.title}")
    r = _run(["efibootmgr"])
    next_boot = ""
    for line in r.stdout.splitlines():
        if line.startswith("BootNext:"):
            next_boot = line.split()[-1]
    print(f"  BootNext            : {next_boot or '(unset)'}")
    return 0


def cmd_plan(args: argparse.Namespace) -> int:
    esp = detect_esp()
    plan = {
        "version": ACREETBOOT_VERSION,
        "esp": str(esp) if esp else None,
        "will_write": [],
        "nvram_entries_to_add": [
            "AcreetBoot GRUB (hardened) -> \\EFI\\acreetboot\\grubx64.efi",
            "AcreetBoot systemd-boot     -> \\EFI\\acreetboot\\systemd-bootx64.efi",
        ],
        "manifest_policy": "strict unless acreetboot.recover=1",
        "payload_partition_label": PAYLOAD_LABEL,
    }
    if esp:
        plan["will_write"] += [
            str(esp / "EFI/acreetboot/"),
            str(esp / "loader/loader.conf"),
            str(esp / "loader/entries/10-acreetboot.conf"),
        ]
    print(json.dumps(plan, indent=2))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="acreetbootctl",
                                     description=__doc__.splitlines()[0])
    parser.add_argument("--version", action="version",
                        version=f"acreetboot {ACREETBOOT_VERSION}")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="show current boot chain state")
    sub.add_parser("plan", help="dry-run print of what install would do")
    args = parser.parse_args(argv)
    if args.command == "status":
        return cmd_status(args)
    if args.command == "plan":
        return cmd_plan(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
