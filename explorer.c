#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <input.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "explorer.h"
#include "console.h"
#include "mz-comm.h"

DEV_ENTRY devices[MAX_DEVICES];
DIR_ENTRY entries[MAX_ENTRIES];
#define VISIBLE_ENTRIES 21
uint16_t device_selected;
uint16_t dev_items;
uint16_t dir_items;
uint16_t file_selected;
uint16_t file_offset;
uint16_t menu_poll;
uint8_t wifi_status;
uint8_t initial_key;
uint16_t wifi_status_blink;
char search_str[MAX_SEARCH];
uint8_t search_ln;
char path[255];


// Bottom line: [n/m] counter at x=1..9, messages at x=11..38; a message
// stays until the next key press
static uint8_t msg_shown;
void clear_message(void) {
  put_multi_char_xy(11, 23, ' ', 28);
  msg_shown = 0;
}

void show_error(const char *msg) {
  char line[29];
  strncpy(line, msg, 28);
  line[28] = 0;
  clear_message();
  put_str_xy(11, 23, line);
  msg_shown = 1;
}

void read_dir(char *path) {
  uint8_t ret;
  ret = list_dir(path, &dir_items, entries);
  if (ret)
    show_error(error_description);
}


void u32toa(uint32_t value, char *str) {
    char buf[11];
    uint8_t i = 0;
    if (value == 0) {
        *str++ = '0';
        *str = 0;
        return;
    }
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0) {
        *str++ = buf[--i];
    }
    *str = 0;
}

void display_item(uint16_t index, uint8_t screen_line) {
    DIR_ENTRY *entry = &entries[index];
    char fileinfo[40];
    char *p = fileinfo;

    const char *name = entry->filename;
    uint8_t name_len = 0;

    // Estimate how many chars the size field will take
    uint8_t size_field_len;
    char size_str[10];

    if (entry->isDir) {
        // [directory]
        *p++ = '[';
        while (*name && name_len < 36) {
            *p++ = *name++;
            name_len++;
        }
        *p++ = ']';
        *p = 0;
        put_multi_char_xy(3 + name_len, screen_line, ' ', 36 - name_len);
    } else {
        uint32_t size = entry->size;
        if (size >= 10000) {
            uint32_t kb = (size + 512) / 1024;
            u32toa(kb, size_str);
            size_field_len = strlen(size_str) + 1;  // +1 for 'K'
        } else {
            u32toa(size, size_str);
            size_field_len = strlen(size_str);
        }

        // Limit filename length to (38 - size_field_len)
        uint8_t max_name_len = 38 - size_field_len;
        while (*name && name_len < max_name_len) {
            *p++ = *name++;
            name_len++;
        }

        // Add padding spaces (optional: skip if unnecessary)
        while ((uint8_t)(p - fileinfo) < (38 - size_field_len)) {
            *p++ = ' ';
        }

        // Add size
        char *s = size_str;
        while (*s) *p++ = *s++;
        if (size >= 10000)
            *p++ = 'k';

        *p = 0;
    }

    put_str_xy(1, screen_line, fileinfo);
}

void display_items(uint16_t offset) {
  int16_t items_to_display=dir_items;
  uint8_t i;
  uint8_t j;
  char line[40];
  if (dir_items - offset > VISIBLE_ENTRIES)
    items_to_display = VISIBLE_ENTRIES;
  else
    items_to_display = dir_items - offset;
  for(i=0; i<items_to_display; i++) {
    display_item(offset + i, i+2);
  }
  memset(line, ' ', 38);
  line[38] = 0;
  for(i=items_to_display; i<VISIBLE_ENTRIES; i++)
    put_str_xy(1, i+2, line);
}

// Drop the selection bar and the [n/m] counter (before a listing changes)
void deselect_file(void) {
    uint16_t old_line = file_selected - file_offset + 2;
    put_char_attr_xy(0, old_line, ' ', 0x75);
    put_char_attr_xy(39, old_line, ' ', 0x75);
    put_multi_attr_xy(1, old_line, 0x71, 38);
    put_multi_char_xy(1, 23, ' ', 9);
}

// Selection bar and [n/m] counter for file_selected (also used to restore
// the listing after an overlay)
void draw_selection(void) {
    uint16_t new_line = file_selected - file_offset + 2;
    char buff[10];
    put_multi_attr_xy(1, new_line, 0x16, 38);
    put_multi_attr_xy(entries[file_selected].isDir ? 2 : 1, new_line, 0x02, search_ln);
    put_char_attr_xy(0, new_line, 0xFC, 0xa5);
    put_char_attr_xy(39, new_line, 0xFA, 0xa5);
    sprintf(buff, "[%3d/%3d]", file_selected + 1, dir_items);
    put_str_xy(1, 23, buff);
}

