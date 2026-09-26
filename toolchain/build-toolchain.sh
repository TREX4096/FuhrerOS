#!/bin/bash
# Build the FuhrerOS freestanding cross toolchain (x86_64-elf) into $PREFIX.
# No libc, no host headers: the kernel can never link against Linux.
#   PREFIX=~/opt/cross ./toolchain/build-toolchain.sh
set -euo pipefail
PREFIX=${PREFIX:-$HOME/opt/cross}
TARGET=x86_64-elf
BINUTILS=2.45
GCC=15.2.0
SRC=${SRC:-$HOME/src/fuhreros-toolchain}
JOBS=$(nproc)
mkdir -p "$SRC" "$PREFIX"
cd "$SRC"
fetch() { [ -f "$(basename "$1")" ] || wget -q "$1"; }
fetch https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS.tar.xz
fetch https://ftp.gnu.org/gnu/gcc/gcc-$GCC/gcc-$GCC.tar.xz
[ -d binutils-$BINUTILS ] || tar xf binutils-$BINUTILS.tar.xz
[ -d gcc-$GCC ] || tar xf gcc-$GCC.tar.xz

# libgcc without the red zone: kernel interrupts would clobber it (OSDev "Libgcc without red zone").
cat > gcc-$GCC/gcc/config/i386/t-x86_64-elf <<'MK'
MULTILIB_OPTIONS += mno-red-zone
MULTILIB_DIRNAMES += no-red-zone
MK
grep -q t-x86_64-elf gcc-$GCC/gcc/config.gcc || sed -i '/^x86_64-\*-elf\*)/a\	tmake_file="${tmake_file} i386/t-x86_64-elf"' gcc-$GCC/gcc/config.gcc

export PATH="$PREFIX/bin:$PATH"
if [ ! -x "$PREFIX/bin/$TARGET-ld" ]; then
	rm -rf build-binutils && mkdir build-binutils && cd build-binutils
	../binutils-$BINUTILS/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror >/dev/null
	make -j"$JOBS" >/dev/null && make install >/dev/null
	cd ..
fi
if [ ! -x "$PREFIX/bin/$TARGET-gcc" ]; then
	rm -rf build-gcc && mkdir build-gcc && cd build-gcc
	../gcc-$GCC/configure --target=$TARGET --prefix="$PREFIX" --disable-nls \
		--enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx >/dev/null
	make -j"$JOBS" all-gcc >/dev/null
	make -j"$JOBS" all-target-libgcc >/dev/null
	make install-gcc install-target-libgcc >/dev/null
	cd ..
fi
"$PREFIX/bin/$TARGET-gcc" --version | head -1
"$PREFIX/bin/$TARGET-gcc" -print-multi-lib
