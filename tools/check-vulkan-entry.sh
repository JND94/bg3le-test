#!/bin/bash
# Checks that the game's way of finding Vulkan (dlopen the loader, dlsym it for
# vkGetInstanceProcAddr) reaches bg3le however the loader was linked, and that
# bg3le's dlsym leaves every other lookup, RTLD_NEXT included, as it was.
# No game needed.
#
#   tools/check-vulkan-entry.sh [path/to/libbg3le.so]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIB="$(realpath "${1:-$ROOT/build/libbg3le.so}")"
CC="${CC:-cc}"
SRC="$ROOT/tools/vulkan-entry-check.c"
WORK="$ROOT/build/vulkan-entry-check"

rm -rf "$WORK"
mkdir -p "$WORK/plain" "$WORK/symbolic"

loader=(-shared -fPIC -O0 -fsemantic-interposition -DSTANDIN_LOADER -Wl,-soname,libvulkan.so.1)
"$CC" "${loader[@]}" "$SRC" -o "$WORK/plain/libvulkan.so.1"
# How Ubuntu links the real one: its own entry points are bound at link time.
"$CC" "${loader[@]}" -Wl,-Bsymbolic-functions "$SRC" -o "$WORK/symbolic/libvulkan.so.1"
"$CC" -shared -fPIC -DSTANDIN_LAYER "$SRC" -o "$WORK/standin-layer.so"
"$CC" -O1 -rdynamic "$SRC" -o "$WORK/probe" -ldl

# The stand-ins must differ as the real loaders do, or this proves nothing.
self_refs() { readelf -rW "$1" | grep -cE 'GLOB_DAT .* vkCreateInstance' || true; }
if [ "$(self_refs "$WORK/plain/libvulkan.so.1")" = 0 ] \
    || [ "$(self_refs "$WORK/symbolic/libvulkan.so.1")" != 0 ]; then
    echo "check-vulkan-entry: the stand-in loaders did not link as intended" >&2
    exit 1
fi

status=0
for kind in plain symbolic; do
    ln -s libvulkan.so.1 "$WORK/$kind/libvulkan.so"
    echo "== $kind loader"
    env LD_LIBRARY_PATH="$WORK/$kind" LD_PRELOAD="$LIB" BG3LE_LOG="$WORK/bg3le.log" \
        "$WORK/probe" "$WORK/standin-layer.so" || status=1
done
[ "$status" = 0 ] && echo "check-vulkan-entry: ok"
exit "$status"
