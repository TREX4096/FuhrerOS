# FuhrerOS build (run inside WSL/Linux; see docs/development-environment.md).
#
#   make            kernel + user space + disk image + bootable ISO
#   make iso        bootable ISO (BIOS + UEFI, Limine)
#   make run        boot in QEMU (scripts/run.sh)
#   make test       self-test boot (scripts/test.sh)
#   make clean
#
# Only the freestanding x86_64-elf toolchain is used for the kernel and user
# programs; the host compiler builds host tools (mkffs0) only.

CROSS    ?= $(HOME)/opt/cross/bin/x86_64-elf-
CC       := $(CROSS)gcc
CXX      := $(CROSS)g++
LD       := $(CROSS)ld
NM       := $(CROSS)nm
OBJCOPY  := $(CROSS)objcopy
HOSTCC   ?= cc
LIMINE_DIR ?= $(HOME)/src/limine

# Build outputs go to the WSL filesystem when the checkout lives on a Windows
# drive (/mnt/*): NTFS stores the sparse 20 GiB disk image fully allocated
# and is slow for VM disk I/O (D-106).
B ?= $(if $(filter /mnt/%,$(CURDIR)),$(HOME)/.cache/fuhreros/build,build)
K := kernel

# --- kernel flags -----------------------------------------------------------
KCOMMON := -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie \
	-mcmodel=kernel -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 \
	-fno-omit-frame-pointer -fno-strict-aliasing -O2 -g -Wall -Wextra \
	-Wno-unused-parameter -Werror=implicit-function-declaration \
	-I$(K)/include -I$(K) -MMD -MP
KCFLAGS   := $(KCOMMON) -std=gnu11
KCXXFLAGS := $(KCOMMON) -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-use-cxa-atexit -nostdinc++
KASFLAGS  := -g -I$(K)/include -I$(K)
KLDFLAGS  := -nostdlib -static -z max-page-size=0x1000 -T $(K)/linker.ld \
	--no-dynamic-linker -z noexecstack
LIBGCC    := $(shell $(CC) -mno-red-zone -print-libgcc-file-name 2>/dev/null)

KSRC_C   := $(shell find $(K) -name '*.c' | sort)
KSRC_CXX := $(shell find $(K) -name '*.cpp' | sort)
KSRC_S   := $(shell find $(K) -name '*.S' | sort)
KOBJ     := $(KSRC_C:%.c=$(B)/%.o) $(KSRC_CXX:%.cpp=$(B)/%.o) $(KSRC_S:%.S=$(B)/%.o)

KERNEL   := $(B)/fuhreros.elf
ISO      := $(B)/fuhreros.iso
ISO_TEST := $(B)/fuhreros-test.iso

.PHONY: all kernel iso iso-test run test clean user disk
all: iso

kernel: $(KERNEL)

$(B)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(KCFLAGS) -c $< -o $@

$(B)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX     $<"
	@$(CXX) $(KCXXFLAGS) -c $< -o $@

$(B)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(KASFLAGS) -c $< -o $@

# Two-pass link: pass 1 without symbols, generate the symbol table, pass 2
# with it. ksyms live in .rodata after .text, so text addresses are stable.
$(KERNEL): $(KOBJ) $(K)/linker.ld scripts/gen-ksyms.sh
	@echo "  LD      $@ (pass 1)"
	@$(LD) $(KLDFLAGS) $(KOBJ) -o $(B)/fuhreros.pass1.elf
	@scripts/gen-ksyms.sh $(NM) $(B)/fuhreros.pass1.elf > $(B)/ksyms.S
	@$(CC) $(KASFLAGS) -c $(B)/ksyms.S -o $(B)/ksyms.o
	@echo "  LD      $@ (pass 2, $$(grep -c '\.quad' $(B)/ksyms.S) symbols)"
	@$(LD) $(KLDFLAGS) $(KOBJ) $(B)/ksyms.o -o $@
	@scripts/check-kernel.sh $@

# --- user space --------------------------------------------------------------
# Freestanding like the kernel, but ring 3: no kernel code model, no red-zone
# restriction. FPU/SSE state is not saved on context switch yet, so user code
# is general-registers-only as well (D-104).
UCFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mgeneral-regs-only \
	-mno-mmx -mno-sse -fno-omit-frame-pointer -O2 -g -std=gnu11 -Wall -Wextra \
	-Wno-unused-parameter -Werror=implicit-function-declaration -Iuser/include -MMD -MP
ULDFLAGS := -nostdlib -static -T user/user.ld -z max-page-size=0x1000 --no-dynamic-linker

