#ifndef __EXPLORER_H_
#define __EXPLORER_H_

#define MAX_SEARCH 16
#define MAX_ENTRIES MAX_DIR_ENTRIES
#define MAX_DEVICES 3

void explorer_init(void);
void explorer_handle_key(uint8_t c);
void explorer_poll(void);
void draw_footer(uint8_t shifted);

#endif
