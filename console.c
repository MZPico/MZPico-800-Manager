#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <input.h>
#include <string.h>
#include <ctype.h>

#include "console.h"


uint8_t vram_codes[256] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6b, 0x6a, 0x2f, 0x2a, 0x2e, 0x2d,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x4f, 0x2c, 0x51, 0x2b, 0x57, 0x49,
  0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x52, 0x59, 0x54, 0xbe, 0x7c,
  0xa4, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xac, 0x79, 0x40, 0xa5, 0x0f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0x6a, 0xfb, 0x6c, 0x4d, 0x4e, 0xff,
};

// Keyboard state (see inkey): 10 strobe columns, index = strobe - 0xF0
// (8 = SHIFT/CTRL/BREAK column, 9 = function keys), bits active low
static uint8_t scan_cur[10];     // this scan
static uint8_t scan_prev[10];    // previous scan (stability window)
static uint8_t scan_last[10];    // state at the last evaluation (new-key detection)
static uint8_t scan_stable;      // identical scans in a row
static uint8_t rep_counter;      // windows until the next (auto)repeat
static uint8_t held_key;         // key reported at the last new press (0 = none / F-key)
static uint8_t key_any;          // last evaluation saw a key down
uint8_t key_shift;               // SHIFT is down (from the last scan)

/* multiplication by 40
 * to be called from assembly code
 * inputs: 
 *    e - a number to multiply
 * outputs:
 *   hl - result
 * destroys:
 *   de
 *   hl
 *   f
 */
void _multiply40(void) __naked {
  __asm
    ld d, 0
    ld h, e
    sla e
    sla e
    sla e
    rl d
    sla e
    rl d
    sla e
    rl d

    ld l, h
    ld h, 0
    sla l
    sla l
    sla l

    add hl, de
    ret
  __endasm;
}

void put_str_xy(uint8_t x, uint8_t y, const char *s) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+2)    ; line number
        call __multiply40
        ld e, (iy+4)
        ld d, 0xd0
        add hl, de
        ld de, hl

        ld l, (iy+0)
        ld h, (iy+1)
insert_loop_psx:
        ld a, (hl)
        or a
        jr z, done_psx
        push hl
        ld c, a
        ld hl, _vram_codes
        ld b, 0
        add hl, bc
        ld a, (hl)
        ld (de), a
        inc de
        pop hl
        inc hl
        jr insert_loop_psx
done_psx:
        pop iy
        ret
    __endasm;
}

void put_str_attr_xy(uint8_t x, uint8_t y, const char *s, uint8_t attr) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; line number
        call __multiply40
        ld e, (iy+6)
        ld d, 0xd0
        add hl, de
        ld de, hl

        ld l, (iy+2)
        ld h, (iy+3)
insert_loop_psa:
        ld a, (hl)
        or a
        jr z, done_psa

        push hl
        ld c, a
        ld hl, _vram_codes
        ld b, 0
        add hl, bc
        ld a, (hl)
        ld (de), a

        push de
        ld a, 8
        add a, d
        ld d, a
        ld a, (iy+0)
        ld (de), a
        pop de

        inc de
        pop hl
        inc hl
        jr insert_loop_psa
done_psa:
        pop iy
        ret
    __endasm;
}

void put_char_attr_xy(uint8_t x, uint8_t y, char c, uint8_t attr) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld d, 0xD0
        ld e, (iy+6)    ; x
        add hl, de

        ld c, (iy+2)    ; char
        ld b, 0
        ld de, hl
        ld hl, _vram_codes
        add hl, bc
        ld a, (hl)
        ld hl, de

        ld (hl), a
        add hl, 0x0800
        ld a, (iy+0)    ; attr
        ld (hl), a

        pop iy
        ret
    __endasm;
}

