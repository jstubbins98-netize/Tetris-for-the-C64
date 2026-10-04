/*
 * Tetris for the Commodore 64
 * Written in C for the cc65 compiler
 *
 * Features:
 *  - Hardware sprites for the active falling tetromino (sprites 0-3)
 *  - SID chip music: Korobeiniki (Tetris Theme A) on title screen
 *  - SID sound effects during gameplay
 *  - Full Tetris: 7 tetrominoes, rotation with wall kicks, DAS,
 *    soft/hard drop, line clearing, scoring, 20-level speed curve
 *
 * Build:
 *   make          (uses Makefile)
 *   cl65 -O -t c64 -o tetris.prg tetris.c   (manual)
 *
 * Controls:
 *   A / CRSR LEFT   Move left
 *   D / CRSR RIGHT  Move right
 *   S / CRSR DOWN   Soft drop
 *   W / CRSR UP     Rotate
 *   SPACE           Hard drop
 *   P               Pause
 *   Joystick 2      Fire = rotate
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <c64.h>
#include <conio.h>
#include <joystick.h>

/*
 * cc65 provides uint8_t / int8_t etc. via its own platform headers.
 * Do NOT include <stdint.h> separately — it causes redefinition errors.
 */

/* =========================================================
 * C64 HARDWARE ACCESS
 *
 * cc65 exposes the VIC-II and SID as extern structs:
 *   VIC   (struct __vic2)   at 0xD000
 *   SID   (struct __sid)    at 0xD400
 *
 * Use VIC.member and SID.v1.member rather than raw pointer macros
 * so we never clash with anything cc65's own headers define.
 * ========================================================= */

/* Sprite pointer table lives at top of screen RAM (0x07F8 in default bank) */
#define SPR_PTRS        ((unsigned char *)0x07F8)

/* Sprite data: 4 slots × 64 bytes each starting at 0x3C00.
 *
 * IMPORTANT: cc65 loads the C64 binary at $0801 and the compiled
 * code+data grows upward. 0x0C00 is well inside the program image
 * and writing there corrupts live machine code. 0x3C00 is near the
 * top of VIC bank 0 ($0000-$3FFF) and safely above any realistic
 * binary size produced from this source.
 *
 * Pointer register value = address / 64
 * 0x3C00 / 64 = 0xF0 = 240
 */
#define SPR_DATA_BASE   ((unsigned char *)0x3C00)
#define SPR_PTR_VAL0    0xF0u   /* pointer register value for slot 0 */
#define SPR_BLOCK       64u

/* Screen RAM and color RAM — named C64_SCR / C64_COL to avoid any
 * clash with cc65/conio definitions of SCREEN_RAM / COLOR_RAM.       */
#define C64_SCR   ((unsigned char *)0x0400)
#define C64_COL   ((unsigned char *)0xD800)

/* CIA #1 jiffy (frame) counter written by the KERNAL IRQ at ~60 Hz   */
#define JIFFY   (*(volatile unsigned char *)0xA2)

/* =========================================================
 * SID CONTROL-REGISTER BIT CONSTANTS
 * (Named SCTRL_* so they never clash with anything in cc65 headers)
 * ========================================================= */
#define SCTRL_GATE  0x01u
#define SCTRL_SYNC  0x02u
#define SCTRL_RING  0x04u
#define SCTRL_TEST  0x08u
#define SCTRL_TRI   0x10u
#define SCTRL_SAW   0x20u
#define SCTRL_PULSE 0x40u
#define SCTRL_NOISE 0x80u

/* =========================================================
 * C64 COLOUR CONSTANTS  (use our own names; conio.h uses COLOR_*)
 * ========================================================= */
#define COL_BLACK      0u
#define COL_WHITE      1u
#define COL_RED        2u
#define COL_CYAN       3u
#define COL_PURPLE     4u
#define COL_GREEN      5u
#define COL_BLUE       6u
#define COL_YELLOW     7u
#define COL_ORANGE     8u
#define COL_BROWN      9u
#define COL_LTRED     10u
#define COL_DKGREY    11u
#define COL_GREY      12u
#define COL_LTGREEN   13u
#define COL_LTBLUE    14u
#define COL_LTGREY    15u

/* =========================================================
 * GAME CONSTANTS
 * ========================================================= */
#define BOARD_W   10
#define BOARD_H   20

/* Screen cell where the board's top-left corner is drawn */
#define BOARD_COL  15
#define BOARD_ROW   2

/*
 * VIC-II pixel coordinate of the top-left character cell:
 *   column 0 -> pixel X 24  (first visible pixel on NTSC/PAL)
 *   row    0 -> pixel Y 50
 * Each text cell is 8×8 pixels.
 */
#define VIC_XOFF  24u
#define VIC_YOFF  50u

/* Pixel origin of the board */
#define BOARD_PX  ((unsigned int)(BOARD_COL * 8u + VIC_XOFF))
#define BOARD_PY  ((unsigned char)(BOARD_ROW * 8u + VIC_YOFF))

/* PETSCII characters used for the board */
#define CH_EMPTY  0x20u   /* space       */
#define CH_BLOCK  0xA0u   /* reverse spc */
#define CH_WALL   0xDDu   /* right-bar   */
#define CH_FLOOR  0xC0u   /* horiz bar   */

/* Number of hardware sprites used for the active piece */
#define N_SPR  4u

/* =========================================================
 * SID NOTE FREQUENCY TABLE  (NTSC C64, clock = 1 022 727 Hz)
 *
 *   reg = note_hz * 2^24 / 1022727
 *
 * Index 0 = REST (silence on that voice).
 * ========================================================= */
