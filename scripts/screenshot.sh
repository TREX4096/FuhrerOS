#!/bin/bash
# Boot FuhrerOS headless (desktop entry), optionally type into it, and save
# PNG screenshots through the QEMU monitor.
#   ./scripts/screenshot.sh OUT_PREFIX [SECONDS_BEFORE_SHOT] [KEYS...]
# KEYS are QEMU `sendkey` names (e.g. meta_l ctrl-alt-t ret); each is sent,
# followed by a short pause, after the first screenshot.
. "$(dirname "$0")/common.sh"
OUT=${1:?usage: screenshot.sh OUT_PREFIX [SECONDS] [KEYS...]}
WAIT=${2:-20}
shift 2 || true
ISO=${FUHRER_ISO:-$B/fuhreros.iso}
cp --sparse=always "$B/disk.img" "$B/disk-shot.img"
detect_accel
MON=$B/shot.monitor
rm -f "$MON"
qemu_base "$ISO" "$B/disk-shot.img"
QEMU+=(-serial "file:$B/shot-serial.log" -display none -vga std -monitor "unix:$MON,server,nowait")
"${QEMU[@]}" &
QPID=$!
trap 'kill $QPID 2>/dev/null' EXIT
sleep "$WAIT"
shot() {
	rm -f "$B/shot.ppm"
	printf 'screendump %s\n' "$B/shot.ppm" | nc -q1 -U "$MON" >/dev/null 2>&1 || printf 'screendump %s\n' "$B/shot.ppm" | nc -N -U "$MON" >/dev/null 2>&1
	for _ in $(seq 1 20); do [ -s "$B/shot.ppm" ] && break; sleep 0.3; done
	python3 - "$B/shot.ppm" "$1" <<'PY'
import sys, zlib, struct
d = open(sys.argv[1], "rb").read()
parts, i = [], 0
while len(parts) < 4:
    while d[i:i+1].isspace(): i += 1
    j = i
    while not d[j:j+1].isspace(): j += 1
    parts.append(d[i:j]); i = j
i += 1
w, h = int(parts[1]), int(parts[2])
px = d[i:i + w*h*3]
raw = b"".join(b"\x00" + px[y*w*3:(y+1)*w*3] for y in range(h))
ch = lambda t, x: struct.pack(">I", len(x)) + t + x + struct.pack(">I", zlib.crc32(t + x) & 0xffffffff)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 6)) + ch(b"IEND", b""))
PY
	echo "$1"
}
shot "$OUT-0.png"
n=1
for k in "$@"; do
	if [[ $k == sleep:* ]]; then sleep "${k#sleep:}"; continue; fi
	if [[ $k == shot ]]; then shot "$OUT-$n.png"; n=$((n + 1)); continue; fi
	printf 'sendkey %s\n' "$k" | nc -q1 -U "$MON" >/dev/null 2>&1 || printf 'sendkey %s\n' "$k" | nc -N -U "$MON" >/dev/null 2>&1
	sleep 0.4
done
