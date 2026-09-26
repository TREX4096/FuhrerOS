#!/bin/bash
# Fetch the Limine binary release (bootloader is not part of FuhrerOS itself).
set -euo pipefail
DEST=${LIMINE_DIR:-$HOME/src/limine}
BRANCH=${LIMINE_BRANCH:-v11.x-binary}
[ -d "$DEST" ] || git clone --depth=1 --branch="$BRANCH" https://github.com/Limine-Bootloader/limine.git "$DEST"
make -C "$DEST" >/dev/null
"$DEST/limine" --version | head -1