#define NOTE_REST   0
#define NOTE_C3     1
#define NOTE_D3     2
#define NOTE_E3     3
#define NOTE_F3     4
#define NOTE_G3     5
#define NOTE_A3     6
#define NOTE_B3     7
#define NOTE_C4     8
#define NOTE_D4     9
#define NOTE_E4    10
#define NOTE_F4    11
#define NOTE_G4    12
#define NOTE_A4    13
#define NOTE_B4    14
#define NOTE_C5    15
#define NOTE_D5    16
#define NOTE_E5    17
#define NOTE_F5    18
#define NOTE_G5    19
#define NOTE_A5    20
#define NOTE_B5    21
#define NOTE_NUM   22

static const unsigned int sid_freq[NOTE_NUM] = {
       0,    /* REST */
    2144,    /* C3  130.81 Hz */
    2407,    /* D3  146.83 Hz */
    2702,    /* E3  164.81 Hz */
    2863,    /* F3  174.61 Hz */
    3214,    /* G3  196.00 Hz */
    3608,    /* A3  220.00 Hz */
    4049,    /* B3  246.94 Hz */
    4289,    /* C4  261.63 Hz */
    4813,    /* D4  293.66 Hz */
    5405,    /* E4  329.63 Hz */
    5727,    /* F4  349.23 Hz */
    6427,    /* G4  392.00 Hz */
    7216,    /* A4  440.00 Hz */
    8097,    /* B4  493.88 Hz */
    8577,    /* C5  523.25 Hz */
    9627,    /* D5  587.33 Hz */
   10809,    /* E5  659.26 Hz */
   11453,    /* F5  698.46 Hz */
   12855,    /* G5  783.99 Hz */
   14432,    /* A5  880.00 Hz */
   16195,    /* B5  987.77 Hz */
};

/* =========================================================
 * MUSIC: KOROBEINIKI (TETRIS THEME A)
 *
 * Each entry: { note_index, duration_in_jiffies }
 *   6  = sixteenth note
 *  12  = eighth note
 *  24  = quarter note
 *  48  = half note
 * Tempo ≈ 150 BPM at 60 Hz (quarter = 24 ticks).
 *
 * MUSIC_END sentinel causes the sequence to loop.
 * ========================================================= */
#define MUSIC_END 0xFFu

typedef struct { unsigned char note; unsigned char dur; } MNote;

/* Voice 1 – melody */
static const MNote melody[] = {
    /* Phrase 1 */
    { NOTE_E5, 24 }, { NOTE_B4, 12 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_C5, 12 }, { NOTE_B4, 12 },
    { NOTE_A4, 24 }, { NOTE_A4, 12 }, { NOTE_C5, 12 },
    { NOTE_E5, 24 }, { NOTE_D5, 12 }, { NOTE_C5, 12 },
    /* Phrase 2 */
    { NOTE_B4, 36 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_E5, 24 },
    { NOTE_C5, 24 }, { NOTE_A4, 24 },
    { NOTE_A4, 48 },
    /* Phrase 3 */
    { NOTE_REST,12}, { NOTE_D5, 24 }, { NOTE_F5, 12 },
    { NOTE_A5, 24 }, { NOTE_G5, 12 }, { NOTE_F5, 12 },
    { NOTE_E5, 36 }, { NOTE_C5, 12 },
    { NOTE_E5, 24 }, { NOTE_D5, 12 }, { NOTE_C5, 12 },
    /* Phrase 4 */
    { NOTE_B4, 24 }, { NOTE_B4, 12 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_E5, 24 },
    { NOTE_C5, 24 }, { NOTE_A4, 24 },
    { NOTE_A4, 48 },
    /* Phrase 5 (repeat) */
    { NOTE_E5, 24 }, { NOTE_B4, 12 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_C5, 12 }, { NOTE_B4, 12 },
    { NOTE_A4, 24 }, { NOTE_A4, 12 }, { NOTE_C5, 12 },
    { NOTE_E5, 24 }, { NOTE_D5, 12 }, { NOTE_C5, 12 },
    /* Phrase 6 */
    { NOTE_B4, 36 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_E5, 24 },
    { NOTE_C5, 24 }, { NOTE_A4, 24 },
    { NOTE_A4, 48 },
    /* Phrase 7 */
    { NOTE_REST,12}, { NOTE_D5, 24 }, { NOTE_F5, 12 },
    { NOTE_A5, 24 }, { NOTE_G5, 12 }, { NOTE_F5, 12 },
    { NOTE_E5, 36 }, { NOTE_C5, 12 },
    { NOTE_E5, 24 }, { NOTE_D5, 12 }, { NOTE_C5, 12 },
    /* Phrase 8 */
    { NOTE_B4, 24 }, { NOTE_B4, 12 }, { NOTE_C5, 12 },
    { NOTE_D5, 24 }, { NOTE_E5, 24 },
    { NOTE_C5, 24 }, { NOTE_A4, 24 },
    { NOTE_A4, 48 },
    { MUSIC_END, 0 }
};

/* Voice 2 – bass */
static const MNote bassline[] = {
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_D3, 48 }, { NOTE_A3, 24 }, { NOTE_D4, 24 },
    { NOTE_C4, 24 }, { NOTE_A3, 24 }, { NOTE_E3, 48 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_F3, 48 }, { NOTE_C4, 24 }, { NOTE_F3, 24 },
    { NOTE_E3, 24 }, { NOTE_A3, 24 }, { NOTE_A3, 48 },
    /* second half */
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_D3, 48 }, { NOTE_A3, 24 }, { NOTE_D4, 24 },
    { NOTE_C4, 24 }, { NOTE_A3, 24 }, { NOTE_E3, 48 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_A3, 48 }, { NOTE_E3, 24 }, { NOTE_A3, 24 },
    { NOTE_F3, 48 }, { NOTE_C4, 24 }, { NOTE_F3, 24 },
    { NOTE_E3, 24 }, { NOTE_A3, 24 }, { NOTE_A3, 48 },
    { MUSIC_END, 0 }
};

