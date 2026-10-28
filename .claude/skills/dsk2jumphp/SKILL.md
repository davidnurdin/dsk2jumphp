---
name: dsk2jumphp
description: Take an Amstrad CPC disc game (.dsk) apart and remake it with JumPHP — run it in the dsk2jumphp emulator, find its variables, extract graphics, tiles, sprites, fonts, level maps, texts, music and sound effects, then write the game in PHP. Use whenever the user wants to port, remake, reverse, decompile or "rip" an Amstrad / CPC game, gives a .dsk file, or wants the original assets / music of a CPC game in a JumPHP project, even without naming the skill.
---

# dsk2jumphp: from a CPC disc to a JumPHP game

The tools already exist: a headless CPC 6128 emulator that logs everything, DSK / dump / sound / Z80 tools,
and the documented method. **Never write a new emulator or disassembler**: use these.

## 0. Find the tools

The repository is three folders above this file (`readlink -f` it if the skill is a symlink):
`<repo>/emulator/cpc`, `<repo>/tools/*.py`, `<repo>/docs/`. If it is not on the machine:
`git clone https://github.com/davidnurdin/dsk2jumphp.git` (ask the user where), then `make -C <repo>` and
`make -C <repo> test`. Read `<repo>/AGENTS.md`, then `<repo>/docs/method.md` before starting, and
`<repo>/docs/emulator.md` for the options. `<repo>/examples/rainbow-islands/` is a complete worked example:
copy its patterns.

## 1. The loop

Work in a scratch folder (never inside a repository: dumps and the .dsk are copyrighted).

1. `python3 -I <repo>/tools/dsk.py info|files|sectors` — what the disc is (AMSDOS files → boot with ROMs,
   only a boot sector → boot without, `-pc 110` if the boot sector starts with a parameter block).
2. `<repo>/emulator/cpc disk.dsk run N -S 50 ...` and **open the screenshots** (PPM → PNG with Pillow).
   ROMs only if needed: ask the user for their path (an installed emulator has them); never download them.
3. Find the keys (`key row` lines in log.txt), script a start with `-k` / `-type`.
4. Find variables: dumps (`-d`) + `cpc.py diff --from --to`, then `-w ADDR` for the code that changes them.
5. Read code only around those PCs: `z80dis.py dump from to --coverage coverage.bin`, `cpc.py xref`, `-r PC -X 32`.
6. Steer: `-p PC:ADDR=VAL` (level choice right when the game sets it), `-c` (keep a counter going),
   `-F FRAME:ADDR=VAL` (requests, cheats, code patches after loading).
7. Extract: `cpc.py palette / screen / tiles / gfx [--zigzag] / text`, maps from `-w` logs of the map buffer
   and camera, objects from level data, sound with `-F` on the music request + `psg.py segments / loop / render`.
8. Write every address and its meaning in a notes file as you find it.

## 2. Into JumPHP

1. In the JumPHP project: `bin/jumphp new <id>` (or the existing game), read JumPHP's `AGENTS.md`.
2. `games/<id>/tools/extract/`: the extraction scripts (Python, using `<repo>/tools`), a README with the
   addresses, and a script `extract.sh <disk.dsk>` that rebuilds `games/<id>/assets/` (like
   `<repo>/examples/rainbow-islands/extract.sh`). The .dsk itself stays out of the repository.
3. Assets: PNG sheets (pen 0 transparent, mode 0 pixels 2 wide), JSON for levels and data, Ogg for sound.
4. Write the game in readable PHP (scenes, entities, world classes), with the original's screen size and rules.
   Check it against the emulator: the same moment side by side (`-s` vs `bin/jumphp shot`).
5. JumPHP's definition of done: `bin/jumphp test <id>`, screenshots looked at, `bin/jumphp lint`, browser build.

## Pitfalls already met

- Patches into code written before the game is loaded are overwritten: patch at a frame after loading.
- Sprites with every other line mirrored: zig-zag blitter, `--zigzag`.
- A map that only exists 64 rows at a time: rebuild it from the write log (`examples/rainbow-islands/mapbuild.py`).
- Music drivers do not tick on frame boundaries: registers never repeat exactly; `psg.py loop` estimates,
  the song pointer (`-w`) gives the exact loop.
- Unused tiles in a picture you assembled = a missing part (Rainbow Islands' rainbow legs): check that every
  tile of a set is used, and look at the result zoomed in.
- Texts in a custom charset: `cpc.py text dump "WORD"` finds them and gives the code of "A".
