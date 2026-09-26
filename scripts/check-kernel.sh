#!/bin/sh
# Guard rails: the kernel must be a static, freestanding x86-64 ELF with no
# interpreter, no dynamic section and no undefined symbols (i.e. nothing that
# could have come from the host Linux runtime).
ELF=$1
NM=${NM:-$HOME/opt/cross/bin/x86_64-elf-nm}
READELF=${READELF:-$HOME/opt/cross/bin/x86_64-elf-readelf}
fail() { echo "check-kernel: $*" >&2; exit 1; }
$READELF -h "$ELF" | grep -q 'Machine:.*X86-64' || fail "not an x86-64 ELF"
$READELF -h "$ELF" | grep -q 'Type:.*EXEC' || fail "not a static executable"
$READELF -l "$ELF" | grep -q INTERP && fail "has a dynamic interpreter"
$READELF -d "$ELF" 2>/dev/null | grep -q 'Dynamic section' && fail "has a dynamic section"
U=$($NM -u "$ELF")
[ -z "$U" ] || fail "undefined symbols: $U"
$READELF -p .comment "$ELF" 2>/dev/null | grep -qi 'linux' && fail "linked with Linux-targeted objects"
exit 0
