#include <string.h>
#include <stdio.h>
#include <ctype.h>


#include "mz-comm.h"
#include "console.h"
#include "explorer.h"

#define MAX_MENU_ITEMS 18
#define MAX_MENU_DESCRIPTION_LENGTH 25
static const char *factory_entry_names[MAX_MENU_ITEMS];
static uint8_t (*exec_wrappers[MAX_MENU_ITEMS])(void);
static uint8_t factory_count;

typedef struct {
  char key;
  char description[MAX_MENU_DESCRIPTION_LENGTH];
  uint8_t (*enabled)(void);
  uint8_t (*execute)(void);
} menu_entry_t;

menu_entry_t menu[MAX_MENU_ITEMS];

ConfigEntry config[MAX_MENU_ITEMS + 5];

uint8_t execute_with_mount(const char *entry_name);
void read_and_execute(void);

uint8_t fdc_enabled(void) {
  __asm
    call 0xe8d5
    jr nz, @ret_false
    ld a, 0x01
    jr @ret_s
@ret_false:
    xor a
@ret_s:
    ld l, a
    ld h, 0
  __endasm
}

uint8_t execute_fdc(void) {
  execute_floppy();
}

uint8_t qd_enabled(void) {
  __asm
    call 0xeb13
    jr nz, @ret_false
    ld a, 0x01
    jr @ret_s
@ret_false:
    xor a
@ret_s:
    ld l, a
    ld h, 0
  __endasm
}

uint8_t execute_qd(void) {
  execute_quickdisk();
}

uint8_t execute_tape(void) __naked {
  __asm
    jp 0xe945 
  __endasm
}

uint8_t execute_monitor(void) __naked {
  __asm
    jmp 0xe93b
  __endasm;
}



// Generic dispatcher: looks up entry name by index
static uint8_t exec_func_dispatch(uint8_t index) {
    if (index >= factory_count)
        return 0;
    return execute_with_mount(factory_entry_names[index]);
}

// Each wrapper simply calls the dispatcher with its own index
static uint8_t exec_func_0(void) { return exec_func_dispatch(0); }
static uint8_t exec_func_1(void) { return exec_func_dispatch(1); }
static uint8_t exec_func_2(void) { return exec_func_dispatch(2); }
static uint8_t exec_func_3(void) { return exec_func_dispatch(3); }
static uint8_t exec_func_4(void) { return exec_func_dispatch(4); }
static uint8_t exec_func_5(void) { return exec_func_dispatch(5); }
static uint8_t exec_func_6(void) { return exec_func_dispatch(6); }
static uint8_t exec_func_7(void) { return exec_func_dispatch(7); }
static uint8_t exec_func_8(void) { return exec_func_dispatch(8); }
static uint8_t exec_func_9(void) { return exec_func_dispatch(9); }
static uint8_t exec_func_10(void) { return exec_func_dispatch(10); }
static uint8_t exec_func_11(void) { return exec_func_dispatch(11); }
static uint8_t exec_func_12(void) { return exec_func_dispatch(12); }
static uint8_t exec_func_13(void) { return exec_func_dispatch(13); }
static uint8_t exec_func_14(void) { return exec_func_dispatch(14); }
static uint8_t exec_func_15(void) { return exec_func_dispatch(15); }
static uint8_t exec_func_16(void) { return exec_func_dispatch(16); }
static uint8_t exec_func_17(void) { return exec_func_dispatch(17); }


uint8_t (* exec_with_mount_factory(const char *entry_name))(void) {
    if (factory_count >= MAX_MENU_ITEMS)
        return NULL;

    factory_entry_names[factory_count] = entry_name;
    return exec_wrappers[factory_count++];
}

uint8_t execute_with_mount(const char *entry_name) {
    char extension[5];
    mount_entry(entry_name);
    if (entry_name[0] != '@')
    {
        clrscr();
        loading_screen(entry_name);
    }
    get_uppercase_extension(entry_name, extension);
    if (!strcmp(extension, "DSK"))
      execute_floppy();
    else if (!strcmp(extension, "MZQ"))
      execute_quickdisk();
    else
      read_and_execute();
    return 0;
}

