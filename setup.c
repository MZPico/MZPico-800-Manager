#include "console.h"
#include "mz-comm.h"


void sharp_info(void) {
  uint8_t i = 0;
  uint8_t c = 20;
  put_str_xy(c, i++, "Sharp MZ-800 info");
  put_str_xy(c, i++, "=================");
  put_str_xy(c, i++, "VRAM:");
  put_str_xy(c, i++, "FD:");
  put_str_xy(c, i++, "QD:");
  put_str_xy(c, i++, "RAM disk:");
  put_str_xy(c ,i++, "MemExt:");
}

void mzpico_info(void) {
  uint8_t i = 0;
  uint8_t c = 0;
  put_str_xy(c, i++, "MZPico card setup");
  put_str_xy(c, i++, "================");
  put_str_xy(c, i++, "Card type:");
  put_str_xy(c, i++, "FW version:");
  put_str_xy(c, i++, "Flash memory:");
  put_str_xy(c, i++, "Flash space:");
  put_str_xy(c, i++, "Flash avail:");
  put_str_xy(c, i++, "MicroSD card:");
  put_str_xy(c, i++, "MicroSD avail:");
  put_str_xy(c, i++, "Manager port:");
  put_str_xy(c, i++, "MZPicoRD active:");
  put_str_xy(c, i++, "MZPicoRD mode:");
  put_str_xy(c, i++, "MZPicoRD base port:");
  put_str_xy(c, i++, "MZPicoRD file:");
  put_str_xy(c, i++, "FDEmu active:");
  put_str_xy(c, i++, "FDEmu base port:");
  put_str_xy(c, i++, "FDEmu0 file:");
  put_str_xy(c, i++, "FDEmu1 file:");
  put_str_xy(c, i++, "FDEmu2 file:");
  put_str_xy(c, i++, "FDEmu3 file:");
  put_str_xy(c, i++, "QDEmu:");
  put_str_xy(c, i++, "QDEmu port:");
  put_str_xy(c, i++, "QDEmu file:");

}

void setup_main(void) {
  char c;

  clrscr();
  mzpico_info();
  sharp_info();
  while (!inkey() && inkey()!=5);
} 
