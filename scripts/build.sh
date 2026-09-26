#!/bin/bash
# Build FuhrerOS: kernel (x86_64-elf toolchain), user space, disk image, ISO.
. "$(dirname "$0")/common.sh"
command -v x86_64-elf-gcc >/dev/null || die "cross toolchain missing: ./toolchain/build-toolchain.sh"
[ -d "$HOME/src/limine" ] || die "Limine missing: ./toolchain/get-limine.sh"
make -C "$REPO" -j"$(nproc)" B="$B" "$@"
echo "BUILD: PASS"
