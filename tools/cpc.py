"""Amstrad CPC helpers, and a command line to look inside the emulator's memory dumps.

    python3 -I tools/cpc.py screen  dump.bin out.png            the screen the CRTC shows (mode, palette per line)
    python3 -I tools/cpc.py palette dump.bin                    the 16 pens: hardware colour numbers and RGB
    python3 -I tools/cpc.py peek    dump.bin ADDR [LEN]         hex dump (64 KB as the CPU saw it at dump time)
    python3 -I tools/cpc.py find    dump.bin HEXBYTES           every address of a byte string ("c9 3e ff")
    python3 -I tools/cpc.py xref    dump.bin ADDR               little-endian references to ADDR (ld hl,ADDR...)
    python3 -I tools/cpc.py text    dump.bin "GAME OVER"        relative search: finds a text in ANY charset
    python3 -I tools/cpc.py gfx     dump.bin ADDR W H N out.png [--mode 0] [--zigzag] [--scale 4]
                                                                N graphics of W bytes x H lines from ADDR
    python3 -I tools/cpc.py tiles   dump.bin ADDR COUNT out.png [--w 2] [--h 8] [--mode 0] [--per-row 16]
    python3 -I tools/cpc.py coverage coverage.bin               the address ranges that were executed
    python3 -I tools/cpc.py diff    a.bin b.bin [--from 3 --to 2] [--range c000-ffff]
                                                                bytes that changed between two dumps; with
                                                                --from / --to only those that went from one value
                                                                to the other (finds lives, level, score...)

ADDR is hex (#c000, &c000, 0xc000 or c000). Dumps come from `cpc ... -d FRAME` (see docs/emulator.md):
128 KB of RAM (8 banks of 16 KB), 64 bytes of state, then the palette and mode of each of the 312 scanlines.
"""
import os
import struct
import sys

# CPC hardware colour number (0-31) -> RGB, as the gate array makes them
HW = [(128, 128, 128), (128, 128, 128), (0, 255, 128), (255, 255, 128), (0, 0, 128), (255, 0, 128), (0, 128, 128),
      (255, 128, 128), (255, 0, 128), (255, 255, 128), (255, 255, 0), (255, 255, 255), (255, 0, 0), (255, 0, 255),
      (255, 128, 0), (255, 128, 255), (0, 0, 128), (0, 255, 128), (0, 255, 0), (0, 255, 255), (0, 0, 0), (0, 0, 255),
      (0, 128, 0), (0, 128, 255), (128, 0, 128), (128, 255, 128), (128, 255, 0), (128, 255, 255), (128, 0, 0),
      (128, 0, 255), (128, 128, 0), (128, 128, 255)]
# firmware colour (BASIC INK 0-26) -> hardware colour number
FIRMWARE = [20, 4, 21, 28, 24, 29, 12, 5, 13, 22, 6, 23, 30, 0, 31, 14, 7, 15, 18, 2, 19, 26, 25, 27, 10, 3, 11]


def mode0_pixels(b):
    """The 2 pens (0-15) of a mode 0 byte, left first."""
    return (((b >> 7) & 1) | ((b >> 2) & 2) | ((b >> 3) & 4) | ((b << 2) & 8),
            ((b >> 6) & 1) | ((b >> 1) & 2) | ((b >> 2) & 4) | ((b << 3) & 8))


def mode1_pixels(b):
    """The 4 pens (0-3) of a mode 1 byte."""
    return [((b >> (7 - i)) & 1) | (((b >> (3 - i)) & 1) << 1) for i in range(4)]


def mode2_pixels(b):
    return [(b >> (7 - i)) & 1 for i in range(8)]


def pixels(b, mode):
    return mode0_pixels(b) if mode == 0 else mode1_pixels(b) if mode == 1 else mode2_pixels(b)


def parse_addr(text):
    text = text.strip().lower()
    for prefix in ('#', '&', '0x', '$'):
        if text.startswith(prefix):
            text = text[len(prefix):]
    return int(text, 16)


