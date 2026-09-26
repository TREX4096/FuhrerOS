#!/bin/bash
# Build FuhrerOS: compile + unit-test the adaptive layer, then assemble the
# guest root filesystem and qcow2 disk image — all inside Docker, so the host
# only needs docker and qemu. Nothing outside $FU_OUT is written.
#
#   ./scripts/build.sh                 base image (console, dev tools, text browsers)
#   FUHRER_PROFILE=desktop ./scripts/build.sh   + Xorg/openbox/Firefox
. "$(dirname "$0")/lib.sh"

command -v docker >/dev/null || fu_die "docker is required for the build"
docker info >/dev/null 2>&1 || fu_die "docker daemon not reachable (start Docker Desktop)"

REV=$(git -C "$FU_REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ -n "$(git -C "$FU_REPO" status --porcelain 2>/dev/null)" ]; then REV="$REV-dirty"; fi

fu_log "stage 1/2: compile + unit tests + builder image"
docker build -q -f "$FU_REPO/vm/image/Dockerfile" -t fuhreros-builder "$FU_REPO" >/dev/null

fu_log "stage 2/2: root filesystem + disk image (profile=$FU_PROFILE, disk=${DISK_GB}G)"
# 20G base; larger VM configs get a bigger overlay and grow the fs at boot.
docker run --rm \
	-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
	-e PROFILE="$FU_PROFILE" -e DISK_GB="${FUHRER_IMAGE_GB:-20}" -e GIT_REV="$REV" \
	-v "$FU_OUT:/out" fuhreros-builder

# The CoW overlay of an older base would be inconsistent; recreate on next run.
rm -f "$FU_OUT"/vm-*-"$FU_PROFILE".qcow2
fu_log "artifacts in $FU_OUT:"
ls -la "$FU_OUT" | grep -E "$FU_PROFILE" >&2
echo "BUILD: PASS"
