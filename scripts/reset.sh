#!/bin/bash
# Discard the VM's persistent disk; the next run starts from the pristine image.
. "$(dirname "$0")/common.sh"
rm -f "$B/vm-disk.img" "$B/disk-test.img"
echo "VM disk reset (next boot copies build/disk.img)"
