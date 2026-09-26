#!/bin/bash
# Save a PNG screenshot of the running headless VM's display.
#   FUHRER_PROFILE=desktop ./scripts/screenshot.sh [out.png]
. "$(dirname "$0")/lib.sh"
OUTPNG=${1:-$FU_OUT/screenshot.png}
fu_vm_running || fu_die "no running headless VM"
PPM=$FU_OUT/screenshot.ppm
rm -f "$PPM"
fu_monitor "screendump $PPM"
for _ in $(seq 1 20); do [ -s "$PPM" ] && break; sleep 0.5; done
[ -s "$PPM" ] || fu_die "screendump failed"
python3 - "$PPM" "$OUTPNG" <<'PY'
import sys, zlib, struct
data = open(sys.argv[1], "rb").read()
parts, i = [], 0
while len(parts) < 4:              # P6 width height maxval
    while data[i:i+1].isspace(): i += 1
    if data[i:i+1] == b"#":
        i = data.index(b"\n", i) + 1; continue
    j = i
    while not data[j:j+1].isspace(): j += 1
    parts.append(data[i:j]); i = j
i += 1
w, h = int(parts[1]), int(parts[2])
px = data[i:i + w * h * 3]
raw = b"".join(b"\x00" + px[y*w*3:(y+1)*w*3] for y in range(h))
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
PY
echo "$OUTPNG"
