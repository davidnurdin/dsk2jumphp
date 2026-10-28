# The method: from a CPC disc to a JumPHP game

The idea: **do not read the game, run it.** The emulator executes the original code; we watch what it writes
and when, dump its memory at the right moments, and steer it with keys and pokes. Code is disassembled only
around the spots the observation points to. Each step below says which tool answers which question, and what
it gave on Rainbow Islands (`examples/rainbow-islands/`, every address there).

Keep a notes file as you go (addresses, meaning, how it was found): it becomes the example's README and the
comments of the extraction scripts. Work in a scratch folder; only scripts and notes go into a repository —
**never the .dsk, the ROMs or dumps** (copyrighted material).

## 0. Inventory the disc

```bash
python3 -I tools/dsk.py info disk.dsk           # format (system #41 / data #C1 / custom), sectors, protections
python3 -I tools/dsk.py files disk.dsk files/   # an AMSDOS directory? files + headers (load / exec addresses)
python3 -I tools/dsk.py sectors disk.dsk 0 41   # the boot sector
```

- Files with AMSDOS headers (BASIC loader, binaries): the game is started from BASIC → boot with ROMs.
- Only a boot sector and a tiny `DISC.BAS` (Rainbow Islands): the game loads itself with its own disc routines
  and its own file table (a list of track / sector / count in the boot code: `#01EF` there). Follow the
  loading in log.txt: every `fdc read track T sector S` is followed by `sector data -> ADDR`.

## 1. Boot it and see it

```bash
mkdir -p run && emulator/cpc disk.dsk run 1500 -S 50                                  # boot sector, no ROM
mkdir -p run && emulator/cpc disk.dsk run 1500 -S 50 -rom cpc6128.rom -urom 7:amsdos.rom -type '100:RUN"DISC\n'
python3 -c "from PIL import Image; import glob; [Image.open(f).save(f[:-4]+'.png') for f in glob.glob('run/*.ppm')]"
```

Look at the screenshots: loading screen, title, demo... Find the keys: log.txt `key row N pc=...` tells which
rows the game reads (row 9 = joystick). Script a start: `-k '300:joy_up+,320:joy_up-,400:joy_fire+,420:joy_fire-'`
(Rainbow Islands: up inserts a credit, fire starts). Note the frame numbers of each screen: everything later
is "run N frames with these keys and look".

If it crashes (black screen, frozen): `-x 0-ff` (or the range where code should never run) gives the last 256
PCs; the boot entry point (`-pc`), a missing ROM or a protection are the usual suspects.

## 2. Find the variables

The game's state is a few bytes: level, lives, score, camera, timer, "what music to play". Ways to find them:

- **Diff two dumps** around an event: `-d 500,1400` then
  `python3 -I tools/cpc.py diff run/dump-00500.bin run/dump-01400.bin --from 3 --to 2` (lives 3 → 2).
  Narrow with `--range`. Repeat with another event until one address is left.
- **Watch the candidate**: `-w fee1` lists every write with the PC and frame → the code that changes it.
- **Who uses a table**: `-R 9800-9fff` / `-W ...` count readers / writers per PC.

Rainbow Islands: round `#FEE0` (0-27), lives `#FEE1`, goal row `#FEE4`, camera `#FEFB` (top map row, 8 bits,
wraps), island loaded `#FEDF`, solid tile threshold `#FEC8`, sound effect request `#614F`, music request
`#6152` (bit 7 = loop).

## 3. Read the code where it matters

```bash
python3 -I tools/z80dis.py run/dump-01400.bin 2740 27a0 --coverage run/coverage.bin   # '*' = executed
python3 -I tools/cpc.py xref run/dump-01400.bin fee0     # where the code loads / stores #FEE0
python3 -I tools/cpc.py find run/dump-01400.bin "3a e0 fe" # ld a,(#fee0)
emulator/cpc ... -r 2761,2986 -X 32                       # registers (and an object at IX) at those PCs
```

Look for the routine that writes the variable, the one that reads the joystick, the loop that copies graphics
(`ldir`, `ld (de),a` in a loop: a blitter; its source address is the graphics). `-t LO-HI` gives the order in
which code runs (initialisation first).

## 4. Steer the game

