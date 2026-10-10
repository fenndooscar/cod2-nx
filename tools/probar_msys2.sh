#!/usr/bin/env bash
# cod2 - probe the MSYS2 environment the Mesa/NVK build needs.
# Prints MSYSTEM and whether each tool build-unified.sh checks for is reachable.
echo "MSYSTEM=$MSYSTEM"
for t in meson ninja python sed git gcc clang rustc; do
    printf '%s: ' "$t"
    command -v "$t" || echo "NO"
done
echo "MESA_SWITCH_RUSTC=$MESA_SWITCH_RUSTC"
echo "--- rustup targets ---"
rustc --print target-list 2>/dev/null | grep -c aarch64-unknown-linux-gnu
