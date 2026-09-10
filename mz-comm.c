// Z80-side client of the MZPico `unicard` repository device (Unicard
// "MZFREPO" protocol on ports 0x50/0x51 plus MZPico extensions).
#include <stdio.h>
#include <string.h>
#include "mz-comm.h"
#include "console.h"

#define MZF_HEADER_START 0x10f0
#define MZF_BODY_TARGET 0x1200
#define MZF_SIZE MZF_HEADER_START + 0x12
#define LOADER_TARGET 0x1000

char error_description[ERROR_DESCRIPTION_LN];

// ---------------- transport ----------------

void uc_cmd(uint8_t command) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    out (UC_CMD_PORT), a
    pop iy
    ret
  __endasm;
}

void uc_wr(uint8_t data) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    out (UC_DATA_PORT), a
    pop iy
    ret
  __endasm;
}

uint8_t uc_rd(void) {
  __asm
    in a, (UC_DATA_PORT)
    ld h, 0
    ld l, a
  __endasm;
}

// STSR rewinds the status pointer without touching anything else; the
// pointer parks after the 4th byte, so every status read starts with it.
void uc_status4(uint8_t *status) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld l, (iy+0)
    ld h, (iy+1)
    ld a, cmdSTSR
    out (UC_CMD_PORT), a
    ld c, UC_CMD_PORT
    ld b, 4
    inir
    pop iy
    ret
  __endasm;
}

// uc_read(dst, n): INIR in 256-byte blocks. Data-port reads never return
// before the byte is ready (the device holds EXWAIT), so no polling.
void uc_read(uint8_t *dst, uint16_t n) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld e, (iy+0)          ; n low
    ld d, (iy+1)          ; n high
    ld l, (iy+2)          ; dst
    ld h, (iy+3)
    ld c, UC_DATA_PORT
_ucr_blocks:
    ld b, 0
    ld a, d
    or a
    jr z, _ucr_rest
    inir
    dec d
    jr _ucr_blocks
_ucr_rest:
    ld b, e
    or b
    jr z, _ucr_done
    inir
_ucr_done:
    pop iy
    ret
  __endasm;
}

// uc_write(src, n): OTIR in 256-byte blocks (putc into the open file)
void uc_write(const uint8_t *src, uint16_t n) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld e, (iy+0)          ; n low
    ld d, (iy+1)          ; n high
    ld l, (iy+2)          ; src
    ld h, (iy+3)
    ld c, UC_DATA_PORT
_ucw_blocks:
    ld b, 0
    ld a, d
    or a
    jr z, _ucw_rest
    otir
    dec d
    jr _ucw_blocks
_ucw_rest:
    ld b, e
    or b
    jr z, _ucw_done
    otir
_ucw_done:
    pop iy
    ret
  __endasm;
}

void uc_wstr(const char *s) {
  while (*s) uc_wr((uint8_t)*s++);
  uc_wr(0x0d);
}

// ---------------- errors ----------------

static const char *fatfs_text(uint8_t code) {
  switch (code) {
    case 1: return "Disk error";
    case 3: return "Not ready";
    case 4: return "No such file";
    case 5: return "No such path";
    case 6: return "Invalid name";
    case 7: return "Access denied";
    case 8: return "Already exists";
    case 10: return "Write protected";
    case 11: return "Invalid drive";
    case 12: return "Drive not enabled";
    case 13: return "No filesystem";
    case 16: return "File locked";
    case 17: return "Out of memory";
    default: return "FS error";
  }
}

static const char *cloud_text(uint8_t code) {
  switch (code) {
    case 1: return "Cloud WiFi not connected";
    case 2: return "Cloud request timeout";
    case 3: return "Cloud request failed";
    case 4: return "Cloud file too large";
    case 5: return "Cloud busy";
    case 6: return "Out of memory";
    case 7: return "Invalid cloud file";
    default: return "Cloud error";
  }
}