// Entry indices are 16-bit everywhere: dir_items can exceed 255
void select_file(uint16_t index) {
    uint16_t old_line = file_selected - file_offset + 2;
    uint16_t new_line;

    // Clear old selection indicators
    put_char_attr_xy(0, old_line, ' ', 0x75);
    put_char_attr_xy(39, old_line, ' ', 0x75);

    uint8_t needs_redraw = 0;

    // Determine if full redraw is needed based on new index
    if (index > file_selected + 1) {
        if (index > file_offset + VISIBLE_ENTRIES - 2) {
            file_offset = (index == dir_items - 1)
                          ? index - VISIBLE_ENTRIES + 1
                          : index - VISIBLE_ENTRIES + 2;
            needs_redraw = 1;
        }
    } else if (index + 1 < file_selected) {
        if (index < file_offset + 1) {
            file_offset = (index == 0) ? 0 : index - 1;
            needs_redraw = 1;
        }
    } else if ((index > file_offset + VISIBLE_ENTRIES - 2) && (index < dir_items - 1)) {
        file_offset = index - VISIBLE_ENTRIES + 2;
        scroll_up(3,20);
        display_item(file_offset + VISIBLE_ENTRIES - 1, 22);
    } else if ((index < file_offset + 1) && (index > 0)) {
        file_offset = index - 1;
        scroll_down(3,20);
        display_item(file_offset, 2);
    }

    put_multi_attr_xy(1, old_line, 0x71, 38);
    if (needs_redraw)
        display_items(file_offset);

    file_selected = index;
    draw_selection();
}

void select_filename(char *file) {
  uint16_t i = 0;

  while (i<dir_items) {
    if (!strnicmp(file, entries[i].filename, strlen(file))) {
      select_file(i);
      break;
    }
    i++;
  }
}

void select_next(uint8_t delta) {
  if (dir_items == 0)
    return;
  if (file_selected + delta >= dir_items)
    delta = dir_items - file_selected - 1;
  if (file_selected < dir_items-1)
    search_ln = 0;
    select_file(file_selected+delta);
}

void select_prev(uint8_t delta) {
  if (dir_items == 0)
    return;
  if (file_selected < delta)
    delta = file_selected;
  if (file_selected != 0)
    search_ln = 0;
    select_file(file_selected-delta);
}

void display_path(char *path) {
  put_str_xy(1, 1, path);
  uint16_t ln = strlen(path);
  put_multi_char_xy(ln + 1, 1, ' ', 37 - ln);
  if (!list_launchable_only) put_str_attr_xy(35, 1, "ALL", 0x65);
}

uint8_t is_root_directory(const char *path) {
    size_t len = strlen(path);

    // Must be at least 3 characters: "x:/"
    if (len < 3)
        return 0;

    // Check if the last two characters are ":/"
    if (path[len - 2] == ':' && path[len - 1] == '/')
        return 1;

    return 0;
}

void remove_last_dir(char *path, char *removed) {
  size_t len = strlen(path);

  // Remove trailing slash if present (but not if it's the root "/")
  if (!is_root_directory(path) && path[len - 1] == '/') {
    path[len - 1] = '\0';
    len--;
  }

  // Find the last slash
  char *last_slash = strrchr(path, '/');
  if (last_slash && last_slash != path && last_slash != path + len - 1) {
    strcpy(removed, last_slash + 1);  // Copy the last directory name
    *last_slash = '\0';               // Truncate at last slash
  } else {
    removed[0] = '\0';  // Nothing removed
  }

  len = strlen(path);
  if (path[len - 1] == ':') {
    path[len] = '/';
    path[len + 1] = '\0';
  }
}

// ---- persisted explorer state: last location and recent launches ----
// mzpico.sav like mzpico.ini: sd:/ first, flash:/ as the fallback (flash is
// always mounted, the card is optional; writes go to the card when it is
// there, so the flash map is not worn by browsing). Written at every launch
// and directory change, read at start: the explorer reopens where it was
// and F4 lists recent launches. ~676 bytes.
#define REC_N 8
#define REC_LN 64
typedef struct {
  char magic[4];
  char last_path[128];
  char last_file[32];
  char recent[REC_N][REC_LN];
} explorer_state_t;
static explorer_state_t st;

static uint8_t has_volume(const char *name) {
  uint8_t i;
  for (i = 0; i < dev_items; i++)
    if (!strcmp(devices[i].name, name)) return 1;
  return 0;
}

