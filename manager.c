#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <input.h>
#include <string.h>

#include "explorer.h"
#include "console.h"
#include "mz-comm.h"


void init_screen(void) {
  clrscr();
  border(0x09);
}

void quit(void) {
  border(0x00);
  __asm
    ld de, amsgx
    jp 0xea34
amsgx:
    DEFB "WELCOME TO IPL", 0x0d
  __endasm;
}

void draw_main_frame(void) {
  uint8_t i;

  put_str_attr_xy(17, 0,  "MZPico", 0x70);
  put_char_attr_xy(16, 0, 0xfe, 0x01);
  put_char_attr_xy(23, 0, 0xfd, 0x01);
  put_str_attr_xy(0, 24, "    Nav    Srch   Exe   Inf   Stp   Quit", 0x70);
  put_str_attr_xy(0, 24, "\xc1\xc2\xc3\xc4", 0x60);
  put_str_attr_xy(8, 24, "A-Z", 0x06);
  put_str_attr_xy(16, 24, "CR", 0x06);
  put_str_attr_xy(22, 24, "F1", 0x06);
  put_str_attr_xy(28, 24, "F2", 0x06);
  put_str_attr_xy(34, 24, "F5", 0x06);
  for (i=2; i<24; i++) {
    put_char_attr_xy(0, i, ' ', 0x05);
    put_char_attr_xy(39, i, ' ', 0x05);
  }
  for (i=0; i<40; i++) {
    put_char_attr_xy(i, 23, ' ', 0x05);
  }
  put_char_attr_xy(0, 1, 0xfe, 0x51);
  put_char_attr_xy(39, 1, 0xfd, 0x51);
  put_char_attr_xy(0, 23, 0xfd, 0x15);
  put_char_attr_xy(39, 23, 0xfe, 0x15);
}


main() {
  uint16_t i;
  unsigned char val;
  uint8_t c;

  init_screen();
  draw_main_frame();
  explorer_init();

  while (1) {
    if (c=inkey()) {
      explorer_handle_key(c);
      switch (c) {
        case 5:
          quit();
          break;
      }
    }
  }
}