// Waits out an asynchronous (cloud) command - with a spinner in the top
// right corner (beside the WiFi icon) and ESC to stop waiting (the device finishes the
// transfer on its own and answers "busy" until then) - then returns
// nonzero and fills error_description when the status reports ERROR
static uint8_t check_error(uint8_t *st) {
  static const char spin[] = "|/-\\";
  uint8_t n = 0;
  while (st[0] & UC_ST_INPROG) {
    if (inkey() == 0x1b) {
      put_char_attr_xy(39, 0, ' ', 0x71);
      strcpy(error_description, "Cancelled (device busy)");
      return 0xfe;
    }
    if ((n & 7) == 0) put_char_attr_xy(39, 0, spin[(n >> 3) & 3], 0x61);   // top right, beside the WiFi icon; keep the frame's blue background
    n++;
    uc_status4(st);
  }
  if (n) put_char_attr_xy(39, 0, ' ', 0x71);
  if (!(st[0] & UC_ST_ERROR)) return 0;
  switch (st[2]) {
    case 1: strcpy(error_description, "Not implemented"); break;
    case 2: strcpy(error_description, "Bad parameter"); break;
    case 3: strcpy(error_description, "Parameter overflow"); break;
    case 4: strcpy(error_description, "No file open"); break;
    case 5: strcpy(error_description, "Device busy"); break;
    case 6: strncpy(error_description, fatfs_text(st[3]), ERROR_DESCRIPTION_LN - 1); break;
    case 7: strncpy(error_description, cloud_text(st[3]), ERROR_DESCRIPTION_LN - 1); break;
    default: sprintf(error_description, "Error %d/%d", st[2], st[3]); break;
  }
  error_description[ERROR_DESCRIPTION_LN - 1] = 0;
  return st[2] ? st[2] : 0xff;
}

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---------------- API ----------------

// Directory listing: sorted (directories first) and filtered to launchable
// files by the device (SETSORT); one 55-byte FILINFO record per entry.
static int entry_less(const DIR_ENTRY *a, const DIR_ENTRY *b) {
  if (a->filename[0] == '.' && a->filename[1] == '.') return 1;
  if (b->filename[0] == '.' && b->filename[1] == '.') return 0;
  if (a->isDir != b->isDir) return a->isDir ? 1 : 0;
  {
    const char *pa = a->filename, *pb = b->filename;
    while (*pa && *pb) {
      char ca = *pa, cb = *pb;
      if (ca >= 'a' && ca <= 'z') ca -= 32;
      if (cb >= 'a' && cb <= 'z') cb -= 32;
      if (ca != cb) return ca < cb;
      pa++; pb++;
    }
    return *pa == 0 && *pb != 0;
  }
}

// Shell sort (Knuth gaps): the records are 37 bytes and the Z80 moves them
// by value, so the O(n^2) insertion sort took seconds on a few hundred
// entries; this stays in place (no index array - the explorer has ~2.8 KB
// between BSS and the stack) at ~n^1.3 moves. entry_less is a total order
// (FAT names are unique case-insensitively), so stability is not needed.
static void dir_sort(DIR_ENTRY *e, uint16_t n) {
  uint16_t gap, i, j;
  DIR_ENTRY key;
  for (gap = 1; gap < n / 3; gap = gap * 3 + 1);
  for (; gap > 0; gap /= 3) {
    for (i = gap; i < n; i++) {
      key = e[i];
      j = i;
      while (j >= gap && entry_less(&key, &e[j - gap])) { e[j] = e[j - gap]; j -= gap; }
      e[j] = key;
    }
  }
}

uint8_t list_launchable_only = 1;

uint8_t list_dir(const char *path, uint16_t *entries_cnt, DIR_ENTRY *entries) {
  uint8_t st[4];
  uint8_t rec[55];
  uint16_t n = 0;

  uc_cmd(cmdX_SETSORT);
  uc_wr(list_launchable_only ? 0x03 : 0x01);   // bit0 sort (device ignores), bit1 launchable-only
  uc_cmd(cmdREADDIR);
  uc_wstr(path);
  uc_status4(st);
  if (check_error(st)) return 1;
  while ((st[0] & UC_ST_STREAM) && n < MAX_DIR_ENTRIES) {
    DIR_ENTRY *e = &entries[n];
    uc_read(rec, 55);
    e->isDir = (rec[8] & 0x10) ? 1 : 0;
    e->size = le32(rec);
    if (rec[22]) {
      uint8_t ln = rec[22];
      if (ln > FILENAME_LN - 1) ln = FILENAME_LN - 1;
      memcpy(e->filename, rec + 23, ln);
      e->filename[ln] = 0;
    } else {
      strncpy(e->filename, (char *)rec + 9, 12);
      e->filename[12] = 0;
    }
    n++;
    uc_status4(st);
  }
  if (st[0] & UC_ST_STREAM) uc_cmd(cmdCLOSE); // more than we can hold
  *entries_cnt = n;
  if (strncmp(path, "cloud:", 6)) dir_sort(entries, n);   // cloud listings keep the server's order
  return 0;
}