ULIB_OBJ := $(B)/user/lib/crt0.o $(B)/user/lib/libfu.o $(B)/user/lib/string.o \
	$(B)/user/lib/printf.o $(B)/user/lib/font8x16.o \
	$(patsubst user/lib/%.c,$(B)/user/lib/%.o,$(filter-out user/lib/libfu.c,$(wildcard user/lib/*.c)))
UPROGS   := $(patsubst user/bin/%.c,$(B)/rootfs/bin/%,$(wildcard user/bin/*.c)) \
	$(patsubst user/apps/%.c,$(B)/rootfs/bin/%,$(wildcard user/apps/*.c))
ROOTFS_FILES := $(patsubst rootfs/%,$(B)/rootfs/%,$(shell find rootfs -type f 2>/dev/null))

$(B)/user/lib/crt0.o: user/lib/crt0.S
	@mkdir -p $(dir $@)
	@$(CC) -c $< -o $@
$(B)/user/lib/%.o: user/lib/%.c
	@mkdir -p $(dir $@)
	@echo "  UCC     $<"
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/user/lib/string.o: kernel/lib/string.c
	@mkdir -p $(dir $@)
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/user/lib/font8x16.o: kernel/gfx/font8x16.c
	@mkdir -p $(dir $@)
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/user/lib/printf.o: kernel/lib/printf.c
	@mkdir -p $(dir $@)
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/user/bin/%.o: user/bin/%.c
	@mkdir -p $(dir $@)
	@echo "  UCC     $<"
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/user/apps/%.o: user/apps/%.c
	@mkdir -p $(dir $@)
	@echo "  UCC     $<"
	@$(CC) $(UCFLAGS) -c $< -o $@
$(B)/rootfs/bin/%: $(B)/user/bin/%.o $(ULIB_OBJ) user/user.ld
	@mkdir -p $(dir $@)
	@$(LD) $(ULDFLAGS) $< $(ULIB_OBJ) $(LIBGCC) -o $@
$(B)/rootfs/bin/%: $(B)/user/apps/%.o $(ULIB_OBJ) user/user.ld
	@mkdir -p $(dir $@)
	@$(LD) $(ULDFLAGS) $< $(ULIB_OBJ) $(LIBGCC) -o $@
$(B)/rootfs/%: rootfs/%
	@mkdir -p $(dir $@)
	@cp $< $@

user: $(UPROGS) $(ROOTFS_FILES)

# The initrd is a plain ustar archive of the staged root filesystem.
$(B)/initrd.tar: $(UPROGS) $(ROOTFS_FILES)
	@echo "  TAR     $@"
	@mkdir -p $(B)/rootfs/home $(B)/rootfs/etc
	@tar --format=ustar --owner=0 --group=0 -C $(B)/rootfs -cf $@ .

# --- FFS0 root disk -----------------------------------------------------------
# The pristine image is rebuilt whenever the staged root filesystem changes;
# scripts/run.sh boots a persistent copy (build/vm-disk.img) of it.
FFS0_MB ?= 20480
$(B)/tools/mkffs0: tools/mkffs0.c kernel/include/ffs0_format.h
	@mkdir -p $(dir $@)
	@echo "  HOSTCC  $<"
	@$(HOSTCC) -O2 -Wall -o $@ $<

$(B)/disk.img: $(B)/tools/mkffs0 $(UPROGS) $(ROOTFS_FILES)
	@echo "  FFS0    $@ ($(FFS0_MB) MiB, sparse)"
	@mkdir -p $(B)/rootfs/home $(B)/rootfs/etc $(B)/rootfs/tmp
	@rm -f $@ && $(B)/tools/mkffs0 $@ $(FFS0_MB) $(B)/rootfs fuhreros >/dev/null

disk: $(B)/disk.img
all: disk

# --- bootable ISO (Limine BIOS + UEFI) -------------------------------------
ISO_DEPS := $(KERNEL) boot/limine.conf $(B)/initrd.tar
$(B)/iso-%/stamp: $(ISO_DEPS)
	@rm -rf $(dir $@) && mkdir -p $(dir $@)boot/limine $(dir $@)EFI/BOOT
	@cp $(KERNEL) $(dir $@)boot/fuhreros.elf
	@cp $(B)/initrd.tar $(dir $@)boot/initrd.tar
	@sed -e 's/@CMDLINE@/$(if $(filter test,$*),test usertest,)/' \
	     -e 's/@TIMEOUT@/$(if $(filter test,$*),0,3)/' boot/limine.conf > $(dir $@)boot/limine/limine.conf
	@cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	    $(LIMINE_DIR)/limine-uefi-cd.bin $(dir $@)boot/limine/
	@cp $(LIMINE_DIR)/BOOTX64.EFI $(dir $@)EFI/BOOT/
	@touch $@

$(ISO): $(B)/iso-main/stamp
	@echo "  ISO     $@"
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label $(B)/iso-main -o $@ 2>/dev/null
	@$(LIMINE_DIR)/limine bios-install $@ 2>/dev/null

$(ISO_TEST): $(B)/iso-test/stamp
	@echo "  ISO     $@ (self-test)"
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label $(B)/iso-test -o $@ 2>/dev/null
	@$(LIMINE_DIR)/limine bios-install $@ 2>/dev/null

iso: $(ISO) $(ISO_TEST)

run: iso
	scripts/run.sh

test: iso
	scripts/test.sh

clean:
	rm -rf $(B)

-include $(shell find $(B) -name '*.d' 2>/dev/null)

.SECONDARY:
