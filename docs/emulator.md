# The emulator (`emulator/cpc`)

A small Amstrad CPC 6128 (Z80, gate array, CRTC, PPI, AY-3-8912, µPD765 disc controller, 128 KB) made to
**run a disc game without a screen and look inside it**: everything is driven by command-line options and
everything it sees goes to files. It is not a player: no window, no sound output (the AY registers are logged
and rendered afterwards by `emulator/ay`).

```bash
make                                    # builds emulator/cpc and emulator/ay (any C compiler)
emulator/cpc <disk.dsk|-> <outdir> <frames> [options]
```

`<outdir>` must exist. `<frames>`: how many 50 Hz frames to run (50 = 1 s of CPC time; it runs much faster).
`-` instead of a disc: no disc in the drive.

## Booting

| Way | Options | When |
|---|---|---|
| Boot sector, no ROM (default) | `-pc 100` (default) | Games that load themselves (`|CPM` discs). Track 0 sector #41 (or the first sector) is copied to #0100 and run with SP = #C000 and no ROM: the game sets up its own interrupts. If the boot sector starts with a CP/M parameter block (16 bytes, code at #0110: look with `tools/dsk.py sectors disk.dsk 0 41`), use `-pc 110`. |
| Raw binary, no ROM | `-load file.bin@4000 [-load other@8000] [-pc 4000]` | A file extracted with `tools/dsk.py files` (strip the 128-byte AMSDOS header first) or a memory image. Starts at the first `-load` address unless `-pc`. |
| Real machine | `-rom cpc6128.rom -urom 7:amsdos.rom -type '100:RUN"DISC\n'` | Games loaded from BASIC / AMSDOS, or anything that calls the firmware. |

ROMs are **not included**: use the ones of an emulator installed on the machine (e.g. `cpc6128.rom` +
`amsdos.rom` of ACE / Caprice / CPCemu / WinAPE; `CPC6128.ROM` is 32 KB = OS + BASIC). `-rom` takes the OS
(16 KB) or OS + BASIC (32 KB); `-urom N:file` puts an upper ROM in slot N (AMSDOS is 7).
BASIC is ready around frame 100; type after that.

## Input

| Option | Effect |
|---|---|
| `-type 'FRAME:text'` | Types text from FRAME (3 frames down, 3 up per key). `\n` = RETURN. Upper case and `"|!#$%&'()*+=?<>_` press SHIFT. Several `-type` allowed. |
| `-k 'FRAME:KEY+,FRAME:KEY-,...'` | Presses (`+`) / releases (`-`) keys at given frames. KEY = `row.bit` of the matrix, or a name: `joy_up joy_down joy_left joy_right joy_fire joy_fire2 space return enter shift control esc tab caps del copy clr up down left right f0`..`f9`, a letter or digit. |

Keyboard matrix (row: bits 0..7) — `log.txt` says which code reads which row (`key row 9 pc=...`): that is the
game's input routine.

| Row | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 0 | ↑ | → | ↓ | f9 | f6 | f3 | ENTER | f. |
| 1 | ← | COPY | f7 | f8 | f5 | f1 | f2 | f0 |
| 2 | CLR | [ | RETURN | ] | f4 | SHIFT | \ | CONTROL |
| 3 | ^ | - | @ | P | ; | : | / | . |
| 4 | 0 | 9 | O | I | L | K | M | , |
| 5 | 8 | 7 | U | Y | H | J | N | SPACE |
| 6 | 6 | 5 | R | T | G | F | B | V |
| 7 | 4 | 3 | E | W | S | D | C | X |
| 8 | 1 | 2 | ESC | Q | TAB | A | CAPS | Z |
| 9 | joy ↑ | joy ↓ | joy ← | joy → | fire (`joy_fire`) | fire 2 | - | DEL |

## Looking