void init(void) {
  clrscr();
  border(0);
}

void reset_menu(void) {
  menu[0].key = 0x00;
}

int add_menu_entry(char key, const char *desc,
                   uint8_t (*enabled)(void),
                   uint8_t (*execute)(void)) {
  int i = 0;

  while (i < MAX_MENU_ITEMS - 1 && menu[i].key != 0x00) {
    i++;
  }

  if (i >= MAX_MENU_ITEMS - 1) {
    return -1;
  }

  // Add new entry
  menu[i].key = key;
  strncpy(menu[i].description, desc, MAX_MENU_DESCRIPTION_LENGTH - 1);
  uint8_t ln = strlen(desc);
  if (ln > MAX_MENU_DESCRIPTION_LENGTH-1)
    ln = MAX_MENU_DESCRIPTION_LENGTH-1;
  menu[i].description[ln] = '\0';
  menu[i].enabled = enabled;
  menu[i].execute = execute;

  // Add new terminator
  menu[i + 1].key = 0x00;

  return 0; // success
}

void init_menu(void) {
  uint16_t menu_entries = 0;

  factory_count = 0;
  exec_wrappers[0] = exec_func_0;
  exec_wrappers[1] = exec_func_1;
  exec_wrappers[2] = exec_func_2;
  exec_wrappers[3] = exec_func_3;
  exec_wrappers[4] = exec_func_4;
  exec_wrappers[5] = exec_func_5;
  exec_wrappers[6] = exec_func_6;
  exec_wrappers[7] = exec_func_7;
  exec_wrappers[8] = exec_func_8;
  exec_wrappers[9] = exec_func_9;
  exec_wrappers[10] = exec_func_10;
  exec_wrappers[11] = exec_func_11;
  exec_wrappers[12] = exec_func_12;
  exec_wrappers[13] = exec_func_13;
  exec_wrappers[14] = exec_func_14;
  exec_wrappers[15] = exec_func_15;
  exec_wrappers[16] = exec_func_16;
  exec_wrappers[17] = exec_func_17;

  reset_menu();
  add_menu_entry('F', "Floppy disk", fdc_enabled, execute_fdc);
  add_menu_entry('Q', "Quick disk", qd_enabled, execute_qd);
  add_menu_entry('C', "Cassette tape", NULL, execute_tape);
  add_menu_entry('M', "Monitor", NULL, execute_monitor);

  get_config("menu", &menu_entries, config);

  for (int i = 0; i < menu_entries; i++) {
    if (strncmp(config[i].key, "key_", 4) != 0)
      continue;

    char menu_key = toupper(config[i].key[4]);

    char *value = config[i].value;
    char *sep = strchr(value, '|');
    if (!sep)
      continue;

    size_t desc_len = sep - value;
    char menu_desc[MAX_MENU_DESCRIPTION_LENGTH];
    strncpy(menu_desc, value, desc_len);
    menu_desc[desc_len] = '\0';

    char *action = sep + 1;
    add_menu_entry(menu_key, menu_desc, NULL, exec_with_mount_factory(action));
  }
}

void display_menu(void) {
  uint8_t col = 12;

  //beep();
  clrscr();
  border(0);
  init_menu();

  put_str_xy(col - 1, 2, "Get ready for MZPico");
  put_str_xy(col, 4, "Please push key");

  int y = 6;
  for (int i = 0; menu[i].key != 0x00; i++) {
    if (menu[i].enabled == NULL || menu[i].enabled()) {
      char line[40];
      snprintf(line, sizeof(line), "%c:%s", menu[i].key, menu[i].description);
      put_str_xy(col, y++, line);
    }
  }
}

void execute_action_loop(void) {
  char c;

  while (1) {
    if ((c = inkey())) {
      for (int i = 0; menu[i].key != 0x00; i++) {
        if (menu[i].key == c) {
          if (menu[i].enabled == NULL || menu[i].enabled()) {
            if (menu[i].execute) {
              menu[i].execute();
              display_menu();
            }
          }
          break;
        }
      }
    }
  }
}

void main(void) {
  display_menu();
  execute_action_loop();
}