static void save_state(void) {
  memcpy(st.magic, "MZX1", 4);
  write_file(has_volume("sd") ? "sd:/mzpico.sav" : "flash:/mzpico.sav", (const uint8_t *)&st, sizeof(st));
}

static uint8_t load_from(const char *p) {
  return read_file(p, (uint8_t *)&st, sizeof(st)) == 0 && memcmp(st.magic, "MZX1", 4) == 0;
}

static void load_state(void) {
  if (!((has_volume("sd") && load_from("sd:/mzpico.sav")) || load_from("flash:/mzpico.sav"))) {
    memset(&st, 0, sizeof(st));
    memcpy(st.magic, "MZX1", 4);
  }
  st.last_path[sizeof(st.last_path) - 1] = 0;
  st.last_file[sizeof(st.last_file) - 1] = 0;
}

// full may point INTO st.recent (a launch from the recent list): copy it
// before the list is re-ordered, or the slot changes under the caller.
static void remember_launch(const char *dir, const char *name, const char *full) {
  char item[REC_LN];
  uint8_t i, j;
  strncpy(st.last_path, dir, sizeof(st.last_path) - 1);
  strncpy(st.last_file, name, sizeof(st.last_file) - 1);
  if (strlen(full) < REC_LN) {
    strcpy(item, full);
    for (i = 0; i < REC_N && strcmp(st.recent[i], item); i++);
    if (i == REC_N) i = REC_N - 1;
    for (j = i; j > 0; j--) memcpy(st.recent[j], st.recent[j - 1], REC_LN);
    strcpy(st.recent[0], item);
  }
  save_state();
}

// Clear the screen and run a mounted entry by extension; never returns
// for MZF/DSK/MZQ.
static void run_mounted(const char *full) {
  char extension[16];
  border(0);
  clrscr();
  loading_screen(full);
  get_uppercase_extension(full, extension);
  if (!strcmp(extension, "MZF") || !strcmp(extension, "M12"))
    read_and_execute();
  else if (!strcmp(extension, "DSK"))
    execute_floppy();
  else if (!strcmp(extension, "MZQ"))
    execute_quickdisk();
}

// Launch a file by full path (listing or recent list): mount first with
// the listing still on screen, remember it, then run.
// The device holds ONE open file: an MZF stays open from mount_entry() to
// the loader, and writing the state file in between would replace it (the
// loader then reads an empty header and returns to the menu). So: mount
// to validate, close, save the state, mount again for the loader.
static void launch_full(const char *full) {
  char dir[128];
  char ext[16];
  const char *slash = strrchr(full, '/');
  size_t dl = slash ? (size_t)(slash - full) : 0;
  get_uppercase_extension(full, ext);
  if (!strcmp(ext, "MZQ") && !qd_boot_supported()) {
    show_error("QD boot needs the 9Z-504M ROM");
    return;
  }
  if (mount_entry(full)) {
    show_error(error_description);
    return;
  }
  uc_cmd(cmdCLOSE);
  if (dl >= sizeof(dir)) dl = sizeof(dir) - 1;
  memcpy(dir, full, dl);
  dir[dl] = 0;
  if (dl > 0 && dir[dl - 1] == ':') { dir[dl] = '/'; dir[dl + 1] = 0; }
  remember_launch(dir, slash ? slash + 1 : full, full);
  if (mount_entry(full)) {
    show_error(error_description);
    return;
  }
  clear_message();
  run_mounted(full);
}

// Footer legend: F1..F5, and with SHIFT held the shifted set
static uint8_t footer_shifted;
void draw_footer(uint8_t shifted) {
  footer_shifted = shifted;
  put_str_attr_xy(0, 24, shifted ? "        Del    Ren    Mkd    All    Menu"
                                 : "        Inf    Dev    Mnt    Rec    Quit", 0x70);
  put_str_attr_xy(0, 24, "\xc1\xc2\xc3\xc4", 0x60);
  put_str_attr_xy(5, 24, "F1", 0x06);
  put_str_attr_xy(12, 24, "F2", 0x06);
  put_str_attr_xy(19, 24, "F3", 0x06);
  put_str_attr_xy(26, 24, "F4", 0x06);
  put_str_attr_xy(33, 24, "F5", 0x06);
}

