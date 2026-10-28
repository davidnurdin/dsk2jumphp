# Example: Rainbow Islands (Amstrad CPC, Graftgold / Ocean 1989)

The complete extraction behind the JumPHP remake `games/rainbow-island`
(https://github.com/davidnurdin/rainbow-island). The disc is not included: bring your own `rainbow.dsk`.

```bash
examples/rainbow-islands/extract.sh rainbow.dsk ../jumphp/games/rainbow-island     # ~20 s, writes assets/
```

It runs the 28 rounds in the emulator (`extract_round.py`), then writes every asset of the game
(`export.py`): loading screen, 7 × (background tiles, overlay tiles, sprite sheet), font, 28 maps (JSON), the
islands' data, the rainbow's shape. Music and sound effects were recorded by hand (docs/method.md, "Sound"),
with the commands at the end of this file.

## The disc

Standard image, system format, 40 tracks. A boot sector (track 0 #41, a 16-byte parameter block then code
at #0110: run it with `-pc 110`) with its own disc routines and a **file table at #01EF**: 3 bytes per file
(sector index 0-8, track, sector count).

| File | Loaded at | Content |
|---|---|---|
| 11 | #C000 | loading screen (16 KB, mode 0, palette in the boot sector at #0195) |
| 2 | #0A00 | the game; a list of blocks moved with LDIR from #9800 (destination, length, data) |
| 12-18 | #9800 | the 7 islands, 9 KB each, run-length packed, unpacked by #7135 to #7300 (#2500 bytes) |

Packing (`island.py`): after 3 equal bytes, the next byte is a count of extra copies.

## Variables

| Address | Meaning |
|---|---|
| #FEE0 | round 0-27 (round & 3 = round of the island, round >> 2 = island) |
| #FEDF | island loaded (#FE = already in memory: poke #80 to force a reload) |
| #FEE1 | lives |
| #FEE4 | goal row (the G sign) |
| #FEFB | camera: top map row, 8 bits, wraps (unwrap it) |
| #FEFC | top limit of the round |
| #FEFE | intro pan counter (#8A: 118 rows; refilled with `-c 2986:fefe=81:2` to reach the top) |
| #FEC8 | solid tile threshold: tile & #7F >= threshold is solid |
| #614F | sound effect request (1 jump, 2 rainbow, 3 bonus, 4 collect, 6 break, 7 credit, 8 tally, 9 splash, 11 hit, 12 extra life) |
| #6152 | music request (bit 7 = loop): 1 theme, 2 boss, 3 game over, 4 goal, 5 hurry, #80 stop |

## Graphics

- Screen 128 × 176 mode 0 (R1 = 32, R6 = 22): 16 lines of scores, 160 lines of playfield.
- Background: tiles of 8 × 8 (16 bytes: 2 bytes × 8 lines) at #7300; tile byte bit 7 = mirrored.
  The map is a ring of 64 rows × 32 tiles at #0800 (row = y & 63): `mapbuild.py` rebuilds whole rounds from the
  write log.
- Overlay tiles at #1000 (pen 0 transparent): the rainbow (#01-#12, 6 × 4 tiles), items (2 × 2 tiles from #13).
- Sprites: 29 frame pointers at #4BFC; frames are W bytes × H lines, **odd lines stored mirrored** (zig-zag
  blitter): `cpc.py gfx ... --zigzag`. Objects: 32-byte records at #47A7 (+2 width, +3 height, +6/7 x,
  +8/9 y, +10 bit 0 flip, +16 frame).
- Font: #1A30, 8 bytes per glyph, 2 bits per pixel, 5 colour schemes at #247D; character codes 0-9, then
  A-Z = #0A-#23, space #24.

## Sound

```bash
# boss music (request #82): game started, deaths and sound effects patched out, music asked at frame 1000
emulator/cpc rainbow.dsk snd 2400 -pc 110 -k '300:joy_up+,320:joy_up-,400:joy_fire+,420:joy_fire-' \
    -F 520:3cc3=7e -F 520:6186=af -F 1000:6152=82
python3 -I tools/psg.py render snd/psg.bin 1000 2040 boss.ogg
```

Lengths used: theme 2237 frames (the game restarts it at #64EB), boss 1040. Effects: `-F FRAME:614f=N` the
same way, rendered from the request frame until the next silence (`psg.py segments`).