/* =========================================================
 * MUSIC PLAYER STATE
 * ========================================================= */
typedef struct {
    const MNote   *seq;
    unsigned int   idx;
    unsigned char  timer;
    unsigned char  active;
} VoiceState;

static VoiceState  v1st, v2st;
static unsigned char music_on = 0;

/* =========================================================
 * SOUND EFFECTS  (Voice 3)
 * ========================================================= */
#define SFX_NONE     0
#define SFX_MOVE     1
#define SFX_ROTATE   2
#define SFX_LOCK     3
#define SFX_LINE     4
#define SFX_TETRIS   5
#define SFX_GAMEOVER 6

static unsigned char sfx_timer = 0;
static unsigned char sfx_type  = 0;

/* =========================================================
 * TETROMINO DEFINITIONS
 *
 * 7 pieces × 4 rotations × 4 rows.
 * Each row entry is a 4-bit column mask; bit3 = leftmost column.
 * ========================================================= */
#define N_PIECES 7

static const unsigned char piece_colors[N_PIECES] = {
    COL_CYAN,    /* I */
    COL_YELLOW,  /* O */
    COL_PURPLE,  /* T */
    COL_GREEN,   /* S */
    COL_RED,     /* Z */
    COL_BLUE,    /* J */
    COL_ORANGE   /* L */
};

static const unsigned char pieces[N_PIECES][4][4] = {
    /* 0: I */
    { { 0x0,0xF,0x0,0x0 }, { 0x2,0x2,0x2,0x2 },
      { 0x0,0xF,0x0,0x0 }, { 0x2,0x2,0x2,0x2 } },
    /* 1: O */
    { { 0x0,0x6,0x6,0x0 }, { 0x0,0x6,0x6,0x0 },
      { 0x0,0x6,0x6,0x0 }, { 0x0,0x6,0x6,0x0 } },
    /* 2: T */
    { { 0x0,0xE,0x4,0x0 }, { 0x0,0x4,0x6,0x4 },
      { 0x0,0x4,0xE,0x0 }, { 0x0,0x4,0xC,0x4 } },
    /* 3: S */
    { { 0x0,0x6,0xC,0x0 }, { 0x0,0x4,0x6,0x2 },
      { 0x0,0x6,0xC,0x0 }, { 0x0,0x4,0x6,0x2 } },
    /* 4: Z */
    { { 0x0,0xC,0x6,0x0 }, { 0x0,0x2,0x6,0x4 },
      { 0x0,0xC,0x6,0x0 }, { 0x0,0x2,0x6,0x4 } },
    /* 5: J */
    { { 0x0,0xE,0x2,0x0 }, { 0x0,0x4,0x4,0xC },
      { 0x0,0x8,0xE,0x0 }, { 0x0,0x6,0x4,0x4 } },
    /* 6: L */
    { { 0x0,0xE,0x8,0x0 }, { 0x0,0xC,0x4,0x4 },
      { 0x0,0x2,0xE,0x0 }, { 0x0,0x4,0x4,0x6 } },
};

/* =========================================================
 * GAME STATE
 * ========================================================= */
static unsigned char board[BOARD_H][BOARD_W]; /* 0=empty, 1-7=piece+1  */

static signed char   piece_x, piece_y;
static unsigned char piece_type, piece_rot;
static unsigned char next_type;

static unsigned long score;
static unsigned int  lines;
static unsigned char level;

static unsigned char game_over;
static unsigned char paused;
static unsigned char gravity_timer;
static unsigned char gravity_delay;
static unsigned char das_timer;

static unsigned char rng_state;

/* =========================================================
 * SPRITE BLOCK BITMAP  (24×21 = 63 bytes)
 *
 * Represents a single 8×8 block in the top-left area of the sprite.
 * Rows 1-6 have the leftmost 6 bits set (0x7E in byte 0).
 * ========================================================= */
static const unsigned char block_spr[63] = {
    0x00,0x00,0x00,  /* row  0 */
    0x7E,0x00,0x00,  /* row  1 */
    0x7E,0x00,0x00,  /* row  2 */
    0x7E,0x00,0x00,  /* row  3 */
    0x7E,0x00,0x00,  /* row  4 */
    0x7E,0x00,0x00,  /* row  5 */
    0x7E,0x00,0x00,  /* row  6 */
    0x00,0x00,0x00,  /* row  7 */
    0x00,0x00,0x00,  /* row  8 */
    0x00,0x00,0x00,  /* row  9 */
    0x00,0x00,0x00,  /* row 10 */
    0x00,0x00,0x00,  /* row 11 */
    0x00,0x00,0x00,  /* row 12 */
    0x00,0x00,0x00,  /* row 13 */
    0x00,0x00,0x00,  /* row 14 */
    0x00,0x00,0x00,  /* row 15 */
    0x00,0x00,0x00,  /* row 16 */
    0x00,0x00,0x00,  /* row 17 */
    0x00,0x00,0x00,  /* row 18 */
    0x00,0x00,0x00,  /* row 19 */
    0x00,0x00,0x00   /* row 20 */
};

/* =========================================================
 * SPRITE HELPERS
 * ========================================================= */

/*
 * Set VIC-II X/Y registers for sprite n.
 * x can exceed 255; we use the MSB register (spr_hi_x) for bit 8.
 */