void show_info(void);
static void selected_full(char *buf, size_t max);
void execute_selection(void) {
  char last_dir[32];
  char *filename;

  if (dir_items == 0)
    return;

  filename = entries[file_selected].filename;
  if (entries[file_selected].isDir) {
    search_ln = 0;
    if (strcmp(filename, "..") == 0) {
      remove_last_dir(path, last_dir);
    } else {
      if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
      }
      strncat(path, filename, sizeof(path) - strlen(path) - 1);
      last_dir[0] = 0;
    };
    display_path(path);
    read_dir(path);
    display_items(0);
    if (last_dir[0] == 0)
      select_file(0);
    else
      select_filename(last_dir);
    strncpy(st.last_path, path, sizeof(st.last_path) - 1);
    st.last_file[0] = 0;
    save_state();
  } else {
    size_t dir_len = strlen(path);
    uint8_t i;
    char ext[16];
    if (path[dir_len - 1] != '/') {
      strcat(path, "/");
    }
    strncat(path, filename, sizeof(path) - strlen(path) - 1);
    get_uppercase_extension(filename, ext);
    if (strcmp(ext, "MZF") && strcmp(ext, "M12") && strcmp(ext, "DSK") && strcmp(ext, "MZQ")) {
      path[dir_len] = 0;
      show_info();            // not launchable: show what it is instead
      return;
    }
    for (i=0; i<255; i++) {
      put_multi_attr_xy(1, file_selected - file_offset +2, 0x16, 38);
      put_multi_attr_xy(1, file_selected - file_offset +2, 0x61, 38);
    };
    launch_full(path);
    path[dir_len] = 0;          // mount failed: back to the directory view
  }
}

int strnicmp(const char *s1, const char *s2, size_t n) {
    while (n-- > 0) {
        char c1 = tolower((unsigned char)*s1++);
        char c2 = tolower((unsigned char)*s2++);
        if (c1 != c2) {
            return (unsigned char)c1 - (unsigned char)c2;
        }
        if (c1 == '\0') {
            break;
        }
    }
    return 0;
}


void search(char c) {
  uint16_t i;

  if (search_ln == MAX_SEARCH - 1)
    return;
  i=0;

  search_str[search_ln] = c;
  while (i<dir_items) {
    if (!strnicmp(search_str, entries[i].filename, search_ln+1)) {
      search_ln++;
      select_file(i);
      break;
    }
    i++;
  }
}

// F1: info about the selected entry, as a framed overlay (rows 8-15,
// x 3-36) in the manager's look: cyan frame with the chamfered corner
// glyphs of the list box, black interior, yellow labels, white values.
// For MZF/M12 the 128-byte header is read from the file (Sharp name,
// attribute, load/exec, body size); for DSK the extended-DSK geometry.
#define INFO_X 3
#define INFO_W 34
#define ATTR_FRAME 0x05
#define ATTR_BAR_TEXT 0x05   // black on the cyan bars (white was hard to read)
#define ATTR_BODY 0x70
#define ATTR_LABEL 0x60
static uint8_t ov_top, ov_bottom;

static void info_line(uint8_t row, const char *label, const char *value) {
  uint8_t ln = strlen(label);
  put_char_attr_xy(INFO_X, row, ' ', ATTR_FRAME);
  put_multi_char_xy(INFO_X + 1, row, ' ', INFO_W - 2);
  put_multi_attr_xy(INFO_X + 1, row, ATTR_BODY, INFO_W - 2);
  put_char_attr_xy(INFO_X + INFO_W - 1, row, ' ', ATTR_FRAME);
  put_str_attr_xy(INFO_X + 2, row, label, ATTR_LABEL);
  put_str_xy(INFO_X + 2 + ln, row, value);
}

static void info_bar(uint8_t row, const char *text, uint8_t tl, uint8_t tr, uint8_t attr_l, uint8_t attr_r) {
  put_multi_char_xy(INFO_X, row, ' ', INFO_W);
  put_multi_attr_xy(INFO_X, row, ATTR_FRAME, INFO_W);
  put_char_attr_xy(INFO_X, row, tl, attr_l);
  put_char_attr_xy(INFO_X + INFO_W - 1, row, tr, attr_r);
  put_str_attr_xy(INFO_X + (INFO_W - strlen(text)) / 2, row, text, ATTR_BAR_TEXT);
}

// Framed overlay rows top..bottom in the manager look (cyan frame with the
// list box's chamfered corners, black interior); rows between are blank
static void overlay_open(const char *title, const char *hint, uint8_t top, uint8_t bottom) {
  uint8_t r;
  ov_top = top; ov_bottom = bottom;
  info_bar(top, title, 0xfe, 0xfd, 0x51, 0x51);
  for (r = top + 1; r < bottom; r++) info_line(r, "", "");
  info_bar(bottom, hint, 0xfd, 0xfe, 0x15, 0x15);
}