class Dump:
    """A memory dump of the emulator (cpc -d / -b / -x)."""

    def __init__(self, path):
        d = open(path, 'rb').read()
        self.banks = [d[i * 16384:(i + 1) * 16384] for i in range(8)]
        st = d[131072:131072 + 64]
        self.state = st
        self.a, self.f, self.b, self.c, self.d, self.e, self.h, self.l = st[0:8]
        self.ix, self.iy, self.sp, self.pc = struct.unpack('<4H', st[8:16])
        self.mode = st[16]
        self.palette = list(st[17:34])      # 16 pens + border, hardware colour numbers
        self.crtc = list(st[34:52])
        self.bank_map = list(st[52:56]) if any(st[53:56]) else [0, 1, 2, st[52] or 3]
        rest = d[131072 + 64:]
        self.line_palette = [list(rest[i * 17:i * 17 + 17]) for i in range(312)] if len(rest) >= 312 * 17 else None
        self.line_mode = list(rest[312 * 17:312 * 18]) if len(rest) >= 312 * 18 else None
        # the 64 KB the CPU saw (RAM only: the ROMs are not in the dump)
        self.mem = b''.join(self.banks[self.bank_map[i]] for i in range(4))

    def rgb(self, pen, line=None):
        pal = self.line_palette[line] if line is not None and self.line_palette else self.palette
        return HW[pal[pen] & 0x1f]

    def screen(self):
        """The displayed picture, like the emulator's screenshots (PIL image, mode 1 resolution)."""
        from PIL import Image
        r1, rows, r9 = self.crtc[1], self.crtc[6], self.crtc[9] + 1
        start = self.crtc[12] << 8 | self.crtc[13]
        width, height = r1 * 8, rows * r9
        first = ((self.crtc[4] + 1) - self.crtc[7]) * r9 + self.crtc[5]
        img = Image.new('RGB', (width, height))
        px = img.load()
        ram = b''.join(self.banks[0:4])          # the CRTC always reads the base 64 KB
        for y in range(height):
            ln = (first + y) % 312
            mode = self.line_mode[ln] if self.line_mode else self.mode
            row, ra = divmod(y, r9)
            for c in range(r1):
                ma = start + row * r1 + c
                addr = ((ma & 0x3000) << 2) | ((ra & 7) << 11) | ((ma & 0x3ff) << 1)
                for k in range(2):
                    pens = pixels(ram[(addr + k) & 0xffff], mode)
                    for x in range(4):
                        pen = pens[x // 2] if mode == 0 else pens[x] if mode == 1 else pens[x * 2]
                        px[c * 8 + k * 4 + x, y] = self.rgb(pen, ln)
        return img


def graphics(mem, addr, w, h, count, mode=0, zigzag=False, rgb=None, gap=2):
    """count pictures of w bytes x h lines stored one after the other (row-major) from addr.
    zigzag: odd lines are stored right to left (a common trick of fast blitters)."""
    from PIL import Image
    ppb = 2 if mode == 0 else 4 if mode == 1 else 8
    wide = 2 if mode == 0 else 1                            # mode 0 pixels are twice as wide as high
    rgb = rgb or (lambda pen: HW[[20, 4, 21, 28, 24, 29, 12, 5, 13, 22, 6, 23, 30, 0, 31, 14][pen]] if pen else (0, 0, 0))
    cell = w * ppb * wide + gap
    img = Image.new('RGB', (count * cell, h), (255, 0, 255))
    px = img.load()
    for n in range(count):
        base = addr + n * w * h
        for y in range(h):
            row = [p for b in mem[base + y * w:base + y * w + w] for p in pixels(b, mode)]
            if zigzag and y & 1:
                row = row[::-1]
            for x, pen in enumerate(row):
                for k in range(wide):
                    px[n * cell + x * wide + k, y] = rgb(pen)
    return img


def relative_search(mem, word):
    """Addresses where `word` is stored in some unknown character code: letters keep their distances
    (A..Z consecutive), whatever the code of 'A'. Returns (address, code of the first letter)."""
    letters = [c for c in word.upper()]
    hits = []
    for i in range(len(mem) - len(letters)):
        base = mem[i] - (ord(letters[0]) - ord('A')) if letters[0].isalpha() else None
        ok = True
        for k, ch in enumerate(letters):
            if ch == ' ':
                continue
            want = (base + ord(ch) - ord('A')) if ch.isalpha() else None
            if want is None or mem[i + k] != want:
                ok = False
                break
        if ok:
            hits.append((i, mem[i]))
    return hits


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd, args = argv[1], argv[2:]
    opts = {}
    rest = []
    i = 0
    while i < len(args):
        if args[i].startswith('--'):
            key = args[i][2:]
            if key == 'zigzag':
                opts[key] = True
            else:
                opts[key] = args[i + 1]
                i += 1
        else:
            rest.append(args[i])
        i += 1
    if cmd == 'coverage':
        cov = open(rest[0], 'rb').read()
        start = None
        for a in range(65537):
            on = a < 65536 and cov[a]
            if on and start is None:
                start = a
            elif not on and start is not None:
                print(f'{start:04x}-{a - 1:04x}  ({a - start} bytes)')
                start = None
        return 0
    dump = Dump(rest[0])
    mem = dump.mem
    if cmd == 'diff':
        other = Dump(rest[1]).mem
        lo, hi = (parse_addr(x) for x in opts.get('range', '0-ffff').split('-'))
        want_from = int(opts['from'], 0) if 'from' in opts else None
        want_to = int(opts['to'], 0) if 'to' in opts else None
        hits = [a for a in range(lo, hi + 1) if mem[a] != other[a]
                and (want_from is None or mem[a] == want_from) and (want_to is None or other[a] == want_to)]
        for a in hits[:400]:
            print(f'{a:04x}  {mem[a]:02x} -> {other[a]:02x}  ({mem[a]} -> {other[a]})')
        print(f'{len(hits)} address(es)' + (' (first 400 shown)' if len(hits) > 400 else ''))
        return 0
    if cmd == 'screen':
        dump.screen().save(rest[1])
    elif cmd == 'palette':
        for pen in range(17):
            hw = dump.palette[pen] & 0x1f
            fw = FIRMWARE.index(hw) if hw in FIRMWARE else '-'
            print(f'{"border" if pen == 16 else "pen %2d" % pen}: hardware {hw:2d}  firmware {fw!s:>2}  rgb {HW[hw]}  #{"%02x%02x%02x" % HW[hw]}')
    elif cmd == 'peek':
        a = parse_addr(rest[1])
        n = int(rest[2], 0) if len(rest) > 2 else 256
        for p in range(a, a + n, 16):
            row = mem[p:min(p + 16, a + n)]
            print(f'{p:04x}  {row.hex(" "):48s}  {"".join(chr(c) if 32 <= c < 127 else "." for c in row)}')
    elif cmd == 'find':
        pat = bytes.fromhex(' '.join(rest[1:]).replace('#', ''))
        p = mem.find(pat)
        while p >= 0:
            print(f'{p:04x}')
            p = mem.find(pat, p + 1)
    elif cmd == 'xref':
        a = parse_addr(rest[1])
        pat = bytes([a & 255, a >> 8])
        p = mem.find(pat)
        while p >= 0:
            print(f'{p:04x}  (opcode before: {mem[p - 1]:02x})  {mem[max(0, p - 3):p + 2].hex(" ")}')
            p = mem.find(pat, p + 1)
    elif cmd == 'text':
        for addr, first in relative_search(mem, rest[1]):
            print(f'{addr:04x}  first letter code {first:02x}  -> "A" = {first - (ord(rest[1][0].upper()) - 65):02x}')
    elif cmd == 'gfx':
        a, w, h, n = parse_addr(rest[1]), int(rest[2]), int(rest[3]), int(rest[4])
        img = graphics(mem, a, w, h, n, int(opts.get('mode', dump.mode)), opts.get('zigzag', False), lambda pen: dump.rgb(pen))
        scale = int(opts.get('scale', 4))
        img.resize((img.width * scale, img.height * scale), 0).save(rest[5])  # 0 = nearest
    elif cmd == 'tiles':
        from PIL import Image
        a, count = parse_addr(rest[1]), int(rest[2])
        w, h, mode = int(opts.get('w', 2)), int(opts.get('h', 8)), int(opts.get('mode', dump.mode))
        per = int(opts.get('per-row', 16))
        one = graphics(mem, a, w, h, count, mode, False, lambda pen: dump.rgb(pen), gap=0)
        tw = one.width // count
        sheet = Image.new('RGB', (per * (tw + 1), ((count + per - 1) // per) * (h + 1)), (255, 0, 255))
        for t in range(count):
            sheet.paste(one.crop((t * tw, 0, t * tw + tw, h)), ((t % per) * (tw + 1), (t // per) * (h + 1)))
        scale = int(opts.get('scale', 3))
        sheet.resize((sheet.width * scale, sheet.height * scale), Image.NEAREST).save(rest[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