void put_multi_char_xy(uint8_t x, uint8_t y, uint8_t c, uint8_t cnt) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld e, (iy+6)    ; x
        ld d, 0xD0      ; d8000 - start of attribute vram
        add hl, de

        ld de, hl
        inc de          ; setup for ldir

        ld c, (iy+2)
        ld b, 0
        push hl
        ld hl, _vram_codes
        add hl, bc
        ld a, (hl)
        pop hl
        ld (hl), a      ; 1st attr
        ld c, (iy+0)    ; cnt
        dec c
        ld b, 0
        ldir            ; copy from the 1st attr on
        pop iy
        ret
    __endasm;
}

void put_multi_attr_xy(uint8_t x, uint8_t y, uint8_t attr, uint8_t cnt) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld e, (iy+6)    ; x
        ld d, 0xD8      ; d8000 - start of attribute vram
        add hl, de

        ld c, (iy+0)    ; cnt
        ld a, c
        or a
        jr z, pmay_finish

        ld de, hl
        inc de          ; setup for ldir

        ld b, (iy+2)
        ld (hl), b      ; 1st attr
        dec c
        jr z, pmay_finish
        ld b, 0
        ldir            ; copy from the 1st attr on
pmay_finish:
        pop iy
        ret
    __endasm;
}

void clrscr(void) __naked {
    __asm
        ld hl, 0xd000
        ld de, hl
        inc de
        xor a
        ld (hl), a
        ld bc, 0x0800-1
        ldir
        ld a, 0x71
        ld (hl), a
        ld bc, 0x800-1
        ldir
        ret
    __endasm;
}

void scroll_down(uint8_t first, uint8_t number) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ;ld hl, 0xd000 + 22*40 -1
    ;ld de, 0xd000 + 23*40 -1
    ;ld bc, 20*40

    ; Load number into a, store in b
    ld a, (iy+0)
    ld b, a        ; b = number

    ; Load first into e
    ld e, (iy+2)

    ; Compute a = first + number - 1
    add a, e       ; a = first + number
    dec a          ; a = first + number - 1
    ld e, a
    call __multiply40
    ; hl = (first + number - 1) * 40
    ld de, 0xd000
    add hl, de     ; hl = source = 0xd000 + offset
    dec hl         ; hl = hl - 1

    ; de = destination = hl + 40
    push hl
    pop de
    ld bc, 40
    add hl, bc     ; hl += 40 → hl = dest + 1
    ex de, hl      ; de = dest = src + 40, hl = src

    ; Compute bc = number * 40
    push hl
    push de
    ld e, (iy+0)
    call __multiply40
    ; hl = number * 40
    ld b, h
    ld c, l
    pop de
    pop hl

scroll_down_loop:
    ld a, (hl)
    ld (de), a
    dec hl
    dec de
    dec bc
    ld a,b
    or c
    jr nz, scroll_down_loop

    pop iy
    ret
  __endasm;
}

void scroll_up(uint8_t first, uint8_t number) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ;ld hl, 0xd000 + 3*40
    ;ld de, 0xd000 + 2*40
    ;ld bc, 20*40

    ; Load number into a, store in b
    ld b, (iy+0)

    ; Load first into e
    ld e, (iy+2)

    dec e
    ; Compute de = 0xD000 + first * 40
    call __multiply40
    ld de, 0xd000
    add hl, de     ; hl = dest
    push hl
    pop de

    ; hl = de + 40 = src
    ld bc, 40
    add hl, bc     ; hl = source

    ; Compute bc = number * 40
    push hl
    push de
    ld e, (iy+0)
    call __multiply40
    ld b, h
    ld c, l
    pop de
    pop hl

scroll_up_loop:
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    dec bc
    ld a, b
    or c
    jr nz, scroll_up_loop

    pop iy
    ret
  __endasm;
}

void border(uint8_t color) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ld bc, 0x06cf
    ld a, (iy+0)
    out (c), a

    pop iy
    ret
  __endasm;
}

void beep(void) __naked {
  __asm
    jp 0x0577
  __endasm;
}