- **Choose the level**: patch the variable right after the game sets it: `-p 2761:fee0=05` (when PC reaches
  #2761, write 5 to #FEE0). Rainbow Islands also needed `-p 2761:fedf=80` (force the island file to reload).
- **Keep something going**: `-c 2986:fefe=81:2` refilled the intro pan counter twice so the camera scrolled
  to the top of the round, logging the whole map on the way.
- **Silence / immortality** for clean captures: patch code with VAL = `#C9` ret, `#AF` xor a, `#7E` ld a,(hl)...
  whatever skips the branch. Patch **after the code is loaded** (a patch written before is overwritten by the
  loader): `-F FRAME:ADDR=VAL` at a frame past the loading, or `-p PC:ADDR=VAL` with a PC of the loaded game.
  Rainbow Islands: `-F 520:3cc3=7e` (no death), `-F 520:6186=af` (no sound effects).
- **Ask for something**: `-F 1000:6152=82` = "play music 2" at frame 1000, through the game's own variable.

## 5. Graphics

```bash
python3 -I tools/cpc.py palette run/dump-01400.bin           # the 16 pens (hardware colours, RGB)
python3 -I tools/cpc.py screen run/dump-01400.bin screen.png
python3 -I tools/cpc.py tiles run/dump-01400.bin 7300 128 tiles.png --w 2 --h 8     # 8x8 mode 0 tiles
python3 -I tools/cpc.py gfx run/dump-01400.bin ed80 6 24 4 bub.png --zigzag         # 4 sprites 6 bytes x 24
python3 -I tools/cpc.py text run/dump-01400.bin "GAME OVER"  # texts in the game's own character code
```

- **Where**: a table of pointers often lists the frames (Rainbow Islands: 29 words at `#4BFC`); `-R` on a
  graphics area finds the blitter, its registers (`-r`) give address, width and height of each frame drawn.
- **Format**: mode 0 = 2 pixels per byte (bits interleaved: `cpc.mode0_pixels`), mode 1 = 4. Sprites are
  usually row-major, W bytes × H lines. If every other line looks mirrored: the blitter zig-zags (`--zigzag`).
  Pen 0 is usually transparent.
- **Sizes**: from the code (the blitter's counters), or by trying widths until the picture looks right (a
  wrong width shears it).
- **Font**: `text` finds a known word whatever the code of "A" is (relative search), then the font is
  the glyph table the print routine reads (`-R`). Rainbow Islands: `#1A30`, 8 bytes per glyph, 2 bits per pixel.
- **Palettes per area**: dumps keep the palette of every scanline (HUD and playfield may differ).
- **Export**: PNG sheets (one image per set, transparent pen 0) + a JSON of rectangles; mode 0 pixels drawn 2
  wide so the JumPHP view keeps the CPC's aspect (a 160-pixel mode 0 screen = 320 virtual pixels; Rainbow
  Islands' 128 × 176 screen → 256 × 176).

## 6. Levels and maps

- Packed data: compare the file on disc with what the game unpacks in RAM (`-w` on the destination, a dump
  after) → the packing scheme (Rainbow Islands: run-length, 3 equal bytes then a count).
- Scrolling games keep only the visible part: watch the writes to the map buffer and to the camera
  (`-w 0800-0fff -w fefb`) while the level scrolls, and rebuild the whole level offline
  (`examples/rainbow-islands/mapbuild.py`: a 64-row ring buffer).
- Objects (enemies, items): a list in the level data; their record layout comes from `-r PC -X 32` at the
  routine that spawns them. Check placements against screenshots.
- Collisions: find the test (a `cp` against a tile number: Rainbow Islands' threshold `#FEC8`) rather than
  guessing from the pictures.
- Export one JSON per level (rows of tiles, objects, properties) for the PHP game.

## 7. Sound

The emulator logs the AY registers every frame (`psg.bin`); `emulator/ay` renders them like a CPC.

```bash
emulator/cpc disk.dsk snd 4000 <start keys> -F 520:3cc3=7e -F 520:6186=af -F 1000:6152=82   # music 2
python3 -I tools/psg.py segments snd/psg.bin                 # where something plays
python3 -I tools/psg.py loop snd/psg.bin 1000 4000           # estimated loop length
python3 -I tools/psg.py render snd/psg.bin 1000 2040 boss.ogg  # intro + one loop -> Ogg (ffmpeg)
```

- Trigger each music / effect through the game's request variable (`-F`), one per run, with everything else
  silenced (patches above), and the game in a state where nothing else plays.
- **Exact loop point**: drivers rarely tick in step with the 50 Hz frames, so `loop` only estimates. Watch the
  driver's song pointer (`-w` on it: the frame where it jumps back is the loop) or the game restarting the tune
  (Rainbow Islands' theme: 2237 frames, restarted by the game at `#64EB`).
- JumPHP plays Ogg: `SoundManager::loadMusic('theme', 'assets/music/theme.ogg')` loops it.

## 8. Write the JumPHP game

1. `bin/jumphp new <id>` in the JumPHP project; copy the exported assets in `games/<id>/assets/`.
2. Keep the extraction reproducible **inside the game**: `games/<id>/tools/extract/` with the scripts of steps
   5-7, a README (addresses, method) and a script that rebuilds `assets/` from the .dsk given as argument. The
   .dsk stays outside the repository.
3. Write the game in readable PHP, following JumPHP's AGENTS.md: the original's rules (speeds, timers, scores)
   come from the notes; use the original screen size (×2 horizontally in mode 0).
4. Compare often: the same moment in the emulator (`-s`) and in the remake (`bin/jumphp shot`) side by side.
5. Simulate what is hard to see (a jump, a collision) in a small PHP script run with `bin/jumphp php`.

The Rainbow Islands remake was made this way: `games/rainbow-island` of the JumPHP project
(github.com/davidnurdin/rainbow-island), extraction in its `tools/extract/`, this repository's example.