// Restore the listing under the overlay. Waits for the key that closed it
// to be released first, so it cannot autorepeat into the listing (a held CR
// would execute the selection).
static void overlay_close(void) {
  uint8_t r;
  key_release();
  for (r = ov_top; r <= ov_bottom; r++)
    put_multi_attr_xy(1, r, 0x71, 38);
  display_items(file_offset);
  if (dir_items)
    draw_selection();
}

static void hex4(uint16_t v, char *out) {
  static const char h[] = "0123456789ABCDEF";
  out[0] = h[(v >> 12) & 15]; out[1] = h[(v >> 8) & 15];
  out[2] = h[(v >> 4) & 15]; out[3] = h[v & 15]; out[4] = 0;
}

// Last n characters of a path for a 28-column value field
static const char *tail_of(const char *p, uint8_t n) {
  size_t l = strlen(p);
  return l > n ? p + l - n : p;
}

void show_info(void) {
  char line[40];
  char num[12];
  char ext[16];
  uint8_t head[52];
  uint8_t i;
  size_t dir_len;
  DIR_ENTRY *e = dir_items ? &entries[file_selected] : 0;

  uint8_t on_cloud = e && !e->isDir && !strncmp(path, "cloud:", 6);
  overlay_open(" File info ", on_cloud ? " S save to card  ESC " : " ESC ", 8, 15);

  info_line(9, "Name: ", e ? e->filename : "-");

  ext[0] = 0;
  if (!e) strcpy(line, "-");
  else if (e->isDir) strcpy(line, "directory");
  else {
    get_uppercase_extension(e->filename, ext);
    if (!strcmp(ext, "MZF") || !strcmp(ext, "M12")) strcpy(line, "program (MZF)");
    else if (!strcmp(ext, "DSK")) strcpy(line, "floppy image (DSK)");
    else if (!strcmp(ext, "MZQ")) strcpy(line, "quick disk image (MZQ)");
    else strcpy(line, "file");
  }
  info_line(10, "Type: ", line);

  if (e && !e->isDir) {
    u32toa(e->size, line); strcat(line, " bytes");
    if (e->size >= 1024) { strcat(line, " ("); u32toa((e->size + 512) / 1024, num); strcat(line, num); strcat(line, "k)"); }
  } else strcpy(line, "-");
  info_line(11, "Size: ", line);

  if (e && !e->isDir && ext[0]) {
    dir_len = strlen(path);
    if (path[dir_len - 1] != '/') strcat(path, "/");
    strncat(path, e->filename, sizeof(path) - strlen(path) - 1);
    if (read_file_head(path, head, sizeof(head))) {
      info_line(12, "", error_description);
    } else if (!strcmp(ext, "MZF") || !strcmp(ext, "M12")) {
      line[0] = 0;
      for (i = 1; i < 18 && head[i] != 0x0d; i++) {
        char c = (char)head[i];
        strncat(line, (c >= 0x20 && c < 0x60) ? &c : "?", 1);
      }
      info_line(12, "MZF name: ", line);
      hex4(head[0], num); strcpy(line, num + 2);
      strcat(line, "  Load "); hex4(head[20] | (head[21] << 8), num); strcat(line, num);
      strcat(line, "  Exec "); hex4(head[22] | (head[23] << 8), num); strcat(line, num);
      info_line(13, "Attr ", line);
      hex4(head[18] | (head[19] << 8), num);
      info_line(14, "Body: ", num);
    } else if (!strcmp(ext, "DSK")) {
      u32toa(head[0x30], line);
      strcat(line, "  Sides: "); u32toa(head[0x31], num); strcat(line, num);
      info_line(12, "Tracks: ", line);
      line[0] = 0;
      for (i = 0; i < 14 && head[0x22 + i] >= 0x20 && head[0x22 + i] < 0x7f; i++)
        strncat(line, (char *)&head[0x22 + i], 1);
      info_line(13, "Creator: ", line);
    }
    path[dir_len] = 0;
  }

  i = wait_key();
  overlay_close();
  if (on_cloud && (i == 'S' || i == 's')) {
    char full[160];
    char dst[48];
    const char *vol = has_volume("sd") ? "sd:/" : "flash:/";
    if (strlen(vol) + strlen(e->filename) >= sizeof(dst)) { show_error("Name too long"); return; }
    strcpy(dst, vol); strcat(dst, e->filename);
    selected_full(full, sizeof(full));
    if (copy_file(full, dst)) { show_error(error_description); return; }
    strcpy(dst, "Saved to "); strcat(dst, vol);
    show_error(dst);
  }
}