// Volumes: "sd:" 0x0D "flash:" 0x0D ... ; names stored without the colon
uint8_t list_dev(uint16_t *entries_cnt, DEV_ENTRY *entries) {
  uint8_t st[4];
  uint16_t n = 0;
  uint8_t i = 0;

  uc_cmd(cmdX_LISTVOL);
  uc_status4(st);
  if (check_error(st)) return 1;
  while ((st[0] & UC_ST_OUTPUT) && n < 3) {
    uint8_t c = uc_rd();
    if (c == 0x0d) {
      entries[n].name[i] = 0;
      n++; i = 0;
    } else if (c == ':') {
      /* dropped */
    } else if (i < DEVICE_LN - 1) {
      entries[n].name[i++] = (char)c;
    }
    uc_status4(st);
  }
  *entries_cnt = n;
  return 0;
}

// Config records are 80 bytes: key[16] value[64], the ConfigEntry layout
uint8_t get_config(const char *section, uint16_t *entries_cnt, ConfigEntry *entries) {
  uint8_t st[4];
  uint16_t n = 0;

  uc_cmd(cmdX_GETCONFIG);
  uc_wstr(section);
  uc_status4(st);
  if (check_error(st)) return 1;
  while ((st[0] & UC_ST_STREAM) && n < 23) {
    uc_read((uint8_t *)&entries[n], CONFIG_ENTRY_SIZE);
    n++;
    uc_status4(st);
  }
  *entries_cnt = n;
  return 0;
}

uint8_t get_wifi_status(void) {
  uint8_t st[4];
  uint8_t status;

  uc_cmd(cmdX_WIFISTATUS);
  uc_status4(st);
  if (check_error(st) || !(st[0] & UC_ST_OUTPUT)) return 0xFF;
  status = uc_rd();
  return status;
}

// First n bytes of a file (MZF/DSK headers for the info panel)
uint8_t read_file_head(const char *path, uint8_t *buf, uint8_t n) {
  uint8_t st[4];
  uc_cmd(cmdOPEN);
  uc_wr(UC_FA_READ);
  uc_wstr(path);
  uc_status4(st);
  if (check_error(st)) return 1;
  uc_read(buf, n);
  uc_cmd(cmdCLOSE);
  return 0;
}

uint8_t read_file(const char *path, uint8_t *buf, uint16_t n) {
  uint8_t st[4];
  uc_cmd(cmdOPEN);
  uc_wr(UC_FA_READ);
  uc_wstr(path);
  uc_status4(st);
  if (check_error(st)) return 1;
  uc_read(buf, n);
  uc_cmd(cmdCLOSE);
  return 0;
}

uint8_t write_file(const char *path, const uint8_t *buf, uint16_t n) {
  uint8_t st[4];
  uc_cmd(cmdOPEN);
  uc_wr(UC_FA_CREATE_WRITE);
  uc_wstr(path);
  uc_status4(st);
  if (check_error(st)) return 1;
  uc_write(buf, n);
  uc_status4(st);
  uc_cmd(cmdCLOSE);
  return check_error(st);
}

// FDDMOUNT into device dev (0-3 = floppy drives 1-4, 5 = Quick Disk); a
// DSK/MZQ image or a directory, session-only like the ini mounts
uint8_t mount_into(uint8_t dev, const char *path) {
  uint8_t st[4];
  uint8_t err;
  uc_cmd(cmdFDDMOUNT);
  uc_wr(dev);
  uc_wstr(path);
  uc_status4(st);
  err = check_error(st);
  if (err == 1)   // NOT_IMPLEMENTED: the device is not in mzpico.ini
    strcpy(error_description, dev == UC_DEV_QD ? "No [qd] in mzpico.ini" : "No [fdc] in mzpico.ini");
  return err ? 1 : 0;
}

