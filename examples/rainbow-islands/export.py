"""Write the game's assets from the original disc and the emulator captures.

    python3 -I export.py <disk.dsk> <work dir of extract_round.py> <game folder>

Everything the PHP game loads comes from here:
  assets/tiles/island-N.png      the 128 background tiles of island N (8x8, 16 per row)
  assets/tiles/overlay-N.png     the 128 "overlay" tiles (rainbows, fruits...) drawn over the background
  assets/sprites/island-N.png    the 29 sprite frames of island N, and island-N.json their rectangles
  assets/fonts/font.png          the game's font: 48 glyphs per row, one row per colour
  assets/sprites/loading.png     the loading screen
  assets/maps/round-NN.json      the 28 rounds: tile rows, goal, signs, enemies
  assets/data/islands.json       names, solidity threshold and enemy kinds of the 7 islands

The CPC draws in mode 0 (pixels twice as wide as high), so every pixel is written 2 pixels wide.
"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from PIL import Image
from cpc import HW, mode0_pixels
from dsk import Disk
from island import unpack2, blocks
from sprite import decode
from frameset import frames as sprite_frames

ISLANDS = ['INSECT ISLAND', 'COMBAT ISLAND', 'MONSTER ISLAND', 'TOY ISLAND',
           "DOH'S ISLAND", 'ROBOT ISLAND', 'DRAGON ISLAND']

# Object type (low 7 bits of a placement) -> sprite frames it was seen drawing in the emulator
# (typemap.py); types the captures never met are guessed from the frame order. Frames 12-15 (the blue spin of a hit enemy) are shared by all.
ENEMY_FRAMES = {
    1: {0x44: [16, 17], 0x45: [16, 17], 0x46: [18, 19], 0x48: [20], 0x4a: [21, 22], 0x4b: [23, 24], 0x4c: [25, 26]},
    2: {0x44: [16, 17], 0x46: [18, 19], 0x48: [20, 21], 0x4a: [24], 0x4c: [22, 23], 0x4e: [25, 26], 0x50: [27, 28]},
    3: {0x44: [19, 20], 0x46: [16, 17], 0x48: [21, 22], 0x4a: [18], 0x4c: [19, 20], 0x4e: [23], 0x50: [25, 26]},
    4: {0x44: [17], 0x46: [16], 0x48: [27], 0x4a: [27], 0x4c: [28], 0x4e: [18, 19, 20, 21], 0x50: [22, 23, 24, 25]},
    5: {0x44: [16], 0x46: [17, 18, 19, 20], 0x48: [21], 0x4a: [22, 23, 24, 25], 0x4c: [26, 27]},
    6: {0x44: [16, 17], 0x46: [16, 17], 0x48: [19], 0x4a: [20, 21], 0x4c: [22, 23], 0x4e: [18], 0x50: [25, 26]},
    7: {0x44: [16, 17], 0x46: [18, 19], 0x48: [18, 19], 0x4a: [18, 19], 0x4c: [20], 0x4e: [21, 22], 0x50: [25, 26]},
}
# Enemies seen flying (the others walk on the platforms)
FLYERS = {1: [0x46, 0x4c], 2: [0x4c, 0x4e], 3: [0x44, 0x4c], 4: [0x48, 0x4a], 5: [0x44, 0x48], 6: [0x48, 0x4e], 7: [0x4c]}
# The rainbow: 6 x 4 overlay tiles (overlay tiles 1-18; its legs are 2 tiles wide)
RAINBOW = [[0x00, 0x01, 0x02, 0x03, 0x04, 0x00],
           [0x05, 0x0f, 0x06, 0x07, 0x10, 0x08],
           [0x09, 0x0a, 0x00, 0x00, 0x0b, 0x0c],
           [0x11, 0x0d, 0x00, 0x00, 0x0e, 0x12]]


def read_file(disk, entry):
    """A file of the loader's table at #01EF: (track, sector index 0-8, sector count)."""
    track, index, count = entry
    return disk.read(track, 0x41 + index, count)


