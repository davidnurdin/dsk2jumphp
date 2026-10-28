"""CPC disc images (.dsk): standard "MV - CPCEMU" and "EXTENDED CPC DSK" files.

    python3 -I tools/dsk.py info     disk.dsk                 format, tracks, sector IDs / sizes / status per track
    python3 -I tools/dsk.py sectors  disk.dsk TRACK R [N]     hex dump of N sectors from sector ID R (hex) of a track
    python3 -I tools/dsk.py raw      disk.dsk out.bin         every track one after the other, sectors sorted by ID
    python3 -I tools/dsk.py files    disk.dsk OUTDIR          the AMSDOS / CP/M directory, files extracted
                                                              (AMSDOS header decoded: type, load, exec address)
    python3 -I tools/dsk.py extended in.dsk out.dsk           the same disc as an EXTENDED image
    python3 -I tools/dsk.py make     out.dsk boot.bin [file.bin@TRACK:R ...]
                                                              a system format disc: boot.bin in track 0 sector #41

A game that boots by itself (|CPM) keeps its own file table: `files` finds nothing useful then; read the
boot sector (`sectors disk.dsk 0 41`) and follow the loader in the emulator (docs/method.md).
"""
import os
import struct
import sys


class Sector:
    def __init__(self, c, h, r, n, st1, st2, data):
        self.c, self.h, self.r, self.n, self.st1, self.st2, self.data = c, h, r, n, st1, st2, data