// F3: mount manager. Shows what is in floppy drives 1-4 and the Quick
// Disk; with a DSK, MZQ or directory selected, 1-4 / Q mount it there
// (session-only, like the ini mounts) and B boots it (DSK/MZQ). Any other
// key closes.
void show_mounts(void) {
  char mb[5 * 70];
  char line[40];
  char ext[16];
  uint8_t i, k, n, mountable = 0, is_mzq = 0;
  size_t dir_len = strlen(path);
  DIR_ENTRY *e = dir_items ? &entries[file_selected] : 0;

  ext[0] = 0;
  if (e) {
    if (e->isDir) mountable = strcmp(e->filename, "..") != 0;
    else {
      get_uppercase_extension(e->filename, ext);
      if (!strcmp(ext, "DSK")) mountable = 1;
      else if (!strcmp(ext, "MZQ")) { mountable = 1; is_mzq = 1; }
    }
  }
  if (mountable) {
    if (path[dir_len - 1] != '/') strcat(path, "/");
    strncat(path, e->filename, sizeof(path) - strlen(path) - 1);
  }
  for (;;) {
    const char *l = mb;
    overlay_open(" Mounts ", mountable ? (is_mzq ? " Q mount  B boot  ESC " : " 1-4 mount  B boot  ESC ") : " ESC ", 8, 15);
    n = get_mounts(mb, sizeof(mb));
    for (i = 0; i < n && i < 5; i++) {
      strcpy(line, "  "); line[0] = l[0]; line[1] = ':';
      info_line(9 + i, line, l[2] ? tail_of(l + 2, 27) : "-");
      l += strlen(l) + 1;
    }
    if (mountable) info_line(14, "", tail_of(e->filename, 30));
    k = wait_key();
    if (!mountable) break;
    if (k >= 'a' && k <= 'z') k -= 32;
    if (!is_mzq && k >= '1' && k <= '4') {
      if (mount_into(k - '1', path)) show_error(error_description);
      continue;
    }
    if (is_mzq && k == 'Q') {
      if (mount_into(UC_DEV_QD, path)) show_error(error_description);
      continue;
    }
    if (k == 'B' && !e->isDir) {
      overlay_close();
      launch_full(path);
      break;
    }
    break;
  }
  path[dir_len] = 0;
  overlay_close();
}

// F4: recent launches, newest first; 1-8 launches, any other key closes
void show_recent(void) {
  char label[4];
  uint8_t i, k, n = 0;
  overlay_open(" Recent ", " 1-8 launch  ESC ", 6, 16);
  for (i = 0; i < REC_N; i++) {
    if (!st.recent[i][0]) break;
    label[0] = '1' + i; label[1] = ' '; label[2] = 0;
    info_line(7 + i, label, tail_of(st.recent[i], 28));
    n++;
  }
  if (!n) info_line(7, "", "nothing launched yet");
  k = wait_key();
  overlay_close();
  if (k >= '1' && k < '1' + n) {
    char full[REC_LN];
    strcpy(full, st.recent[k - '1']);   // the launch re-orders st.recent
    launch_full(full);
  }
}

// ---- file operations (SHIFT+F1..F4) ----
// Full path of the selected entry into buf (the directory path + name)
static void selected_full(char *buf, size_t max) {
  size_t l = strlen(path);
  strncpy(buf, path, max - 1); buf[max - 1] = 0;
  if (l && buf[l - 1] != '/' && l < max - 1) strcat(buf, "/");
  strncat(buf, entries[file_selected].filename, max - strlen(buf) - 1);
}

// Re-list the current directory and put the cursor on name (or index)
static void relist(const char *name, uint16_t index) {
  deselect_file();
  read_dir(path);
  display_items(0);
  if (dir_items == 0) { put_str_xy(5, 10, "No files found on this device"); return; }
  if (index >= dir_items) index = dir_items - 1;
  select_file(index);
  if (name && name[0]) select_filename(name);
}

void delete_selected(void) {
  char full[160];
  uint8_t k;
  if (!dir_items || !strcmp(entries[file_selected].filename, "..")) return;
  selected_full(full, sizeof(full));
  overlay_open(" Delete ", " Y delete  ESC ", 9, 13);
  info_line(10, "", tail_of(entries[file_selected].filename, 30));
  info_line(11, "", entries[file_selected].isDir ? "directory (must be empty)" : "file");
  k = wait_key();
  overlay_close();
  if (k != 'Y' && k != 'y') return;
  if (fs_unlink(full)) { show_error(error_description); return; }
  relist(0, file_selected);
}

