# Amstrad CPC cheat sheet (what you need to read a game)

## Memory

- 64 KB seen by the Z80; the 6128 has 128 KB: banks 4-7 are paged in with the gate array (`OUT #7Fxx,#C0+n`,
  n = 0-7: 0 = normal, 1 = bank 7 at #C000, 2 = banks 4-7 everywhere, 4-7 = bank 4-7 at #4000).
- ROMs overlay #0000-#3FFF (OS) and #C000-#FFFF (BASIC or the selected upper ROM, `OUT #DFxx,n`) **for reads
  only**; writes always reach the RAM below. Games usually turn both off and use the whole RAM.
- Screen: 16 KB, normally #C000-#FFFF (CRTC R12/R13 choose the 16 KB page and the start). Games often use a
  smaller screen (R1 = width in words, R6 = height in character rows).

Screen address of byte X (0-79) of line Y (0-199), standard 80 × 200 screen at #C000:
`#C000 + (Y >> 3) * 80 + (Y & 7) * #800 + X`. Other sizes: `start + (Y / (R9+1)) * R1 * 2 + (Y % (R9+1)) * #800 + X`.

## Pixels

| Mode | Resolution | Pens | Pixels per byte | Bits (pixel 0 = left) |
|---|---|---|---|---|
| 0 | 160 × 200 | 16 | 2 | pixel 0: bits 7,3,5,1 (bit 0 of pen first); pixel 1: bits 6,2,4,0 |
| 1 | 320 × 200 | 4 | 4 | pixel i: bit 7-i (pen bit 0), bit 3-i (pen bit 1) |
| 2 | 640 × 200 | 2 | 8 | pixel i: bit 7-i |

`tools/cpc.py` `mode0_pixels(byte)` / `mode1_pixels(byte)` do it. Mode 0 pixels are twice as wide as high: draw
them 2 virtual pixels wide in the remake.

## Colours

27 colours; the gate array uses "hardware" numbers 0-31 (`OUT #7Fxx,#40+hw` after selecting the pen with
`OUT #7Fxx,pen`), BASIC uses "firmware" numbers 0-26. `tools/cpc.py` has both tables (`HW`, `FIRMWARE`).
Each component is 0, 128 or 255.

## Interrupts and timing

4 MHz Z80 (≈ 3.3 MHz effective: every instruction rounded up to 1 µs), 50 Hz frames of 312 lines of 64 µs =
19968 µs. The gate array interrupts every 52 lines: **6 interrupts per frame** (300 Hz), the VSYNC is in PPI
port B bit 0. Games time everything in frames: speeds are "pixels per frame" (× 50 = per second).

## Sound: AY-3-8912 (1 MHz)

Reached through the PPI: register number on port A (#F4xx) then port C (#F6xx) = #C0 (select), value on
port A then port C = #80 (write), port C = 0 (inactive). Registers: R0-R5 tone periods of A, B, C (12 bits:
f = 62500 / period Hz), R6 noise period, R7 mixer (bits 0-2 tone off, 3-5 noise off, active low), R8-R10
volumes (bit 4 = use the envelope), R11-R12 envelope period, R13 envelope shape (writing it restarts it),
R14 = keyboard row data (port A input).

## Keyboard

PPI port C bits 0-3 select the row (0-9), the row is read through AY register 14: a bit at 0 = key pressed.
Matrix: docs/emulator.md. Row 9 = joystick 0.

## Disc

µPD765 at #FB7E (status) / #FB7F (data), motor #FA7E. 40 tracks (3"), 9 sectors of 512 bytes.
- Data format: sectors #C1-#C9, directory at track 0. System format: #41-#49, tracks 0-1 reserved (boot
  sector = track 0 #41, loaded at #0100 by `|CPM`), directory at track 2. IBM: #01-#08.
- Directory: 64 entries of 32 bytes (CP/M), blocks of 1 KB (2 sectors).
- AMSDOS header (first 128 bytes of a binary file): type (byte 18: 0 BASIC, 2 binary), load address (21-22),
  length (24-25), entry (26-27), checksum of bytes 0-66 (67-68).
- `.dsk` files: "MV - CPCEMU" (all tracks the same size) or "EXTENDED" (per-track size, real sector sizes:
  protections). `tools/dsk.py` reads both.

## Firmware

With ROMs, games call the firmware through jump blocks: #BB00-#BB4D keyboard, #BB5A-#BB8A text, #BBBA-#BBFC
graphics, #BC0E-#BC3B screen (#BC0E SCR SET MODE, #BC32 SCR SET INK), #BC77-#BCA4 cassette / disc (CAS IN OPEN
#BC77, CAS IN DIRECT #BC83), #BD19 MC WAIT FLYBACK (wait for the VSYNC). A `call #bc83` in the loader = "load
this file whole": its address is in HL. Many games switch the firmware off right after loading.
