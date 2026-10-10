#!/usr/bin/env bash
# Build the UML microboot test kernel
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${CVM_UML_WORK:-/srv/cvm/uml}"
KREL=6.12
SRCDIR="$WORK/linux-$KREL"
OUT="$WORK/uml-microboot"
mkdir -p "$WORK" "$OUT"

cd "$WORK"
if [ ! -d "$SRCDIR/.git" ]; then
  git clone --depth 1 --branch "linux-$KREL.y" \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
      "$SRCDIR"
fi
cd "$SRCDIR"
make distclean >/dev/null 2>&1 || true
make ARCH=um defconfig
FRAG=""
for cand in "$ROOT"/linux-vmscript/uml/kernel-config.uml.frag \
            "$ROOT"/uml/kernel-config.uml.frag \
            "$ROOT"/src/kernel-config.uml.frag \
            "$ROOT"/kernel-config.uml.frag; do
  [ -f "$cand" ] && FRAG="$cand" && break
done
[ -n "$FRAG" ] || { echo "UML config fragment not found (looked in $ROOT)"; exit 4; }
scripts/kconfig/merge_config.sh -m .config "$FRAG"
yes "" | make ARCH=um olddefconfig
JOBS="${JOBS:-$(nproc)}"
if make -j"$JOBS" ARCH=um >"$OUT/build.log" 2>&1; then
  :  # output binary is ./linux
else
  tail -40 "$OUT/build.log" >&2
  exit 1
fi
cp ./linux "$OUT/uml-microboot"
"$OUT/uml-microboot" --help >/dev/null 2>&1 || true
echo "built: $OUT/uml-microboot ($(stat -c '%s' "$OUT/uml-microboot") bytes)"