def rgb(pal, pen):
    return HW[pal[pen] & 0x1f]


def tile_image(data, pal, transparent):
    img = Image.new('RGBA', (16 * 8, 8 * 8), (0, 0, 0, 0))
    px = img.load()
    for t in range(128):
        for y in range(8):
            row = [p for b in data[t * 16 + y * 2:t * 16 + y * 2 + 2] for p in mode0_pixels(b)]
            for x, p in enumerate(row):
                colour = (0, 0, 0, 0) if transparent and p == 0 else rgb(pal, p) + (255,)
                px[(t % 16) * 8 + x * 2, (t // 16) * 8 + y] = colour
                px[(t % 16) * 8 + x * 2 + 1, (t // 16) * 8 + y] = colour
    return img


def sprite_sheet(mem, pal):
    rects, x = [], 0
    found = sprite_frames(mem)
    height = max(f[2] for f in found if f)
    img = Image.new('RGBA', (sum(f[1] * 4 + 2 for f in found if f), height), (0, 0, 0, 0))
    px = img.load()
    for f in found:
        if not f:
            rects.append(None)
            continue
        base, w, h = f
        for yy, row in enumerate(decode(mem, base, w, h)):
            for xx, p in enumerate(row):
                if p:
                    px[x + xx * 2, yy] = px[x + xx * 2 + 1, yy] = rgb(pal, p) + (255,)
        rects.append({'x': x, 'y': 0, 'w': w * 4, 'h': h})
        x += w * 4 + 2
    return img, rects


def boss_frame(sheet, rects):
    """The boss is the biggest frame of the island's own ones (16-28). Some are stored as their left half
    only (the right half is drawn mirrored): their rightmost column is not empty."""
    best = max((i for i in range(16, len(rects)) if rects[i]), key=lambda i: rects[i]['w'] * rects[i]['h'])
    r = rects[best]
    edge = sum(1 for y in range(r['h']) if sheet.getpixel((r['x'] + r['w'] - 1, y))[3])
    return best, edge > r['h'] // 3


def font_image(mem, pal):
    schemes = [2, 4, 5, 6, 7]  # red, green, cyan, yellow, white
    img = Image.new('RGBA', (48 * 8, len(schemes) * 8), (0, 0, 0, 0))
    px = img.load()
    for row, scheme in enumerate(schemes):
        table = mem[0x247d + scheme * 4:0x247d + scheme * 4 + 4]
        for ch in range(48):
            for y in range(8):
                bits = mem[0x1a30 + ch * 8 + y]
                for x in range(4):
                    v = (bits >> (6 - 2 * x)) & 3
                    if v:
                        c = rgb(pal, mode0_pixels(table[v])[1]) + (255,)
                        px[ch * 8 + x * 2, row * 8 + y] = px[ch * 8 + x * 2 + 1, row * 8 + y] = c
    return img


def round_lists(block):
    """The three lists of a round block: placements by player height, placements by camera, signs."""
    offsets = struct.unpack('<3H', block[:6])
    by_camera = block[offsets[1]:offsets[2]]
    objects, i = [], 3
    while i + 2 < len(by_camera):
        y, x, t = by_camera[i:i + 3]
        i += 3
        if x == 0xff:
            break
        if t & 0x80 and not x & 0x60:
            objects.append({'type': t & 0x7f, 'x': x & 0x1f, 'y': y - 256, 'flip': bool(x & 0x80)})
    signs = block[offsets[2]:]
    sign_rows = [signs[k + 1] - 256 for k in range(0, len(signs) - 1, 2)]
    return objects, sign_rows, block[7]


def main():
    dsk, work, game = sys.argv[1:4]
    assets = os.path.join(game, 'assets')
    for sub in ('tiles', 'sprites', 'fonts', 'maps', 'data'):
        os.makedirs(os.path.join(assets, sub), exist_ok=True)
    disk = Disk(dsk)

    # the loading screen: 16 KB of mode 0 screen read by the boot loader (file 11), palette from the boot sector
    boot = disk.sector(0, 0x41).data
    pal = list(boot[0x95:0x95 + 16])
    screen = read_file(disk, (22, 2, 32))
    img = Image.new('RGB', (320, 200))
    px = img.load()
    for y in range(200):
        for x in range(80):
            b = screen[(y // 8) * 80 + (y % 8) * 0x800 + x]
            for k, p in enumerate(mode0_pixels(b)):
                px[x * 4 + k * 2, y] = px[x * 4 + k * 2 + 1, y] = rgb(pal, p)
    img.save(os.path.join(assets, 'sprites', 'loading.png'))

    islands = []
    for n in range(1, 8):
        mem = open(f'{work}/round-{(n - 1) * 4:02d}/dump-02200.bin', 'rb').read()
        pal = list(mem[0x20011:0x20021])
        tile_image(mem[0x7300:0x7b00], pal, False).save(f'{assets}/tiles/island-{n}.png')
        tile_image(mem[0x1000:0x1800], pal, True).save(f'{assets}/tiles/overlay-{n}.png')
        sheet, rects = sprite_sheet(mem, pal)
        sheet.save(f'{assets}/sprites/island-{n}.png')
        json.dump(rects, open(f'{assets}/sprites/island-{n}.json', 'w'))
        if n == 1:
            font_image(mem, pal).save(f'{assets}/fonts/font.png')
        islands.append({
            'name': ISLANDS[n - 1],
            'solidFrom': mem[0xfec8],
            'palette': ['#%02x%02x%02x' % rgb(pal, p) for p in range(16)],
            'enemies': {str(t): {'frames': f, 'flies': t in FLYERS[n]} for t, f in ENEMY_FRAMES[n].items()},
            'bossFrame': boss_frame(sheet, rects)[0],
            'bossMirrored': boss_frame(sheet, rects)[1],
        })
    json.dump(islands, open(f'{assets}/data/islands.json', 'w'), indent=1)

    # where Bub's feet rest on a rainbow: the first opaque pixel of each of its 48 columns
    mem = open(f'{work}/round-00/dump-02200.bin', 'rb').read()
    profile = []
    for x in range(48):
        col, sub = x // 8, (x % 8) // 2
        top = 32
        for y in range(32):
            t = RAINBOW[y // 8][col]
            if t and mode0_pixels(mem[0x1000 + t * 16 + (y % 8) * 2 + sub // 2])[sub % 2]:
                top = y
                break
        profile.append(top)
    json.dump({'tiles': RAINBOW, 'profile': profile}, open(f'{assets}/data/rainbow.json', 'w'))

    for r in range(28):
        n = r // 4 + 1
        data = blocks(unpack2(read_file(disk, ISLAND_FILES[n - 1])))
        objects, sign_rows, hurry = round_lists(data['blocks'][2 + r % 4])
        m = json.load(open(f'{work}/round-{r:02d}/map.json'))
        mem = open(f'{work}/round-{r:02d}/dump-02200.bin', 'rb').read()
        json.dump({
            'island': n, 'round': r + 1, 'top': m['top'], 'goalRow': mem[0xfee4] - 256,
            'hurry': hurry, 'signs': sign_rows, 'objects': objects,
            'rows': [''.join('%02x' % t for t in row) for row in m['rows']],
        }, open(f'{assets}/maps/round-{r + 1:02d}.json', 'w'), indent=0)
    print('assets written to', assets)


# (track, sector, count) of the seven island files, from the loader's table at #01EF
ISLAND_FILES = [(25, 7, 18), (27, 7, 18), (29, 7, 18), (31, 7, 18), (33, 7, 18), (35, 7, 18), (37, 7, 18)]

if __name__ == '__main__':
    main()
