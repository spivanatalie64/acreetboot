#!/usr/bin/env bash
# Unit test: setbootnext against a FAKE efivarfs directory.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
BIN="${ACREETBOOT_TEST_BIN:-../build/bin/setbootnext}"
[ -x "$BIN" ] || { echo "setbootnext binary not built yet"; exit 77; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
touch "$tmp/Nada-8be4df61-93ea-11d0-8200-00c04fd430c8"
"$BIN" --efivars "$tmp" 0002
x="$(xxd -p "$tmp/BootNext-8be4df61-93ea-11d0-8200-00c04fd430c8")"
[ "$x" = "070000000200" ] || { echo "unexpected: $x"; exit 1; }
echo "setbootnext wrote BootNext=0002 correctly"
