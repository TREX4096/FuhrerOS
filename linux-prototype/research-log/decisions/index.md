# Decision index (details: docs/decisions.md)

| id | decision | status |
|---|---|---|
| D-001 | Linux base, not a new kernel; hobby bootloader kept in legacy/ | adopted |
| D-002 | Alpine 3.22 + linux-virt 6.12 guest | adopted |
| D-003 | Docker build, mke2fs -d, CoW overlays, artifacts on native fs | adopted |
| D-004 | 1 s sampling interval | supported by E-002 (0.09 % CPU) |
| D-005 | hysteresis 3 + dwell 3 s | adopted, ablation A7 |
| D-006 | class → policy map | rev. 2 after E-002 (seq→SPECIALIZED, cpu→NORMAL); E-003 validates |
| D-007 | policy = reversible knob bundle + libfuhrer hint | adopted |
| D-008 | classifier features / thresholds | thresholds provisional |
| D-009 | one system-wide policy | adopted for prototype |
| D-010 | KVM if accessible else TCG | adopted |
| D-011 | benchmark methodology | adopted |
| D-012 | seqlocked shm policy publication | adopted |
| D-013 | dev-VM credentials / autologin | development only |
| D-014 | switch cost dominated by elevator change | recorded |
| D-015 | self-booting GPT disk (GRUB BIOS+UEFI), optional LTS kernel | adopted; BIOS+UEFI boot PASS in QEMU |