void rename_selected(void) {
  char full[160];
  char newp[160];
  char name[32];
  uint8_t ok;
  size_t l;
  if (!dir_items || !strcmp(entries[file_selected].filename, "..")) return;
  selected_full(full, sizeof(full));
  strncpy(name, entries[file_selected].filename, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
  overlay_open(" Rename ", " CR ok  ESC ", 9, 13);
  info_line(10, "", "New name:");
  ok = input_line(INFO_X + 2, 11, INFO_W - 4, name, sizeof(name));
  overlay_close();
  if (!ok || !name[0] || !strcmp(name, entries[file_selected].filename)) return;
  l = strlen(path);
  strncpy(newp, path, sizeof(newp) - 1); newp[sizeof(newp) - 1] = 0;
  if (l && newp[l - 1] != '/') strcat(newp, "/");
  strncat(newp, name, sizeof(newp) - strlen(newp) - 1);
  if (fs_rename(full, newp)) { show_error(error_description); return; }
  relist(name, file_selected);
}

void mkdir_prompt(void) {
  char newp[160];
  char name[32];
  uint8_t ok;
  size_t l;
  name[0] = 0;
  overlay_open(" New folder ", " CR ok  ESC ", 9, 13);
  info_line(10, "", "Name:");
  ok = input_line(INFO_X + 2, 11, INFO_W - 4, name, sizeof(name));
  overlay_close();
  if (!ok || !name[0]) return;
  l = strlen(path);
  strncpy(newp, path, sizeof(newp) - 1); newp[sizeof(newp) - 1] = 0;
  if (l && newp[l - 1] != '/') strcat(newp, "/");
  strncat(newp, name, sizeof(newp) - strlen(newp) - 1);
  if (fs_mkdir(newp)) { show_error(error_description); return; }
  relist(name, 0);
}

// SHIFT+F5: add the selected launchable file to the boot menu - a key
// letter, a description (prefilled from the name), then SETCONFIG writes
// key_<x>=<desc>|<path> into [menu] of the ini that was loaded (sd:/ or
// flash:/); the menu shows it at its next start.
void add_to_menu(void) {
  char full[160];
  char desc[25];
  char keyname[8];
  char value[64];
  char ext[16];
  uint8_t k, i, ok;
  DIR_ENTRY *e = dir_items ? &entries[file_selected] : 0;
  if (!e || e->isDir) return;
  get_uppercase_extension(e->filename, ext);
  if (strcmp(ext, "MZF") && strcmp(ext, "M12") && strcmp(ext, "DSK") && strcmp(ext, "MZQ")) { show_error("Not launchable"); return; }
  selected_full(full, sizeof(full));
  if (strncmp(full, "sd:", 3) && strncmp(full, "flash:", 6)) { show_error("Only sd: and flash: files"); return; }
  if (strlen(full) > 40) { show_error("Path too long for the menu (40)"); return; }
  overlay_open(" Add to menu ", " ESC ", 8, 15);
  info_line(9, "", tail_of(e->filename, 30));
  info_line(10, "Key: ", "press A-Z or 0-9");
  k = wait_key();
  if (k >= 'a' && k <= 'z') k -= 32;
  if (!((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'))) { overlay_close(); return; }
  if (k == 'F' || k == 'Q' || k == 'C' || k == 'M') { overlay_close(); show_error("F Q C M are built-in keys"); return; }
  keyname[0] = k; keyname[1] = 0;
  info_line(10, "Key: ", keyname);
  for (i = 0; i < sizeof(desc) - 1 && e->filename[i] && e->filename[i] != '.'; i++) desc[i] = e->filename[i];
  desc[i] = 0;
  info_line(11, "Name:", "");
  ok = input_line(INFO_X + 2, 12, 24, desc, sizeof(desc));
  overlay_close();
  if (!ok || !desc[0]) return;
  strcpy(keyname, "key_"); keyname[4] = (k >= 'A' && k <= 'Z') ? k + 32 : k; keyname[5] = 0;
  strcpy(value, desc); strcat(value, "|"); strcat(value, full);
  if (set_config("menu", keyname, value)) { show_error(error_description); return; }
  strcpy(value, "Added to menu as "); value[17] = k; value[18] = 0;
  show_error(value);
}

void toggle_show_all(void) {
  list_launchable_only ^= 1;
  display_path(path);
  relist(dir_items ? entries[file_selected].filename : 0, 0);
}

void refresh_device(void) {
  deselect_file();
  clear_message();
  sprintf(path, "%s:/", devices[device_selected].name);
  strncpy(st.last_path, path, sizeof(st.last_path) - 1);
  st.last_file[0] = 0;
  save_state();
  display_path(path);
  read_dir(path);
  display_items(0);
  if (dir_items>0) {
    select_file(0);
  } else {
    put_str_xy(5, 10, "No files found on this device");
  }
}

void cycle_device(void) {
  if (dev_items == 1)
    return;
  device_selected++;
  if (device_selected >= dev_items)
    device_selected = 0;
  search_ln = 0;
  refresh_device();
}

// Reopen the saved location (volume must exist and list); selects the
// last launched file. Returns 0 when there is nothing to resume.
static uint8_t resume_state(void) {
  uint8_t i;
  const char *colon = strchr(st.last_path, ':');
  if (!st.last_path[0] || !colon) return 0;
  for (i = 0; i < dev_items; i++)
    if (!strncmp(devices[i].name, st.last_path, colon - st.last_path) && strlen(devices[i].name) == (size_t)(colon - st.last_path))
      break;
  if (i == dev_items) return 0;
  device_selected = i;
  deselect_file();
  strcpy(path, st.last_path);
  display_path(path);
  dir_items = 0;
  if (list_dir(path, &dir_items, entries) || dir_items == 0) return 0;
  display_items(0);
  select_file(0);
  if (st.last_file[0]) select_filename(st.last_file);
  return 1;
}

void explorer_init(void) {
  dir_items = 0;
  file_selected = 0;
  file_offset = 0;
  search_ln = 0;
  menu_poll = 0;
  wifi_status = 0;
  wifi_status_blink = 0;
  initial_key = 0;

  uint8_t ret = list_dev(&dev_items, devices);
  if (ret) {
    show_error(error_description);
  }
  for (uint8_t i=0; i<dev_items; i++) {
    if (strcmp(devices[i].name, "sd") == 0) {
      put_char_attr_xy(37, 0, 0xbf, 0xc1);
      break;
    }
  }

  device_selected = 0;
  load_state();
  if (resume_state())
    initial_key = 1;             // a resumed view is intent: WiFi connecting must not switch to cloud
  else
    refresh_device();
}

void explorer_handle_key(uint8_t c) {
  if (c) initial_key = c;
  if (c && msg_shown) clear_message();
  switch (c) {
    case 0x01:
      show_info();
      break;
    case 0x02:
      cycle_device();
      break;
    case 0x03:
      show_mounts();
      break;
    case 0x04:
      show_recent();
      break;
    case 0x81:
      delete_selected();
      break;
    case 0x82:
      rename_selected();
      break;
    case 0x83:
      mkdir_prompt();
      break;
    case 0x84:
      toggle_show_all();
      break;
    case 0x85:
      add_to_menu();
      break;
    case 0x11:
      select_next(1);
      break;
    case 0x12:
      select_prev(1);
      break;
    case 0x13:
      select_next(20);
      break;
    case 0x14:
      select_prev(20);
      break;
    case 0x0a:
    case 0x0d:  // CR mapping differs between z88dk versions (\n vs \r)
      execute_selection();
      break;
  }
  if ((c>='A') && (c<='Z') || (c>='0') && (c<='9'))
    search(c);
}

void explorer_poll(void) {
  uint8_t prev_wifi_status = wifi_status;
  menu_poll++;
  if (key_shift != footer_shifted) draw_footer(key_shift);
  char c;
  char attr;
  if (menu_poll == 250) {              // inkey() scans are ~0.5 ms: 8 Hz status, 4 Hz blink
    menu_poll = 0;
    wifi_status = get_wifi_status();
    c = 0xb5;
    attr = 0xe1;
    switch(wifi_status) {
      case WIFI_STATUS_INIT:
      case WIFI_STATUS_STARTING:
        attr = 0xa1;
      case WIFI_STATUS_CONNECTING:
        if (wifi_status == prev_wifi_status) {
          wifi_status_blink++;
        } else {
          wifi_status_blink = 0;
        }
        if ((wifi_status_blink & 0x02) == 0) {
          c = 0xb5;
        } else {
          c = ' ';
        }
        break;
      case WIFI_STATUS_CONNECTED:
        c = 0xb5;
        attr = 0xc1;
        if (prev_wifi_status != WIFI_STATUS_CONNECTED) {
          list_dev(&dev_items, devices);
          if (!initial_key || (dir_items == 0)) {
            device_selected = dev_items - 1;
            refresh_device();
          }
        }
        break;
      case WIFI_STATUS_DISCONNECTED:
        c = 0xb5;
        attr = 0xe1;
        break;
      case WIFI_STATUS_ERROR:
        c = 0xb5;
        attr = 0xa1;
        break;
      case WIFI_STATUS_NOT_SUPPORTED:
        c = ' ';
        break;
      default:
        c = ' ';
        break;
    }
    put_char_attr_xy(38, 0, c, attr);
  }
}