static void spr_setpos(unsigned char n, unsigned int x, unsigned char y)
{
    unsigned char *xreg = (unsigned char *)(0xD000u + (unsigned int)n * 2u);
    unsigned char *yreg = (unsigned char *)(0xD001u + (unsigned int)n * 2u);

    if (x > 255u) {
        *xreg = (unsigned char)(x - 256u);
        VIC.spr_hi_x |=  (unsigned char)(1u << n);
    } else {
        *xreg = (unsigned char)x;
        VIC.spr_hi_x &= (unsigned char)~(1u << n);
    }
    *yreg = y;
}

/* Set colour register for sprite n (0xD027 + n) */
static void spr_setcol(unsigned char n, unsigned char col)
{
    unsigned char *creg = (unsigned char *)(0xD027u + n);
    *creg = col;
}

/* =========================================================
 * SPRITE INITIALISATION
 * ========================================================= */
static void sprites_init(void)
{
    unsigned char s, i;
    unsigned char *dst;

    /* Write block sprite data into the four sprite slots */
    for (s = 0; s < N_SPR; s++) {
        dst = SPR_DATA_BASE + (unsigned int)s * SPR_BLOCK;
        for (i = 0; i < 63u; i++) {
            dst[i] = block_spr[i];
        }
        dst[63] = 0;
        SPR_PTRS[s] = (unsigned char)(SPR_PTR_VAL0 + s);
    }

    VIC.spr_ena    = 0x00;
    VIC.spr_hi_x   = 0x00;
    VIC.spr_bg_prio= 0x00;   /* sprites in front of background */
    VIC.spr_mcolor = 0x00;   /* single colour */
    VIC.spr_exp_x  = 0x00;
    VIC.spr_exp_y  = 0x00;
}

static void sprites_show(void)
{
    unsigned char s;
    VIC.spr_ena |= 0x0Fu;
    for (s = 0; s < N_SPR; s++) {
        spr_setcol(s, piece_colors[piece_type]);
    }
}

static void sprites_hide(void)
{
    VIC.spr_ena &= 0xF0u;
}

/*
 * Walk the 4×4 rotation mask and position one sprite per filled cell.
 * Uses the current piece_x / piece_y / piece_type / piece_rot globals.
 */
static void sprites_update(void)
{
    unsigned char row, col, bit, spr;
    unsigned char mask;
    signed char   bx, by;
    unsigned int  px;
    unsigned char py;

    spr = 0;
    for (row = 0; row < 4u && spr < N_SPR; row++) {
        mask = pieces[piece_type][piece_rot][row];
        for (col = 0; col < 4u && spr < N_SPR; col++) {
            bit = (unsigned char)((mask >> (3u - col)) & 1u);
            if (!bit) continue;
            bx = piece_x + (signed char)col;
            by = piece_y + (signed char)row;
            if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
                px = BOARD_PX + (unsigned int)(unsigned char)bx * 8u;
                py = (unsigned char)(BOARD_PY + (unsigned char)by * 8u);
                spr_setpos(spr, px, py);
            }
            spr++;
        }
    }
    sprites_show();
}

/* =========================================================
 * SID HELPERS
 * ========================================================= */
static void sid_silence(void)
{
    SID.v1.ctrl = 0;
    SID.v2.ctrl = 0;
    SID.v3.ctrl = 0;
    SID.amp     = 0;
}

/*
 * Trigger a note on one of the three SID voices (0, 1, or 2).
 * freq       : SID frequency register value
 * waveform   : SCTRL_TRI / SCTRL_SAW / SCTRL_PULSE / SCTRL_NOISE
 * ad         : attack (hi nibble) | decay  (lo nibble)
 * sr         : sustain(hi nibble) | release(lo nibble)
 */
static void sid_noteon(unsigned char voice, unsigned int freq,
                       unsigned char waveform,
                       unsigned char ad, unsigned char sr)
{
    if (voice == 0u) {
        SID.v1.freq = freq;
        SID.v1.pw   = 0x0800u;
        SID.v1.ad   = ad;
        SID.v1.sr   = sr;
        SID.v1.ctrl = (unsigned char)(waveform | SCTRL_GATE);
    } else if (voice == 1u) {
        SID.v2.freq = freq;
        SID.v2.pw   = 0x0800u;
        SID.v2.ad   = ad;
        SID.v2.sr   = sr;
        SID.v2.ctrl = (unsigned char)(waveform | SCTRL_GATE);
    } else {
        SID.v3.freq = freq;
        SID.v3.pw   = 0x0800u;
        SID.v3.ad   = ad;
        SID.v3.sr   = sr;
        SID.v3.ctrl = (unsigned char)(waveform | SCTRL_GATE);
    }
}

static void sid_noteoff(unsigned char voice)
{
    if      (voice == 0u) SID.v1.ctrl &= (unsigned char)~SCTRL_GATE;
    else if (voice == 1u) SID.v2.ctrl &= (unsigned char)~SCTRL_GATE;
    else                  SID.v3.ctrl &= (unsigned char)~SCTRL_GATE;
}

/* =========================================================
 * MUSIC PLAYER
 * ========================================================= */
static void music_start(void)
{
    v1st.seq    = melody;
    v1st.idx    = 0;
    v1st.timer  = 0;
    v1st.active = 1;

    v2st.seq    = bassline;
    v2st.idx    = 0;
    v2st.timer  = 0;
    v2st.active = 1;

    SID.amp  = 0x0Fu;   /* max volume, no filter */
    music_on = 1;
}

static void music_stop(void)
{
    music_on = 0;
    sid_silence();
}