// Current mounts: lines "1:path" .. "4:path", "Q:path" (empty path = empty
// drive), 0-terminated back to back in buf; returns the number of lines
uint8_t get_mounts(char *buf, uint8_t max) {
  uint8_t st[4];
  uint8_t i = 0, lines = 0;
  uc_cmd(cmdX_MOUNTS);
  uc_status4(st);
  if (check_error(st)) return 0;
  while (st[0] & UC_ST_OUTPUT) {
    uint8_t c = uc_rd();
    if (c == 0x0d) { if (i < max) buf[i++] = 0; lines++; }
    else if (i < max - 1) buf[i++] = (char)c;
    uc_status4(st);
  }
  buf[max - 1] = 0;
  return lines;
}

static uint8_t simple_cmd_done(void) {
  uint8_t st[4];
  uc_status4(st);
  return check_error(st) ? 1 : 0;
}

uint8_t fs_unlink(const char *path) { uc_cmd(cmdUNLINK); uc_wstr(path); return simple_cmd_done(); }
uint8_t fs_mkdir(const char *path)  { uc_cmd(cmdMKDIR);  uc_wstr(path); return simple_cmd_done(); }
uint8_t set_config(const char *section, const char *key, const char *value) {
  uc_cmd(cmdX_SETCONFIG); uc_wstr(section); uc_wstr(key); uc_wstr(value); return simple_cmd_done();
}
uint8_t copy_file(const char *src, const char *dst) {
  uc_cmd(cmdX_COPY); uc_wstr(src); uc_wstr(dst); return simple_cmd_done();
}
uint8_t fs_rename(const char *old_path, const char *new_path) {
  uc_cmd(cmdRENAME); uc_wstr(old_path); uc_wstr(new_path); return simple_cmd_done();
}

static void get_uppercase_ext(const char *path, char *ext) {
  const char *dot = strrchr(path, '.');
  uint8_t i = 0;
  ext[0] = 0;
  if (!dot) return;
  dot++;
  while (*dot && i < 4) {
    char c = *dot++;
    if (c >= 'a' && c <= 'z') c -= 32;
    ext[i++] = c;
  }
  ext[i] = 0;
}

// DSK -> floppy drive 1, MZQ -> Quick Disk, anything else (MZF, M12, @menu
// ...) is opened for reading and stays open for read_and_execute().
uint8_t mount_entry(const char *path) {
  uint8_t st[4];
  char ext[5];

  get_uppercase_ext(path, ext);
  if (!strcmp(ext, "DSK")) return mount_into(UC_DEV_FD1, path);
  if (!strcmp(ext, "MZQ")) return mount_into(UC_DEV_QD, path);
  uc_cmd(cmdOPEN);
  uc_wr(UC_FA_READ);
  uc_wstr(path);
  uc_status4(st);
  if (check_error(st)) return 1;
  return 0;
}

// The file is open on the data port (mount_entry): stream the 128-byte MZF
// header to 0x10F0 and the body to 0x1200, then let the ROM relocate and
// run it. The loader copies itself below the body first because the body
// overwrites this program.
void read_and_execute(void) __naked {
  __asm
    ld sp, MZF_HEADER_START

    ld hl, _read_and_execute_start
    ld de, LOADER_TARGET
    ld bc, _read_and_execute_end - _read_and_execute_start
    ldir
    jp LOADER_TARGET

_read_and_execute_start:
    ld b, 128
    ld c, UC_DATA_PORT
    ld hl, MZF_HEADER_START
    inir

    ; Validate the header before the body overwrites this program: a body
    ; of 0 bytes (nothing was open, the device streams zeros) or one that
    ; would not fit below the stack at 0xD000 (0x1200 + size <= 0xD000)
    ; means the file is not a program we can run. Back to the menu then.
    ld de, (MZF_SIZE)
    ld a, d
    or e
    jr z, _re_bad_header
    ld a, d
    cp 0xBE
    jr c, _re_header_ok
    jr nz, _re_bad_header
    ld a, e
    or a
    jr nz, _re_bad_header
_re_header_ok:
    ld hl, MZF_BODY_TARGET
    ld de, (MZF_SIZE)

_re_read_blocks:
    ld b,0
    ld a,d
    or a
    jr z, _re_read_remaining
    inir
    dec d
    jr _re_read_blocks

_re_read_remaining:
    ld b, e
    or b
    jr z, _re_done
    inir

_re_done:
    ld a, cmdCLOSE
    out (UC_CMD_PORT), a
    exx
    ld bc,0x0600
    exx
    ld hl, MZF_SIZE
    ld sp, MZF_HEADER_START
    jp 0xecfc     ; relocate and execute
_re_bad_header:
    ; CLOSE whatever is open, OPEN the embedded menu, restart this loader
    ; (relocated copy: no data references, the string is emitted inline)
    ld a, cmdCLOSE
    out (UC_CMD_PORT), a
    ld a, cmdOPEN
    out (UC_CMD_PORT), a
    ld a, UC_FA_READ
    out (UC_DATA_PORT), a
    ld a, 0x40        ; '@'
    out (UC_DATA_PORT), a
    ld a, 0x6d        ; 'm'
    out (UC_DATA_PORT), a
    ld a, 0x65        ; 'e'
    out (UC_DATA_PORT), a
    ld a, 0x6e        ; 'n'
    out (UC_DATA_PORT), a
    ld a, 0x75        ; 'u'
    out (UC_DATA_PORT), a
    ld a, 0x0d
    out (UC_DATA_PORT), a
    jp LOADER_TARGET
_read_and_execute_end:
  __endasm;
}