// Scan the whole keyboard matrix (8255 memory-mapped in MZ-700 mode: strobe
// to 0xE000, rows from 0xE001; ~5 us settle, two reads ORed so a bit counts
// as pressed only when both agree) into scan_cur, and while at it compute
// the flags inkey() needs - in assembler, because the same loop in C cost
// ~4 ms per scan on sccz80 and made BASIC's 16/64/6 windows 5 s long.
// Column 8 (SHIFT/CTRL/BREAK) is masked to BREAK. Returns:
//   bit0 any key down, bit1 changed vs previous scan, bit2 a key newly
//   pressed vs scan_last, bit3 that new key is in the F-key column,
//   bit4 that new key is BREAK.
static uint8_t scan_matrix(void) __naked {
  __asm
    push ix
    ld hl, _scan_cur
    ld de, _scan_prev
    ld ix, _scan_last
    ld c, 0
    ld a, 0xf0
    ld b, 10
_sm_loop:
    ld (0xe000), a
    push af
    push bc
    ld b, 4
_sm_settle:
    djnz _sm_settle
    ld a, (0xe001)
    ld b, a
    ld a, (0xe001)
    or b
    pop bc
    ld (hl), a
    ld a, b
    cp 2                 ; column 8 (strobe 0xf8): note SHIFT, keep BREAK only
    jr nz, _sm_nomask
    ld a, (hl)
    and 1
    xor 1
    ld (_key_shift), a
    ld a, (hl)
    or 0x7f
    ld (hl), a
_sm_nomask:
    ld a, (de)
    cp (hl)
    jr z, _sm_same
    set 1, c             ; changed
_sm_same:
    ld a, (hl)
    ld (de), a
    cp 0xff
    jr z, _sm_none
    set 0, c             ; any key down
_sm_none:
    cpl
    and (ix+0)           ; pressed now and not at the last evaluation
    jr z, _sm_nonew
    set 2, c             ; new key
    ld a, b
    cp 1
    jr nz, _sm_notfk
    set 3, c             ; in the F-key column
_sm_notfk:
    cp 2
    jr nz, _sm_nonew
    set 4, c             ; BREAK (column 8 is masked to that bit)
_sm_nonew:
    inc hl
    inc de
    inc ix
    pop af
    inc a
    djnz _sm_loop
    ld l, c
    ld h, 0
    pop ix
    ret
  __endasm;
}

// Map the F-key row (strobe 9, active low) to 1..5 (highest bit wins)
static uint8_t map_fmask(uint8_t m) {
  if      (m & 0x80) return 1;
  else if (m & 0x40) return 2;
  else if (m & 0x20) return 3;
  else if (m & 0x10) return 4;
  else if (m & 0x08) return 5;
  else               return 0;
}

#define KEY_WINDOW       8   // identical scans before a state is evaluated (~8 ms)
#define KEY_REPEAT_DELAY 64  // windows before the first repeat (~0.55 s)
#define KEY_REPEAT_RATE   6  // windows between repeats (~50 ms)

// Timing: one scan_matrix() is ~330 T per column = ~0.95 ms at 3.55 MHz
// (settle, double read, compare), heavier than BASIC's ~0.4 ms scan, so the
// window is 8 scans instead of BASIC's 16 to land on BASIC's feel: ~8 ms
// debounce, first repeat after ~0.55 s, then ~20 per second.
// Non-blocking, one scan per call. Replicates Sharp BASIC's GETL key
// handling (MZ-2Z046 A0B21..A0C68): the matrix is scanned every pass and
// any change restarts a window of KEY_WINDOW identical scans (debounce);
// once stable, a bit pressed now that was not pressed at the last
// evaluation is a new key and is reported at once (the ROM decodes it to
// ASCII, called once); a key still held is reported again after
// KEY_REPEAT_DELAY windows and then every KEY_REPEAT_RATE windows; no key
// resets everything. SHIFT and CTRL alone are not keys (their column is
// masked except BREAK), and function keys never repeat - as in BASIC.
uint8_t inkey(void) {
  uint8_t f = scan_matrix();

  if (f & 2) { scan_stable = 0; return 0; }
  if (scan_stable < KEY_WINDOW) { scan_stable++; return 0; }
  scan_stable = 0;                       // one evaluation per window
  key_any = f & 1;

  if (!(f & 1)) {
    memcpy(scan_last, scan_cur, 10);
    held_key = 0;
    rep_counter = KEY_REPEAT_DELAY;
    return 0;
  }
  if (f & 4) {
    memcpy(scan_last, scan_cur, 10);
    rep_counter = KEY_REPEAT_DELAY;
    if (f & 8) { held_key = 0; return map_fmask((uint8_t)~scan_cur[9]) + (key_shift ? 0x80 : 0); }   // SHIFT+F1..F5 = 0x81..0x85 (never ASCII: CR is 0x0A on some z88dk builds)
    if (f & 16) { held_key = 0; return 0x1b; }   // BREAK = ESC: the ROM decoder returns 0 for it, BASIC synthesizes 0x1B too
    held_key = getk();                   // ROM decode of the key just pressed
    return held_key;
  }
  if (held_key && --rep_counter == 0) {
    rep_counter = KEY_REPEAT_RATE;
    return held_key;
  }
  return 0;
}