/* Advance one voice by one jiffy; loops on MUSIC_END sentinel */
static void voice_tick(VoiceState *vs, unsigned char sid_voice,
                       unsigned char waveform,
                       unsigned char ad, unsigned char sr)
{
    unsigned char note, dur;

    if (!vs->active) return;

    if (vs->timer == 0u) {
        note = vs->seq[vs->idx].note;
        dur  = vs->seq[vs->idx].dur;

        if (note == MUSIC_END) {
            vs->idx = 0;
            note = vs->seq[0].note;
            dur  = vs->seq[0].dur;
        }
        vs->timer = dur;
        vs->idx++;

        if (note == NOTE_REST) {
            sid_noteoff(sid_voice);
        } else {
            sid_noteon(sid_voice, sid_freq[note], waveform, ad, sr);
        }
    }
    vs->timer--;
}

/* Call once per jiffy */
static void music_tick(void)
{
    if (!music_on) return;
    voice_tick(&v1st, 0u, SCTRL_TRI,  0x02u, 0xA4u);
    voice_tick(&v2st, 1u, SCTRL_SAW,  0x03u, 0x74u);
}

/* =========================================================
 * SOUND EFFECTS  (SID voice 2 — index 2)
 * ========================================================= */
static void sfx_play(unsigned char type)
{
    sfx_type  = type;
    sfx_timer = 0;

    switch (type) {
    case SFX_MOVE:
        sid_noteon(2u, sid_freq[NOTE_A5],    SCTRL_PULSE, 0x00u, 0xF2u);
        sfx_timer = 4;  break;
    case SFX_ROTATE:
        sid_noteon(2u, sid_freq[NOTE_B5],    SCTRL_PULSE, 0x00u, 0xF2u);
        sfx_timer = 5;  break;
    case SFX_LOCK:
        sid_noteon(2u, sid_freq[NOTE_A3],    SCTRL_NOISE, 0x00u, 0xF4u);
        sfx_timer = 8;  break;
    case SFX_LINE:
        sid_noteon(2u, sid_freq[NOTE_E5],    SCTRL_SAW,   0x00u, 0xF3u);
        sfx_timer = 15; break;
    case SFX_TETRIS:
        sid_noteon(2u, sid_freq[NOTE_A5],    SCTRL_TRI,   0x00u, 0xA4u);
        sfx_timer = 30; break;
    case SFX_GAMEOVER:
        sid_noteon(2u, sid_freq[NOTE_E4],    SCTRL_NOISE, 0x04u, 0x33u);
        sfx_timer = 60; break;
    default: break;
    }
}

static void sfx_tick(void)
{
    if (sfx_timer > 0u) {
        sfx_timer--;
        if (sfx_timer == 0u) {
            sid_noteoff(2u);
            sfx_type = SFX_NONE;
        }
    }
}

/* =========================================================
 * DISPLAY UTILITIES
 * ========================================================= */
static void scr_put(unsigned char col, unsigned char row,
                    unsigned char ch,  unsigned char color)
{
    unsigned int off = (unsigned int)row * 40u + col;
    C64_SCR[off] = ch;
    C64_COL[off] = color;
}

static void scr_puts(unsigned char col, unsigned char row,
                     const char *s, unsigned char color)
{
    while (*s) {
        scr_put(col++, row, (unsigned char)*s++, color);
    }
}

static void scr_fill(unsigned char col, unsigned char row,
                     unsigned char w,   unsigned char h,
                     unsigned char ch,  unsigned char color)
{
    unsigned char r, c;
    for (r = row; r < row + h; r++) {
        for (c = col; c < col + w; c++) {
            scr_put(c, r, ch, color);
        }
    }
}

static void scr_clear(void)
{
    unsigned int i;
    for (i = 0; i < 1000u; i++) {
        C64_SCR[i] = CH_EMPTY;
        C64_COL[i] = COL_LTGREY;
    }
}

static void draw_border(void)
{
    unsigned char r, c;

    for (r = 0; r < BOARD_H; r++) {
        scr_put(BOARD_COL - 1, BOARD_ROW + r, CH_WALL, COL_WHITE);
        scr_put(BOARD_COL + BOARD_W, BOARD_ROW + r, CH_WALL, COL_WHITE);
    }
    for (c = 0; c < BOARD_W + 2u; c++) {
        scr_put(BOARD_COL - 1 + c, BOARD_ROW - 1,       CH_FLOOR, COL_WHITE);
        scr_put(BOARD_COL - 1 + c, BOARD_ROW + BOARD_H, CH_FLOOR, COL_WHITE);
    }
}

static void draw_ui_labels(void)
{
    scr_puts(1,  3, "TETRIS", COL_YELLOW);
    scr_puts(1,  5, "SCORE",  COL_LTBLUE);
    scr_puts(1,  9, "LINES",  COL_LTBLUE);
    scr_puts(1, 13, "LEVEL",  COL_LTBLUE);
    scr_puts(27, 3, "NEXT",   COL_LTBLUE);
}

static void draw_stats(void)
{
    char buf[12];
    unsigned long s;
    unsigned char i;

    s = score;
    buf[10] = '\0';
    for (i = 9u; i < 255u; i--) {
        buf[i] = (char)('0' + (unsigned char)(s % 10u));
        s /= 10u;
        if (s == 0u && i < 9u) break;
    }
    scr_puts(1, 7, buf + i, COL_WHITE);

    /* lines — 5 digits */
    buf[5] = '\0';
    buf[4] = (char)('0' + (unsigned char)(lines % 10u));
    buf[3] = (char)('0' + (unsigned char)((lines / 10u)   % 10u));
    buf[2] = (char)('0' + (unsigned char)((lines / 100u)  % 10u));
    buf[1] = (char)('0' + (unsigned char)((lines / 1000u) % 10u));
    buf[0] = (char)('0' + (unsigned char)(lines / 10000u));
    scr_puts(1, 11, buf, COL_WHITE);

    /* level — 2 digits */
    buf[2] = '\0';
    buf[1] = (char)('0' + (unsigned char)(level % 10u));
    buf[0] = (char)('0' + (unsigned char)(level / 10u));
    scr_puts(1, 15, buf, COL_WHITE);
}