// ---------------- ROM boot bridge (F / Q / C with error return) ----------------
// The 9Z-504M ROM boot routines report errors by printing a message (DE)
// and dropping into the ROM IPL menu, from which only a reset leads back
// here. Their real work (sector reads, QD sync, the tape monitor calls)
// stays in ROM; only the short control sequences that hold the error exits
// are replicated below, with those exits pointed at our handler:
//   F: CALL 0xE44A - the routine stores its return address at 0xCEFE and
//      every error path returns through it with DE = message.
//   Q: E9B7..EA05 transcribed; the early "JR C,EA34", the wrong-type exit
//      and the pushed return address (EA04, reached through F244 RET C) all
//      become the handler.
//   C: E945..E9B6 transcribed; the two "JP C,E9AA" and the E9AA block end
//      in the handler.
// FD and tape load the program body over 0x1200 (this program), so the
// bridge runs relocated at ROM_BRIDGE (below the FD work table at 0xCEE9)
// and the handler reloads @menu itself through port 0x50. The message
// pointer is left for the menu at ROM_ERR_CELL: "MZ" magic, then DE.
// Entry: ROM_BRIDGE + 3*kind (jump table). Bytes at the three ROM entries
// are checked first; a foreign ROM falls back to the plain jump.
#define ROM_BRIDGE 0xCD00
#define ROM_ERR_CELL 0xCCF8
#define REL(l) ROM_BRIDGE + l - _rb_start   /* no outer parentheses: ld hl,(x) would be an indirect load */

// Per-kind fingerprints of the ROM code the bridge relies on. Checked
// against the ROMs mz800emu ships (9Z-504M; JSS 1.3, 1.5C, 1.6A, 1.8C;
// Willy's): the FD boot and its 0xCEFE error return are identical in all
// of them, the tape routine E945..E9B6 in 9Z-504M and JSS (Willy's
// rewrote it from E97D on), the QD boot at E9B7 only in 9Z-504M. Any
// other ROM gets the plain jump (errors end in the ROM menu, as before).
static uint8_t rom_match(uint16_t addr, const uint8_t *sig, uint8_t n) {
  const uint8_t *p = (const uint8_t *)addr;
  while (n--) if (*p++ != *sig++) return 0;
  return 1;
}

static uint8_t rom_supported(uint8_t kind) {
  static const uint8_t fd_e44a[] = {0xE3, 0x22, 0xFE, 0xCE, 0xCD, 0xD5, 0xE8};
  static const uint8_t fd_e4c2[] = {0x31, 0xEE, 0x10, 0x2A, 0xFE, 0xCE, 0xE3, 0xC9};
  static const uint8_t qd_e9b7[] = {0xCD, 0x13, 0xEB, 0x3E, 0x02, 0x20};
  static const uint8_t qd_e9e0[] = {0xCD, 0xF7, 0xEE, 0xDA, 0x02, 0xF2};
  static const uint8_t qd_e9fa[] = {0xD5, 0x3E, 0x06, 0x32, 0x30, 0x11};
  static const uint8_t qd_f244[] = {0x3E, 0x06, 0x32, 0x30, 0x11, 0xCD};
  switch (kind) {
    case 0: return rom_match(0xE44A, fd_e44a, 7) && rom_match(0xE4C2, fd_e4c2, 8);
    case 1: return rom_match(0xE9B7, qd_e9b7, 6) && rom_match(0xE9E0, qd_e9e0, 6) &&
                   rom_match(0xE9FA, qd_e9fa, 6) && rom_match(0xF244, qd_f244, 6);
    default: return 1;   // tape: monitor API + ECFC only, identical in every known ROM
  }
}