// Wait until every key is released, then for a new press; returns it.
// Resets the scanner so a held F-key cannot dismiss a screen it opened.
// Wait until every key is released (and forget any autorepeat in flight)
void key_release(void) {
  console_init();
  do { inkey(); } while (key_any || scan_stable);
}

// Line editor at (x,y), width w: edits buf (max chars incl. NUL) with a
// cursor: left/right move, HOME to the start, CLR empties, DEL deletes the
// character left of the cursor, INST inserts a space, printable keys insert.
// CR accepts (returns 1), ESC cancels (0). Sharp ASCII control codes.
uint8_t input_line(uint8_t x, uint8_t y, uint8_t w, char *buf, uint8_t max) {
  uint8_t k, i, n = strlen(buf), c = n;
  key_release();
  for (;;) {
    put_multi_char_xy(x, y, ' ', w);
    put_multi_attr_xy(x, y, 0x70, w);
    put_str_xy(x, y, buf);
    if (c < w) put_char_attr_xy(x + c, y, c < n ? buf[c] : ' ', 0x16);
    while (!(k = inkey()));
    if (k == 0x0d || k == 0x0a) return 1;
    if (k == 0x1b) return 0;
    if (k == 0x14) { if (c) c--; continue; }                       // left
    if (k == 0x13) { if (c < n) c++; continue; }                   // right
    if (k == 0x15) { c = 0; continue; }                            // HOME
    if (k == 0x16) { n = c = 0; buf[0] = 0; continue; }            // CLR
    // DEL and INST reach getk() as the raw codes 0x60 / 0x61 (measured on
    // the MZ-800: z88dk's display-code conversion leaves them alone)
    if (k == 0x60 || k == 0x10 || k == 0x7f || k == 0x08) {        // DEL: left of cursor
      if (c) { for (i = c - 1; i < n; i++) buf[i] = buf[i + 1]; n--; c--; }
      continue;
    }
    if (k == 0x61 || k == 0x18) k = ' ';                           // INST: insert a space
    if (k >= 0x20 && k < 0x7f) {
      if (n < max - 1 && n < w) { for (i = n; i > c; i--) buf[i] = buf[i - 1]; buf[c++] = (char)k; buf[++n] = 0; }
      continue;
    }
  }
}

uint8_t wait_key(void) {
  uint8_t k;
  key_release();
  while (!(k = inkey()));
  return k;
}

void loading_screen(const char* name) {
  put_str_xy(16, 9, "LOADING");
  int ln = strlen(name);
  put_str_xy(20 - ln / 2, 11, name);
}

void get_uppercase_extension(const char* filename, char* extension) {
  const char* dot = strrchr(filename, '.');
  if (!dot || dot == filename) {
    extension[0] = '\0';
    return;
  }

  dot++;
  while (*dot) {
    *extension++ = toupper((unsigned char)*dot);
    dot++;
  }
  *extension = '\0';
}

void console_init(void) {
  memset(scan_prev, 0xff, 10);
  memset(scan_last, 0xff, 10);
  scan_stable = 0;
  rep_counter = KEY_REPEAT_DELAY;
  held_key = 0;
  key_any = 1;
}