static void draw_board(void)
{
    unsigned char row, col, cell;
    for (row = 0; row < BOARD_H; row++) {
        for (col = 0; col < BOARD_W; col++) {
            cell = board[row][col];
            if (cell == 0u) {
                scr_put(BOARD_COL + col, BOARD_ROW + row, CH_EMPTY, COL_BLACK);
            } else {
                scr_put(BOARD_COL + col, BOARD_ROW + row,
                        CH_BLOCK, piece_colors[cell - 1u]);
            }
        }
    }
}

static void draw_next(void)
{
    unsigned char row, col, mask;
    scr_fill(27u, 5u, 4u, 4u, CH_EMPTY, COL_BLACK);
    for (row = 0; row < 4u; row++) {
        mask = pieces[next_type][0][row];
        for (col = 0; col < 4u; col++) {
            if ((mask >> (3u - col)) & 1u) {
                scr_put((unsigned char)(27u + col),
                        (unsigned char)(5u  + row),
                        CH_BLOCK, piece_colors[next_type]);
            }
        }
    }
}

/* =========================================================
 * PSEUDO-RANDOM (xorshift-8)
 * ========================================================= */
static unsigned char rng_next(void)
{
    rng_state ^= (unsigned char)(rng_state << 1u);
    rng_state ^= (unsigned char)(rng_state >> 1u);
    rng_state ^= (unsigned char)(rng_state << 4u);
    return rng_state;
}

static unsigned char rng_piece(void)
{
    return (unsigned char)(rng_next() % N_PIECES);
}

/* =========================================================
 * COLLISION DETECTION
 * ========================================================= */
static unsigned char piece_collides(signed char nx, signed char ny,
                                    unsigned char rot)
{
    unsigned char row, col, bit, mask;
    signed char   bx, by;

    for (row = 0; row < 4u; row++) {
        mask = pieces[piece_type][rot][row];
        for (col = 0; col < 4u; col++) {
            bit = (unsigned char)((mask >> (3u - col)) & 1u);
            if (!bit) continue;
            bx = nx + (signed char)col;
            by = ny + (signed char)row;
            if (bx < 0 || bx >= BOARD_W)             return 1;
            if (by >= BOARD_H)                         return 1;
            if (by >= 0 && board[by][bx] != 0u)       return 1;
        }
    }
    return 0;
}

/* =========================================================
 * PIECE MANAGEMENT
 * ========================================================= */
static void piece_spawn(unsigned char type)
{
    piece_type = type;
    piece_rot  = 0;
    piece_x    = (signed char)(BOARD_W / 2 - 2);
    piece_y    = -1;
}

static void piece_lock(void)
{
    unsigned char row, col, bit, mask;
    signed char   bx, by;

    sfx_play(SFX_LOCK);

    for (row = 0; row < 4u; row++) {
        mask = pieces[piece_type][piece_rot][row];
        for (col = 0; col < 4u; col++) {
            bit = (unsigned char)((mask >> (3u - col)) & 1u);
            if (!bit) continue;
            bx = piece_x + (signed char)col;
            by = piece_y + (signed char)row;
            if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
                board[by][bx] = (unsigned char)(piece_type + 1u);
            }
        }
    }
}

/* Clear completed lines; return count */
static unsigned char clear_lines(void)
{
    unsigned char row, col, full, cleared, dst;

    cleared = 0;
    for (row = 0; row < BOARD_H; row++) {
        full = 1;
        for (col = 0; col < BOARD_W; col++) {
            if (board[row][col] == 0u) { full = 0; break; }
        }
        if (full) {
            for (dst = row; dst > 0u; dst--) {
                for (col = 0; col < BOARD_W; col++) {
                    board[dst][col] = board[dst - 1u][col];
                }
            }
            for (col = 0; col < BOARD_W; col++) {
                board[0][col] = 0;
            }
            cleared++;
            /* re-check same row index (now holds what was above) */
            row--;
        }
    }
    return cleared;
}

static void add_score(unsigned char cleared)
{
    static const unsigned int pts[5] = { 0, 40, 100, 300, 1200 };

    if (cleared == 0u) return;
    if (cleared > 4u)  cleared = 4;

    score += (unsigned long)pts[cleared] * (unsigned long)(level + 1u);
    lines += cleared;
    level  = (unsigned char)(lines / 10u);
    if (level > 19u) level = 19;

    gravity_delay = (level < 10u)
                    ? (unsigned char)(48u - level * 4u)
                    : 4u;

    sfx_play(cleared >= 4u ? SFX_TETRIS : SFX_LINE);
}

/* Hard-drop: fall until collision, score 2 per row */
static void hard_drop(void)
{
    unsigned int dropped = 0;
    while (!piece_collides(piece_x, (signed char)(piece_y + 1), piece_rot)) {
        piece_y++;
        dropped++;
    }
    score += dropped * 2u;
}

/* =========================================================
 * INPUT
 * ========================================================= */
#define KEY_LEFT   0x01u
#define KEY_RIGHT  0x02u
#define KEY_DOWN   0x04u
#define KEY_UP     0x08u
#define KEY_SPACE  0x10u
#define KEY_P      0x20u

