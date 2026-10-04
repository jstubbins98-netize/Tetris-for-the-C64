# Tetris for the Commodore 64

This project is a fully-featured implementation of the classic game Tetris for the Commodore 64. It is written entirely in C and designed to be compiled using the **cc65** cross-compiler.

## Features

- **Hardware Sprites:** Utilizes C64 hardware sprites (0-3) for smooth rendering of the active falling tetromino.
- **SID Chip Audio:** 
  - Features a multi-voice rendition of *Korobeiniki* (Tetris Theme A) using the C64's SID chip.
  - Authentic sound effects for movement, rotation, locking, line clears, scoring a Tetris, and game over.
- **Classic Mechanics:** Includes all 7 standard tetrominoes, rotation with wall kicks, Delayed Auto Shift (DAS) for smooth movement, soft/hard drops, line clearing, and a classic scoring system.
- **Difficulty Curve:** Features a 20-level speed curve that gets progressively faster as you clear lines.

## Prerequisites

To compile and play this game, you will need:
- [cc65](https://cc65.github.io/) - A freeware C compiler for 6502 based systems.
- A Commodore 64 emulator like [VICE](https://vice-emu.sourceforge.io/), or a way to transfer the compiled `.prg` file to real C64 hardware.

## Building the Game

The entire source code is contained within the `tetris.c` file. 

You can build the game manually via the command line using cc65:

```bash
cl65 -O -t c64 -o tetris.prg tetris.c
```

or use the provided Makefile:

```bash
make
```

This will output a `tetris.prg` executable file that can be loaded directly into your emulator or onto a real Commodore 64.

## Controls

The game supports both Keyboard and Joystick (Port 2) inputs.

| Action | Keyboard | Joystick (Port 2) |
| :--- | :--- | :--- |
| **Move Left** | `A` or `CRSR LEFT` | Left |
| **Move Right** | `D` or `CRSR RIGHT` | Right |
| **Rotate** | `W` or `CRSR UP` | Fire Button |
| **Soft Drop** | `S` or `CRSR DOWN` | Down |
| **Hard Drop** | `SPACE` | *N/A* |
| **Pause** | `P` | *N/A* |

*Note: On the title screen, press **SPACE** or the Joystick **Fire** button to start the game.*

## Technical Details

- **Video Memory:** Screen RAM is mapped to `0x0400` and Color RAM to `0xD800`.
- **Sprite Memory:** Sprite pointers are located at `0x07F8` with sprite data stored safely at `0x3C00` to avoid interfering with the compiled program image.
- **Timing:** Game loop and music/audio synchronization are tied to the CIA #1 jiffy clock (approx. 60Hz frame counter).
