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
  uc_cmd(cmdFDDMOUNT);
  uc_wr(dev);
  uc_wstr(path);
  uc_status4(st);
  return check_error(st) ? 1 : 0;
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

void execute_floppy(void) __naked {
  __asm
    ld sp, MZF_HEADER_START
    jp 0xe44a
  __endasm;
}

void execute_quickdisk(void) __naked {
  __asm
    ld sp, MZF_HEADER_START
    jp 0xe9b7
  __endasm;
}