static void rom_boot_bridge(uint8_t kind) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    pop iy
    ld hl, _rb_start
    ld de, ROM_BRIDGE
    ld bc, _rb_end - _rb_start
    ldir
    ld l, a
    ld h, 0
    ld b, h
    ld c, l
    add hl, bc          ; kind*2
    add hl, bc          ; kind*3
    ld de, ROM_BRIDGE
    add hl, de
    jp (hl)             ; jump table entry

_rb_start:
    jp REL(_rb_fd)
    jp REL(_rb_qd)
    jp REL(_rb_tape)

_rb_fd:
    ld sp, 0x10F0
    call 0xEA59
    call 0xE44A
    jp REL(_rb_err)

_rb_qd:
    ld sp, 0x10F0
    call 0xEB13
    ld a, 2
    jp nz, REL(_rb_tape_err)
    call 0xEEEC
    call 0xEF27
    ld de, 0xEDA7
    jp c, REL(_rb_err)
    call 0xEA59
    ld a, 0x0D
    ld (0x11A3), a
    call 0xF25F
    ld a, 1
    ld (0x113A), a
    ld hl, REL(_rb_err)
    ld sp, 0x10EE
    ex (sp), hl
    call 0xEEF7
    jp c, 0xF202
    ld a, (0x10F0)
    cp 1
    ld de, 0xEE27
    jr nz, _rb_qd_bad
    ld de, 0xED88
    rst 0x18
    jp 0xEEC2
_rb_qd_bad:
    push de
    ld a, 6
    ld (0x1130), a
    call 0xE010
    pop de
    jp REL(_rb_err)

_rb_tape:
    ; Tape: the ROM E-half is used only for ECFC (identical in every ROM
    ; variant); screen output goes through the monitor (0x0012 PRNT with
    ; the 0xC6 clear code, 0x000C space, 0x0018 message) and the message
    ; texts live in this bridge - so this path works with any ROM that
    ; keeps the standard monitor tape API (0x001E, 0x0027, 0x002A).
    ld sp, 0x10F0
    ld hl, 0xE002
    ld a, (hl)
    and 0x10
    jr nz, _rb_t_go
    inc hl
    ld a, 6
    ld (hl), a
    inc a
    ld (hl), a
    dec hl
    ld a, (hl)
    and 0x10
    jr nz, _rb_t_go
    ld a, 0xC6
    call 0x0012
    call 0x0006
    call 0x0006
    ld de, REL(_rb_msg_ready)
    call REL(_rb_print12)
_rb_t_wait:
    call 0x001E
    jr z, _rb_t_nodata
    ld a, (hl)
    and 0x10
    jr z, _rb_t_wait
_rb_t_go:
    ld a, 0xC6
    call 0x0012
    call 0x0006
    ld de, REL(_rb_msg_looking)
    rst 0x18
    call 0x0027
    jr c, _rb_tape_err
    ld a, 0xC6
    call 0x0012
    ld de, REL(_rb_msg_loading)
    rst 0x18
    ld de, 0x10F1
    rst 0x18
    ld hl, (0x1104)
    exx
    ld hl, 0x1200
    ld (0x1104), hl
    call 0x002A
    jr c, _rb_tape_err
    ld bc, 0x0100
    exx
    ld (0x1104), hl
    ld hl, 0x1102
    jp 0xECFC
_rb_t_nodata:
    ld de, REL(_rb_msg_ready)
    jr _rb_err
_rb_tape_err:
    cp 2
    ld de, REL(_rb_msg_ready)
    jr z, _rb_err
    ld de, REL(_rb_msg_lderr)
    jr _rb_err

    ; 12 spaces, message (DE), newline - what the ROM does in EA4E
_rb_print12:
    ld b, 12
_rb_sp:
    call 0x000C
    djnz _rb_sp
    rst 0x18
    jp 0x0006

    ; Sharp-ASCII texts as the 9Z-504M ROM has them, 0x0D terminated
_rb_msg_loading:
    defb 0x49,0x50,0x4C,0x20,0xA6,0xA4,0x20,0xB8,0xB7,0xA1,0x9C,0xA6,0xB0,0x97,0x20,0x0D