class Disk:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.extended = d[:8] == b'EXTENDED'
        if not self.extended and d[:8] != b'MV - CPC':
            raise ValueError(f'{path}: not a DSK image')
        self.creator = d[0x22:0x30].rstrip(b'\0').decode('latin-1')
        self.track_count, self.sides = d[0x30], d[0x31] or 1
        self.tracks = {}                       # (track, side) -> [Sector]
        off = 0x100
        for i in range(self.track_count * self.sides):
            size = d[0x34 + i] * 256 if self.extended else struct.unpack('<H', d[0x32:0x34])[0]
            if size == 0:
                continue
            th = d[off:off + 0x100]
            if th[:10] != b'Track-Info':
                off += size
                continue
            track, side, n_all, count = th[0x10], th[0x11], th[0x14], th[0x15]
            p = off + 0x100
            secs = []
            for s in range(count):
                c, h, r, n, st1, st2 = th[0x18 + s * 8:0x18 + s * 8 + 6]
                length = struct.unpack('<H', th[0x1e + s * 8:0x20 + s * 8])[0] if self.extended else 128 << (n_all & 7)
                length = length or 128 << (n & 7)
                secs.append(Sector(c, h, r, n, st1, st2, d[p:p + length]))
                p += length
            self.tracks[(track, side & 1)] = secs
            off += size

    def sector(self, track, r, side=0):
        for s in self.tracks.get((track, side), []):
            if s.r == r:
                return s
        return None

    def read(self, track, r, count, side=0):
        """count sectors from sector ID r of track, going on to the next track after the last ID of
        the track (#C9 for a data disc, #49 for a system disc)."""
        out = b''
        ids = sorted(s.r for s in self.tracks.get((track, side), []))
        for _ in range(count):
            s = self.sector(track, r, side)
            if s is None:
                raise KeyError(f'no sector {r:02x} on track {track}')
            out += s.data
            r += 1
            if r > ids[-1]:
                track += 1
                ids = sorted(x.r for x in self.tracks.get((track, side), []))
                r = ids[0]
        return out

    def format_name(self):
        ids = sorted(s.r for s in self.tracks.get((0, 0), []))
        if ids and ids[0] == 0xc1:
            return 'data'
        if ids and ids[0] == 0x41:
            return 'system'
        if ids and ids[0] == 0x01:
            return 'ibm'
        return 'custom'

    def files(self):
        """The CP/M directory (64 entries, 1 KB blocks): [(user, name, bytes)]."""
        fmt = self.format_name()
        first_track, first_r = {'data': (0, 0xc1), 'system': (2, 0x41), 'ibm': (1, 0x01)}.get(fmt, (None, None))
        if first_track is None:
            return []

        def linear(n):                          # the n-th 512-byte sector of the data area
            return self.read(first_track + n // 9, first_r + n % 9, 1)
        directory = b''.join(linear(i) for i in range(4))
        entries = {}
        for i in range(64):
            e = directory[i * 32:i * 32 + 32]
            if e[0] == 0xe5 or e[0] > 15:
                continue
            name = bytes(c & 0x7f for c in e[1:9]).decode('latin-1').strip()
            ext = bytes(c & 0x7f for c in e[9:12]).decode('latin-1').strip()
            entries.setdefault((e[0], name + ('.' + ext if ext else '')), []).append((e[12], e[15], [b for b in e[16:32] if b]))
        out = []
        for (user, name), extents in entries.items():
            data = b''
            for _, records, blocks in sorted(extents):
                chunk = b''.join(linear(b * 2) + linear(b * 2 + 1) for b in blocks)
                data += chunk[:records * 128]
            out.append((user, name, data))
        return out


def amsdos_header(data):
    """The 128-byte AMSDOS header of a file, or None: type (0 BASIC, 1 protected, 2 binary), load, length, exec."""
    if len(data) < 128:
        return None
    h = data[:128]
    if sum(h[:67]) & 0xffff != struct.unpack('<H', h[67:69])[0]:
        return None
    return {'type': h[18], 'load': struct.unpack('<H', h[21:23])[0], 'length': struct.unpack('<H', h[24:26])[0],
            'exec': struct.unpack('<H', h[26:28])[0]}


def write_extended(disk, path):
    tracks = sorted(disk.tracks)
    count = max(t for t, _ in tracks) + 1
    header = bytearray(b'EXTENDED CPC DSK File\r\nDisk-Info\r\n'.ljust(0x22, b'\0'))
    header += b'dsk2jumphp'.ljust(14, b'\0')
    header += bytes([count, disk.sides, 0, 0])
    blocks = []
    for t in range(count):
        for side in range(disk.sides):
            secs = disk.tracks.get((t, side), [])
            th = bytearray(b'Track-Info\r\n'.ljust(16, b'\0'))
            th += bytes([t, side, 0, 0, secs[0].n if secs else 2, len(secs), 0x4e, 0xe5])
            for s in secs:
                th += bytes([s.c, s.h, s.r, s.n, s.st1, s.st2]) + struct.pack('<H', len(s.data))
            th = th.ljust(0x100, b'\0') + b''.join(s.data for s in secs)
            th = th.ljust((len(th) + 255) // 256 * 256, b'\0')
            blocks.append(th)
    header = header.ljust(0x34, b'\0') + bytes(len(b) // 256 for b in blocks)
    open(path, 'wb').write(bytes(header.ljust(0x100, b'\0')) + b''.join(blocks))


def make_system_disc(path, boot, extra=()):
    """40 tracks of 9 x 512-byte sectors #41-#49 (system format), boot in track 0 sector #41,
    extra = [(bytes, track, first R)]."""
    image = {(t, r): bytes([0xe5]) * 512 for t in range(40) for r in range(0x41, 0x4a)}

    def put(data, track, r):
        for i in range(0, len(data), 512):
            image[(track, r)] = data[i:i + 512].ljust(512, b'\0')
            r += 1
            if r > 0x49:
                r, track = 0x41, track + 1
    put(boot, 0, 0x41)
    for data, track, r in extra:
        put(data, track, r)
    header = b'MV - CPCEMU Disk-File\r\nDisk-Info\r\n'.ljust(0x22, b'\0') + b'dsk2jumphp'.ljust(14, b'\0')
    header += bytes([40, 1]) + struct.pack('<H', 0x100 + 9 * 512)
    out = header.ljust(0x100, b'\0')
    for t in range(40):
        th = b'Track-Info\r\n'.ljust(16, b'\0') + bytes([t, 0, 0, 0, 2, 9, 0x4e, 0xe5])
        for r in range(0x41, 0x4a):
            th += bytes([t, 0, r, 2, 0, 0, 0, 0])
        out += th.ljust(0x100, b'\0') + b''.join(image[(t, r)] for r in range(0x41, 0x4a))
    open(path, 'wb').write(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == 'make':
        extra = []
        for spec in argv[4:]:
            f, where = spec.rsplit('@', 1)
            t, r = where.split(':')
            extra.append((open(f, 'rb').read(), int(t), int(r, 16)))
        make_system_disc(argv[2], open(argv[3], 'rb').read(), extra)
        return 0
    disk = Disk(argv[2])
    if cmd == 'info':
        print(f'{"EXTENDED" if disk.extended else "standard"} image by "{disk.creator}", {disk.track_count} tracks, '
              f'{disk.sides} side(s), {disk.format_name()} format')
        for (t, side), secs in sorted(disk.tracks.items()):
            ids = ' '.join(f'{s.r:02x}' + (f'/N{s.n}' if s.n != 2 else '') + (f'!{s.st1:02x}{s.st2:02x}' if s.st1 or s.st2 else '')
                           + (f'({len(s.data)})' if len(s.data) != 128 << (s.n & 7) else '') for s in secs)
            print(f'track {t:2d} side {side}: {len(secs):2d} sectors  {ids}')
    elif cmd == 'sectors':
        t, r = int(argv[3]), int(argv[4], 16)
        data = disk.read(t, r, int(argv[5]) if len(argv) > 5 else 1)
        for p in range(0, len(data), 16):
            row = data[p:p + 16]
            print(f'{p:04x}  {row.hex(" "):48s}  {"".join(chr(c) if 32 <= c < 127 else "." for c in row)}')
    elif cmd == 'raw':
        out = b''
        for key in sorted(disk.tracks):
            out += b''.join(s.data for s in sorted(disk.tracks[key], key=lambda s: s.r))
        open(argv[3], 'wb').write(out)
        print(f'{len(out)} bytes')
    elif cmd == 'files':
        os.makedirs(argv[3], exist_ok=True)
        files = disk.files()
        if not files:
            print(f'no directory ({disk.format_name()} format): a self-booting disc, see docs/method.md')
        for user, name, data in files:
            h = amsdos_header(data)
            info = f"AMSDOS type {h['type']} load #{h['load']:04x} length #{h['length']:04x} exec #{h['exec']:04x}" if h else 'no header'
            print(f'user {user:2d}  {name:12s} {len(data):6d} bytes  {info}')
            open(os.path.join(argv[3], name), 'wb').write(data)
    elif cmd == 'extended':
        write_extended(disk, argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
