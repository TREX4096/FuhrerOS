# Real hardware (M18)

**Status: NOT RUN.** FuhrerOS has only been booted in QEMU/KVM. This page
is the prepared procedure and a prediction of what will and will not work.
The prediction is derived from the drivers that exist, and every item in it
is UNKNOWN - REQUIRES VERIFICATION until someone boots a real machine.

## Safety: the internal disk cannot be written

NEW_EXPLANATION M18 requires booting from a disposable medium and never
writing to the internal disk. With the current kernel this holds by
construction, not only by procedure:

- The only block-device driver is **virtio-blk**, which exists only in
  VMs. The kernel has no AHCI (SATA), NVMe or USB-storage driver, so it
  cannot see, let alone write, a laptop's internal disk.
- Without a virtio disk, the kernel mounts the initrd from the boot image
  as a read-only root (`kernel/core/subsys.c`), with `/tmp` in RAM. Nothing
  is persisted.
- `scripts/make-usb.sh` writes only to a device that the kernel reports as
  removable or USB-attached. It refuses devices with mounted partitions,
  asks you to type the device name, and verifies the written image.

## Procedure

1. `./scripts/build.sh`, then `./scripts/test.sh` must pass in QEMU. Then
   run `./scripts/test-power.sh poweroff` and `reboot`.
2. Write the stick with one of:
   - **Windows:** Rufus, "DD image" mode, with `fuhreros.iso` from the
     build directory (`\\wsl$\Ubuntu\home\<user>\.cache\fuhreros\build`);
   - **Linux / WSL:** attach the stick to WSL with `usbipd`, then run
     `./scripts/make-usb.sh /dev/sdX`.
3. Boot the target from USB (firmware boot menu). The ISO is hybrid: Limine
   BIOS and UEFI (`limine-uefi-cd.bin`) on a protective MBR. Secure Boot
   must be off, because the image is not signed.
4. Choose **FuhrerOS (text console)** first. It shows the boot log without
   the desktop. The self-test entry can also be used; it prints `TEST ...`
   lines on screen.
5. Record the result as an E-1xx experiment and any failure as an F-1xx
   record.

## Expected behaviour (prediction, UNVERIFIED)

| area | expectation | why |
|---|---|---|
| boot to banner | likely | Limine provides the framebuffer (GOP/VBE), memory map, RSDP; the kernel builds its own page tables |
| ACPI tables, LAPIC/IOAPIC | likely | parsed from the MADT |
| timer calibration | **risk** | the TSC and LAPIC timer are calibrated with PIT channel 2. Some recent machines disable or omit the 8254 PIT; there is no HPET/ACPI-PM-timer fallback yet |
| framebuffer console and desktop | likely, at the firmware's resolution | only a linear framebuffer is used; no GPU driver |
| keyboard / touchpad | **uncertain** | only PS/2 (i8042) is supported. Many laptops expose the internal keyboard/touchpad as PS/2 and some firmware emulates USB keyboards, but modern I²C-HID touchpads and USB keyboards need drivers that do not exist |
| storage | **no disk** | virtio-blk only (see Safety) |
| network | **no network** | virtio-net only; no e1000/Realtek/Intel Wi-Fi driver |
| poweroff / reboot | likely | ACPI S5 from the FADT + `\_S5_`, reset via the FADT reset register, 0xCF9 or the 8042 (verified in QEMU without the debug-exit device) |
| SMP | not used | uniprocessor kernel (D-105) |

## What would be needed for a useful real-hardware session

In order of value: AHCI or NVMe (read-only first), xHCI + USB HID, an
Intel/Realtek NIC, and I²C-HID for Precision Touchpads (UI_SUGGESTION §14).
None of these exists yet.