| Option | Writes |
|---|---|
| `-s 100,250` / `-S 50` | `screen-NNNNN.ppm`: the picture the CRTC displays (size from R1/R6/R9, mode and palette per scanline), at those frames / every N frames. 4 PPM pixels per CRTC byte pair = mode 1 resolution: a mode 0 pixel is 2 wide. Convert: `python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"`. |
| `-d 500,1000` | `dump-NNNNN.bin`: the whole machine at the end of those frames (format below). |
| `-b PC` | `bpPPPP-N.bin`: a dump each time PC reaches that address (50 at most). |
| `-x LO-HI` | When execution first enters LO-HI: `ALARM` line in log.txt with the last 256 PCs, and `alarm.bin` (a dump). Catches crashes (`-x 0-ff` = jumped into the zero page) or "when does this routine run". |
| `-w LO-HI` (8 at most) | Every write in that range: `write ADDR=VAL pc=PC f=FRAME` in log.txt. The main tool to find variables and what changes them. One address: `-w fee0`. |
| `-W LO-HI` | Summary of the code that wrote in that range: `writer PC count` at the end of log.txt. |
| `-R LO-HI` | Same for reads: `reader PC count`. Finds the code that uses a table (graphics, map, text...). |
| `-r PC1,PC2 [-X 32]` | regs.txt: `frame scanline PC a= f= bc= de= hl= ix= iy= sp=` each time those PCs run (2 million lines max); `-X N` adds N bytes at IX (object structures). |
| `-t LO-HI` | trace.txt: `frame PC` the first time each address of that range runs: the order code is reached. |

Always written: `log.txt` (FDC commands, sectors read and where their data went, keyboard rows, watched
writes, alarms, readers / writers), `psg.bin` (sound), `coverage.bin` (65536 bytes, 1 = executed),
`regs.txt` (empty without `-r`).

## Changing

| Option | Effect |
|---|---|
| `-F FRAME:ADDR=VAL` (64 at most) | Writes a byte at the start of a frame. Cheats, "play music N", forcing a state. |
| `-p PC:ADDR=VAL` (16) | Writes a byte the first time PC reaches an address: patch a value right when the game has just set it (e.g. the round number when the game picks it), or patch code once it is loaded and before it runs. |
| `-c PC:ADDR=VAL:N` (4) | Each time PC is reached and ADDR is 0, writes VAL again, N times at most: keeps a counter from running out (a timer, the length of an intro pan...). |

## Formats

**log.txt** lines: `write fefb=ed pc=2a4c f=1234`, `fdc cmd 46 00 02 00 41 02 41 2a ff pc=7123`,
`fdc read track 2 side 0 sector 41 (512 bytes) pc=7123`, `  sector data -> 9800` (where the first byte of a
sector went: the loading address), `key row 9 pc=3a10`, `ALARM ...`, `reader 2b10 4096`, `writer 1c20 512`.

**psg.bin**: one 16-byte record per frame: AY registers R0-R13 as they are at the end of the frame, byte 14 =
1 if R13 (envelope shape) was written during the frame (the envelope restarts), byte 15 = 0.
`tools/psg.py` reads it; `emulator/ay psg.bin out.wav FROM TO [channels]` renders frames FROM-TO (channels: bit
mask, 7 = A+B+C) at 44.1 kHz stereo (A left, B centre, C right, like a CPC with stereo output).

**dump-NNNNN.bin** (`tools/cpc.py` `Dump` reads it):

| Offset | Size | Content |
|---|---|---|
| 0 | 131072 | RAM banks 0-7, 16 KB each (bank 0-3 = the base 64 KB) |
| #20000 | 8 | A F B C D E H L |
| #20008 | 8 | IX IY SP PC (little-endian words) |
| #20010 | 1 | screen mode (gate array) |
| #20011 | 17 | palette: hardware colour (0-31) of pens 0-15, then the border |
| #20022 | 18 | CRTC registers R0-R17 |
| #20034 | 4 | RAM bank in each 16 KB of the CPU's view (#0000, #4000, #8000, #C000) |
| #20038 | 3 | lower ROM on, upper ROM on, upper ROM number |
| #20040 | 312 × 17 | palette of each scanline (games change colours mid-screen) |
| … | 312 | mode of each scanline |

The CPU's 64 KB at dump time = banks given by the 4 bytes at #20034 (`Dump.mem`). ROM contents are not dumped.

## Limits

- Timing: 4 MHz with CPC wait states (instructions rounded up to 1 µs), 312 lines of 64 µs, the gate array's
  interrupt every 52 lines resynchronised by the VSYNC. Good for games; not cycle-exact (demos with CRTC tricks).
- Video: per scanline only the palette, the mode and the screen start are captured; split-screen made by
  reprogramming the CRTC mid-frame shows as one area.
- Disc: one drive, reads by sector ID; weak sectors, sector size tricks and other copy protections that need
  exact FDC timing are not emulated. Writes stay in memory (the .dsk file is never changed).
- No tape, no printer, no 464 / 664 differences (it is a 6128), no digitised sound (the AY is logged once per frame).
