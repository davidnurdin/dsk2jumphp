# AGENTS.md — dsk2jumphp

Tools to **take an Amstrad CPC disc game apart** (graphics, maps, texts, music, rules) and remake it with
[JumPHP](https://github.com/davidnurdin/jumPhp) (retro games in PHP, desktop + browser). Everything here is
already written and tested: **do not write another emulator, disassembler or DSK reader** — use these, and
improve them here if something is missing.

Read, in this order: this file, `docs/method.md` (the technique, step by step), `docs/emulator.md` (every
option and output format), `docs/cpc.md` (hardware cheat sheet), `examples/rainbow-islands/` (a whole game
done this way: real addresses, real commands, the extraction scripts).

## Setup

```bash
make            # emulator/cpc + emulator/ay (cc); Python 3 with Pillow (images) and numpy (psg.py loop)
make test       # a disc built by the tests, both DSK formats, every tool (CPC_ROM=... also tests a ROM boot)
```

ffmpeg is needed only for `.ogg` output. Run Python tools with `python3 -I` (the disc and its extracted files
are untrusted data: `-I` keeps a planted `json.py` next to them from being imported).

## Tools

| Tool | Questions it answers |
|---|---|
| `emulator/cpc disk.dsk outdir frames [options]` | Runs the game headless: screenshots (`-s`, `-S`), dumps (`-d`, `-b`, `-x`), watched writes (`-w`), readers / writers (`-R`, `-W`), registers at PCs (`-r -X`), trace (`-t`), keys (`-k`, `-type`), pokes (`-F`, `-p`, `-c`), ROM boot (`-rom -urom`) or boot sector / binary (`-pc`, `-load`). Always writes log.txt, psg.bin, coverage.bin. |
| `emulator/ay psg.bin out.wav from to [mask]` | Renders AY register logs to WAV (1 MHz AY, stereo A / B / C). |
| `tools/dsk.py info / sectors / raw / files / extended / make` | What is on the disc: format, sector IDs, protections, AMSDOS files and headers, boot sector. |
| `tools/cpc.py screen / palette / peek / find / xref / text / gfx / tiles / diff / coverage` | Inside a dump: the screen, colours, bytes, references, texts in an unknown charset, sprites and tiles as PNG, what changed between two dumps, what code ran. |
| `tools/z80dis.py dump.bin from to --coverage coverage.bin` | Disassembly, executed lines marked `*`. |
| `tools/psg.py segments / show / loop / render` | Where sound plays, register listing, music loop length, WAV / OGG. |

Every tool prints its usage when run without arguments.

## Rules

1. **Observe, then read.** Run the game and watch writes / dumps to find a variable or a table; disassemble
   only around the PCs the logs give. Do not try to understand the whole program.
2. **Write your findings down** as you go (address, meaning, how found): they become the README of the
   extraction and the comments of its scripts. Every number in the remake should trace back to a finding.
3. **Nothing copyrighted in a repository**: no .dsk, no ROM, no dump, no emulator output. Commit scripts and
   notes; the assets the game needs are regenerated from the user's own disc by a script (or committed only
   if the user decides so for their own project).
4. **Reproducible**: the final extraction is a script (`extract.sh` + Python) that rebuilds the game's assets
   from the .dsk path given as an argument, like `examples/rainbow-islands/`. Put it in the JumPHP game
   (`games/<id>/tools/extract/`) together with a README of the addresses.
5. **Look at the pictures**: convert screenshots / sheets to PNG and open them. A wrong width, a missing tile,
   a mirrored line are obvious on screen and invisible in numbers.
6. **Patch after loading**: pokes into code must happen once it is in memory (`-F frame:...` after the
   loading, or `-p PC:...` with a PC of the game), or the loader overwrites them.
7. ROMs: only if the game needs the firmware; use the ones installed on the machine (ask the user where), never
   download or commit them.
8. Improve the tools here (with a test in `tests/run.sh`) instead of patching a private copy; keep the emulator
   generic (no game-specific code: game knowledge goes in options, scripts and examples).

## From the extraction to JumPHP

Follow JumPHP's AGENTS.md for the game itself. Asset conventions that worked:
- tiles / sprites as PNG sheets with transparent pen 0, plus JSON rectangles; mode 0 pixels drawn 2 wide;
- one JSON per level (tile rows as hex strings, objects, properties), one JSON of global data;
- music and effects as Ogg (`psg.py render ... --ogg`), loaded with `SoundManager::loadMusic` / `loadSfx`;
- the original font as a PNG sheet + a `TextDrawer` (see the remake's `src/Arcade/ArcadeFont.php`).