static unsigned char read_input(void)
{
    unsigned char keys = 0;
    unsigned char joy;

    if (kbhit()) {
        unsigned char ch = (unsigned char)cgetc();
        switch (ch) {
        case 'a': case 'A':  keys |= KEY_LEFT;  break;
        case 'd': case 'D':  keys |= KEY_RIGHT; break;
        case 's': case 'S':  keys |= KEY_DOWN;  break;
        case 'w': case 'W':  keys |= KEY_UP;    break;
        case ' ':            keys |= KEY_SPACE;  break;
        case 'p': case 'P':  keys |= KEY_P;     break;
        case  29:            keys |= KEY_RIGHT; break;  /* CRSR RIGHT */
        case 157:            keys |= KEY_LEFT;  break;  /* CRSR LEFT  */
        case  17:            keys |= KEY_DOWN;  break;  /* CRSR DOWN  */
        case 145:            keys |= KEY_UP;    break;  /* CRSR UP    */
        default: break;
        }
    }

    joy = joy_read(JOY_2);
    if (joy & JOY_LEFT_MASK)   keys |= KEY_LEFT;
    if (joy & JOY_RIGHT_MASK)  keys |= KEY_RIGHT;
    if (joy & JOY_DOWN_MASK)   keys |= KEY_DOWN;
    if (joy & JOY_BTN_1_MASK)  keys |= KEY_UP;

    return keys;
}

/* =========================================================
 * JIFFY HELPERS
 * ========================================================= */

/* Spin until the jiffy counter advances once (~1/60 s) */
static void wait_jiffy(void)
{
    unsigned char start = JIFFY;
    while (JIFFY == start) { /* busy wait */ }
}

/* Wait n jiffies while ticking music/sfx */
static void wait_jiffies(unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        wait_jiffy();
        music_tick();
        sfx_tick();
    }
}

/* =========================================================
 * TITLE SCREEN
 * ========================================================= */
static void title_screen(void)
{
    scr_clear();
    VIC.bordercolor = COL_DKGREY;
    VIC.bgcolor0    = COL_BLACK;

    /* ASCII art banner */
    scr_puts(8, 4,  "####### ####### ####### #####",   COL_YELLOW);
    scr_puts(8, 5,  "   #    #          #    #    #",  COL_YELLOW);
    scr_puts(8, 6,  "   #    #####      #    #####",   COL_YELLOW);
    scr_puts(8, 7,  "   #    #          #    #   #",   COL_YELLOW);
    scr_puts(8, 8,  "   #    #######    #    #    #",  COL_YELLOW);

    scr_puts(8, 9,  "  ######  ######",                COL_LTBLUE);
    scr_puts(8, 10, "  #        #",                    COL_LTBLUE);
    scr_puts(8, 11, "  ######   ######",               COL_LTBLUE);
    scr_puts(8, 12, "       #        #",               COL_LTBLUE);
    scr_puts(8, 13, "  ######   ######",               COL_LTBLUE);

    scr_puts(11, 15, "FOR THE COMMODORE 64", COL_CYAN);
    scr_puts(13, 16, "   CC65 EDITION   ",  COL_LTGREEN);

    scr_puts(8, 18, "W / CRSR UP    - ROTATE",    COL_LTGREY);
    scr_puts(8, 19, "A / CRSR LEFT  - MOVE LEFT", COL_LTGREY);
    scr_puts(8, 20, "D / CRSR RIGHT - MOVE RIGHT",COL_LTGREY);
    scr_puts(8, 21, "S / CRSR DOWN  - SOFT DROP", COL_LTGREY);
    scr_puts(8, 22, "SPACE          - HARD DROP", COL_LTGREY);
    scr_puts(8, 23, "P              - PAUSE",     COL_LTGREY);

    scr_puts(9, 24, "PRESS SPACE OR FIRE TO START", COL_WHITE);

    /* Decorative pieces */
    scr_put(3, 4,  CH_BLOCK, COL_CYAN);   /* I piece */
    scr_put(4, 4,  CH_BLOCK, COL_CYAN);
    scr_put(5, 4,  CH_BLOCK, COL_CYAN);
    scr_put(6, 4,  CH_BLOCK, COL_CYAN);

    scr_put(3, 18, CH_BLOCK, COL_YELLOW); /* O piece */
    scr_put(4, 18, CH_BLOCK, COL_YELLOW);
    scr_put(3, 19, CH_BLOCK, COL_YELLOW);
    scr_put(4, 19, CH_BLOCK, COL_YELLOW);

    scr_put(35, 18, CH_BLOCK, COL_PURPLE); /* T piece */
    scr_put(36, 18, CH_BLOCK, COL_PURPLE);
    scr_put(37, 18, CH_BLOCK, COL_PURPLE);
    scr_put(36, 19, CH_BLOCK, COL_PURPLE);

    music_start();

    /* Drain any keys left in the buffer from loading/running the program
     * (e.g. the SPACE / RETURN used at the BASIC prompt).
     * Wait a few jiffies first so the buffer has time to settle.        */
    wait_jiffies(6);
    while (kbhit()) { cgetc(); }

    for (;;) {
        music_tick();
        sfx_tick();
        if (read_input() & (KEY_SPACE | KEY_UP)) break;
    }

    music_stop();
}

/* =========================================================
 * GAME-OVER SCREEN
 * ========================================================= */
static void gameover_screen(void)
{
    sfx_play(SFX_GAMEOVER);
    scr_puts(BOARD_COL - 1, BOARD_ROW + 9, "  GAME  OVER  ", COL_RED);
    wait_jiffies(120);
    draw_stats();
    scr_puts(BOARD_COL - 1, BOARD_ROW + 11, " PRESS SPACE  ", COL_WHITE);

    for (;;) {
        sfx_tick();
        if (read_input() & KEY_SPACE) break;
    }
}

/* =========================================================
 * GAME INITIALISATION
 * ========================================================= */
