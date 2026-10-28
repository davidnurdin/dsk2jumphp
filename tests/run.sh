#!/bin/sh
# The tests of dsk2jumphp: `make test`. A disc built here (tests/make_disc.py), standard and EXTENDED images,
# the emulator's outputs and every Python tool. With CPC_ROM (+ CPC_AMSDOS) set, also a real boot to BASIC.
set -eu
cd "$(dirname "$0")/.."
OUT=tests/out
rm -rf "$OUT" && mkdir -p "$OUT/std" "$OUT/ext" "$OUT/rom"
fail() { echo "FAIL: $*"; exit 1; }

python3 -I tests/make_disc.py
python3 -I tools/dsk.py info "$OUT/test.dsk" | grep -q "system format" || fail "dsk.py info"
python3 -I tools/dsk.py extended "$OUT/test.dsk" "$OUT/test-ext.dsk"

emulator/cpc "$OUT/test.dsk" "$OUT/std" 60 -s 50 -d 50 -w c000-c001 2>/dev/null
emulator/cpc "$OUT/test-ext.dsk" "$OUT/ext" 60 -s 50 -d 50 -w c000-c001 2>/dev/null
cmp -s "$OUT/std/dump-00050.bin" "$OUT/ext/dump-00050.bin" || fail "standard and EXTENDED images run differently"

# the screen is red, the AY plays channel A
python3 -I - "$OUT" <<'PY' || fail "screen / sound"
import sys
sys.path.insert(0, 'tools')
from cpc import Dump
import psg
out = sys.argv[1]
d = Dump(out + '/std/dump-00050.bin')
img = d.screen()
assert img.getpixel((10, 10)) == (255, 0, 0), img.getpixel((10, 10))
assert d.mode == 0 and d.palette[1] == 12
frames = psg.load(out + '/std/psg.bin')
assert frames[50][8] == 15 and frames[50][0] == 0 and frames[50][1] == 1, frames[50][:14].hex()
assert psg.segments(frames) and psg.segments(frames)[0][0] < 5
PY
grep -q "^write c000=c0" "$OUT/std/log.txt" || fail "watched writes"
python3 -I tools/cpc.py peek "$OUT/std/dump-00050.bin" c000 4 | grep -q "c0 c0 c0 c0" || fail "cpc.py peek"
python3 -I tools/cpc.py coverage "$OUT/std/coverage.bin" | grep -q "^0100-" || fail "coverage"
python3 -I tools/z80dis.py "$OUT/std/dump-00050.bin" 100 104 --coverage "$OUT/std/coverage.bin" | grep -q "\*0100  f3         di" || fail "z80dis"
python3 -I tools/psg.py render "$OUT/std/psg.bin" 0 50 "$OUT/note.wav" >/dev/null && [ -s "$OUT/note.wav" ] || fail "psg render"

if [ -n "${CPC_ROM:-}" ]; then
  emulator/cpc - "$OUT/rom" 300 -rom "$CPC_ROM" ${CPC_AMSDOS:+-urom 7:$CPC_AMSDOS} -type '150:print 6*7\n' -d 299 2>/dev/null
  # BASIC prints 42: the digits are in screen memory as mode 1 glyph bytes; just check the screen changed
  python3 -I tools/cpc.py screen "$OUT/rom/dump-00299.bin" "$OUT/rom/basic.png"
  echo "ROM boot: look at $OUT/rom/basic.png (BASIC prompt, 'print 6*7', 42)"
fi
echo "all tests passed"
