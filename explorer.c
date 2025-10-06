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
char search_str[MAX_SEARCH];
uint8_t search_ln;
char path[255];


void read_dir(char *path) {
  uint8_t ret;
  ret = list_dir(path, &dir_items, entries);
  if (ret)
  {
    put_str_xy(15, 23, error_description);
  }
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

void display_item(uint8_t index, uint8_t screen_line) {
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

void select_file(uint8_t index) {
    uint16_t old_line = file_selected - file_offset + 2;
    uint16_t new_line;
    uint8_t i;

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

    if (needs_redraw) {
        put_multi_attr_xy(1, old_line, 0x71, 38);
        display_items(file_offset);
    } else {
        put_multi_attr_xy(1, old_line, 0x71, 38);
    }

    // Draw new selection indicators
    new_line = index - file_offset + 2;
    put_multi_attr_xy(1, new_line, 0x16, 38);
    put_multi_attr_xy(entries[index].isDir ? 2 : 1, new_line, 0x02, search_ln);
    put_char_attr_xy(0, new_line, 0xFC, 0xa5);
    put_char_attr_xy(39, new_line, 0xFA, 0xa5);

    file_selected = index;

    // Display file selection status
    char buff[10];
    sprintf(buff, "[%3d/%3d]", file_selected + 1, dir_items);
    put_str_xy(1, 23, buff);
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

void execute_selection(void) {
  uint8_t i;
  uint8_t ret=0;
  char extension[16];
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
  } else {
    for (i=0; i<255; i++) {
      put_multi_attr_xy(1, file_selected - file_offset +2, 0x16, 38);
      put_multi_attr_xy(1, file_selected - file_offset +2, 0x61, 38);
    };
    border(0);
    clrscr();
    if (path[strlen(path) - 1] != '/') {
      strcat(path, "/");
    }
    strncat(path, filename, sizeof(path) - strlen(path) - 1);
    loading_screen(path);
    ret = mount_entry(path);
    if (ret) {
      put_str_xy(15, 23, error_description);
      return;
    }
    get_uppercase_extension(filename, extension);
    if (!strcmp(extension, "MZF") || !strcmp(extension, "M12"))
      read_and_execute();
    else if (!strcmp(extension, "DSK"))
      execute_floppy();
    else if (!strcmp(extension, "MZQ"))
      execute_quickdisk();
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

void refresh_device(void) {
  sprintf(path, "%s:/", devices[device_selected].name);
  display_path(path);
  read_dir(path);
  display_items(0);
  if (dir_items>0)
    select_file(0);
  else
    put_str_xy(5, 10, "No files found on this device");
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

void explorer_init(void) {
  dir_items = 0;
  file_selected = 0;
  file_offset = 0;
  search_ln = 0;

  uint8_t ret = list_dev(&dev_items, devices);
  if (ret) {
    put_str_xy(15, 23, error_description);
  }

  device_selected = 0;
  refresh_device();
}

void explorer_handle_key(char c) {
  switch (c) {
    case 0x02:
      cycle_device();
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
      execute_selection();
      break;
  }
  if ((c>='A') && (c<='Z') || (c>='0') && (c<='9'))
    search(c);
}