static void game_init(void)
{
    rng_state = JIFFY ^ 0x5Au;
    if (rng_state == 0u) rng_state = 0xABu;

    memset(board, 0, sizeof(board));

    score         = 0;
    lines         = 0;
    level         = 0;
    gravity_delay = 48;
    gravity_timer = 0;
    das_timer     = 0;
    game_over     = 0;
    paused        = 0;

    scr_clear();
    VIC.bordercolor = COL_DKGREY;
    VIC.bgcolor0    = COL_BLACK;

    draw_border();
    draw_ui_labels();
    draw_board();
    draw_stats();

    next_type = rng_piece();
    piece_spawn(rng_piece());
    draw_next();
    sprites_update();

    music_start();
}

/* =========================================================
 * MAIN GAME LOOP
 * ========================================================= */
static void game_loop(void)
{
    unsigned char keys, prev_keys, new_rot, cleared;

    prev_keys = 0;

    while (!game_over) {
        wait_jiffy();
        music_tick();
        sfx_tick();

        keys = read_input();

        /* ---- Pause ---- */
        if ((keys & KEY_P) && !(prev_keys & KEY_P)) {
            paused ^= 1u;
            if (paused) {
                scr_puts(BOARD_COL, BOARD_ROW + 10, " PAUSED ", COL_WHITE);
            } else {
                draw_board();
                sprites_update();
            }
        }
        if (paused) { prev_keys = keys; continue; }

        /* ---- Move left ---- */
        if ((keys & KEY_LEFT) && !(prev_keys & KEY_LEFT)) {
            if (!piece_collides((signed char)(piece_x - 1), piece_y, piece_rot)) {
                piece_x--;
                sprites_update();
                sfx_play(SFX_MOVE);
            }
            das_timer = 16;
        } else if ((keys & KEY_LEFT) && prev_keys & KEY_LEFT) {
            if (das_timer > 0u) {
                das_timer--;
            } else {
                if (!piece_collides((signed char)(piece_x - 1), piece_y, piece_rot)) {
                    piece_x--;
                    sprites_update();
                }
                das_timer = 6;
            }
        }

        /* ---- Move right ---- */
        if ((keys & KEY_RIGHT) && !(prev_keys & KEY_RIGHT)) {
            if (!piece_collides((signed char)(piece_x + 1), piece_y, piece_rot)) {
                piece_x++;
                sprites_update();
                sfx_play(SFX_MOVE);
            }
            das_timer = 16;
        } else if ((keys & KEY_RIGHT) && prev_keys & KEY_RIGHT) {
            if (das_timer > 0u) {
                das_timer--;
            } else {
                if (!piece_collides((signed char)(piece_x + 1), piece_y, piece_rot)) {
                    piece_x++;
                    sprites_update();
                }
                das_timer = 6;
            }
        }

        /* Reset DAS when neither direction held */
        if (!(keys & (KEY_LEFT | KEY_RIGHT))) {
            das_timer = 0;
        }

        /* ---- Rotate ---- */
        if ((keys & KEY_UP) && !(prev_keys & KEY_UP)) {
            new_rot = (unsigned char)((piece_rot + 1u) % 4u);
            if (!piece_collides(piece_x, piece_y, new_rot)) {
                piece_rot = new_rot;
                sprites_update();
                sfx_play(SFX_ROTATE);
            } else if (!piece_collides((signed char)(piece_x - 1), piece_y, new_rot)) {
                piece_x--;
                piece_rot = new_rot;
                sprites_update();
                sfx_play(SFX_ROTATE);
            } else if (!piece_collides((signed char)(piece_x + 1), piece_y, new_rot)) {
                piece_x++;
                piece_rot = new_rot;
                sprites_update();
                sfx_play(SFX_ROTATE);
            }
        }

        /* ---- Hard drop ---- */
        if ((keys & KEY_SPACE) && !(prev_keys & KEY_SPACE)) {
            hard_drop();
            sprites_update();
            gravity_timer = gravity_delay; /* force immediate lock */
        }

        /* ---- Soft drop: accelerate gravity ---- */
        if (keys & KEY_DOWN) {
            if (gravity_timer < (unsigned char)(gravity_delay - 2u)) {
                gravity_timer = (unsigned char)(gravity_delay - 2u);
            }
        }

        prev_keys = keys;

        /* ---- Gravity ---- */
        gravity_timer++;
        if (gravity_timer >= gravity_delay) {
            gravity_timer = 0;

            if (!piece_collides(piece_x, (signed char)(piece_y + 1), piece_rot)) {
                piece_y++;
                sprites_update();
            } else {
                /* Lock piece onto board */
                sprites_hide();
                piece_lock();

                /* Check game-over: any filled cell in row 0 */
                {
                    unsigned char c;
                    game_over = 0;
                    for (c = 0; c < BOARD_W; c++) {
                        if (board[0][c] != 0u) { game_over = 1; break; }
                    }
                }

                if (!game_over) {
                    cleared = clear_lines();
                    add_score(cleared);
                    draw_board();
                    draw_stats();

                    piece_spawn(next_type);
                    next_type = rng_piece();
                    draw_next();

                    if (piece_collides(piece_x, piece_y, piece_rot)) {
                        game_over = 1;
                    } else {
                        sprites_update();
                    }
                }
            }
        }
    }
}

/* =========================================================
 * ENTRY POINT
 * ========================================================= */
int main(void)
{
    joy_install(joy_static_stddrv);
    sprites_init();
    sid_silence();
    SID.amp = 0x0Fu;

    for (;;) {
        title_screen();
        game_init();
        game_loop();
        music_stop();
        gameover_screen();
    }
}
