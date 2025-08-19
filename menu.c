#include <string.h>
#include <stdio.h>

#include "mz-comm.h"
#include "console.h"

#define MAX_MENU_ITEMS 18
#define MAX_MENU_DESCRIPTION_LENGTH 25

typedef struct {
  char key;
  char description[MAX_MENU_DESCRIPTION_LENGTH];
  uint8_t (*enabled)(void);
  uint8_t (*execute)(void);
} menu_entry_t;

menu_entry_t menu[MAX_MENU_ITEMS];


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

uint8_t execute_basic(void) {
  mount_entry("#basic");
  read_and_execute(); 
}

uint8_t execute_setup(void) {
  setup_main();
}

uint8_t execute_explorer(void) {
  manager_main();
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
  reset_menu();
  add_menu_entry('F', "Floppy disk", fdc_enabled, execute_fdc);
  add_menu_entry('Q', "Quick disk", qd_enabled, execute_qd);
  add_menu_entry('C', "Cassette tape", NULL, execute_tape);
  add_menu_entry('M', "Monitor", NULL, execute_monitor);
  add_menu_entry('B', "Basic", NULL, execute_basic);
  add_menu_entry('E', "Explorer", NULL, execute_explorer);
}

void display_menu(void) {
  uint8_t col = 12;

  beep();
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
          break;  // stop checking once a match is found
        }
      }
    }
  }
}

void main(void) {
  if (inkey() == 5)
    execute_setup();
  display_menu();
  execute_action_loop();
}