_rb_msg_ready:
    defb 0x4D,0xA1,0xA9,0x92,0x20,0x9D,0x92,0xA1,0x9C,0xBD,0x20,0x43,0x4D,0x54,0x0D
_rb_msg_looking:
    defb 0x20,0x20,0x20,0x20,0x20,0x49,0x50,0x4C,0x20,0xA6,0xA4,0x20,0xB8,0xB7,0xB7,0xA9,0xA6,0xB0,0x97,0x20,0xAA,0xB7,0x9D,0x20,0xA1,0x20,0x9E,0x9D,0xB7,0x97,0x9D,0xA1,0xB3,0x0D
_rb_msg_lderr:
    defb 0x43,0x4D,0x54,0x3A,0x4C,0xB7,0xA1,0x9C,0xA6,0xB0,0x97,0x20,0x92,0x9D,0x9D,0xB7,0x9D,0x0D

_rb_err:
    ; DE = ROM message. Leave it for the menu, reload @menu through the
    ; device (this program at 0x1200 may be partly overwritten).
    ld sp, 0x10F0
    ld (ROM_ERR_CELL + 2), de
    ld a, 0x4D
    ld (ROM_ERR_CELL), a
    ld a, 0x5A
    ld (ROM_ERR_CELL + 1), a
    ld a, cmdCLOSE
    out (UC_CMD_PORT), a
    ld a, cmdOPEN
    out (UC_CMD_PORT), a
    ld a, UC_FA_READ
    out (UC_DATA_PORT), a
    ld a, 0x40
    out (UC_DATA_PORT), a
    ld a, 0x6D
    out (UC_DATA_PORT), a
    ld a, 0x65
    out (UC_DATA_PORT), a
    ld a, 0x6E
    out (UC_DATA_PORT), a
    ld a, 0x75
    out (UC_DATA_PORT), a
    ld a, 0x0D
    out (UC_DATA_PORT), a
    ld b, 128
    ld c, UC_DATA_PORT
    ld hl, MZF_HEADER_START
    inir
    ld hl, MZF_BODY_TARGET
    ld de, (MZF_SIZE)
_rb_ld_blocks:
    ld b, 0
    ld a, d
    or a
    jr z, _rb_ld_rest
    inir
    dec d
    jr _rb_ld_blocks
_rb_ld_rest:
    ld b, e
    or b
    jr z, _rb_ld_done
    inir
_rb_ld_done:
    ld a, cmdCLOSE
    out (UC_CMD_PORT), a
    exx
    ld bc, 0x0600
    exx
    ld hl, MZF_SIZE
    ld sp, MZF_HEADER_START
    jp 0xECFC
_rb_end:
  __endasm;
}

static void rom_boot_plain(uint8_t kind) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    pop iy
    ld sp, MZF_HEADER_START
    or a
    jp z, 0xE44A
    dec a
    jp z, 0xE9B7
    jp 0xE945
  __endasm;
}

// Hand over to the ROM boot for kind 0 = floppy, 1 = Quick Disk, 2 = tape.
// Never returns; a ROM error restarts the menu with the message in
// ROM_ERR_CELL (bridge) or lands in the ROM menu (unknown ROM).
void rom_boot(uint8_t kind) {
  if (rom_supported(kind)) rom_boot_bridge(kind);
  else if (kind != 1) rom_boot_plain(kind);
  // QD on a ROM without the 9Z-504M driver: E9B7 is some other routine
  // there (a RAM-disk probe on JSS, a message printer on Willy's; neither
  // has a QD driver at all) - do nothing, the callers check first
}

// QD boot is possible only with the 9Z-504M QD driver in ROM
uint8_t qd_boot_supported(void) {
  return rom_supported(1);
}

// Message left by the ROM boot bridge: pointer to a Sharp-ASCII string in
// ROM (0x0D terminated), or 0. Clears the cell.
const char *rom_boot_error(void) {
  uint8_t *c = (uint8_t *)ROM_ERR_CELL;
  const char *msg;
  if (c[0] != 0x4D || c[1] != 0x5A) return 0;
  msg = (const char *)(c[2] | (c[3] << 8));
  c[0] = 0;
  if ((uint16_t)msg < ROM_BRIDGE) return 0;   // ROM text or a text inside the bridge
  return msg;
}

void execute_floppy(void) { rom_boot(0); }
void execute_quickdisk(void) { rom_boot(1); }
void execute_tape(void) { rom_boot(2); }
