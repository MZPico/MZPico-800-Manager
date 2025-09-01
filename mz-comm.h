#ifndef __PICO_COMM_H__
#define __PICO_COMM_H__

#include <stdint.h>

#define COMMAND_PORT 0x40
#define DATA_PORT 0x41
#define RESET_PORT 0x44

#define REPO_CMD_LIST_DIR   0x01
#define REPO_CMD_MOUNT      0x03
#define REPO_CMD_UPLOAD     0x04
#define REPO_CMD_LIST_DEV   0x05
#define REPO_CMD_CHDEV      0x06
#define REPO_CMD_LIST_WF    0x07
#define REPO_CMD_CONN_WF    0x08
#define REPO_CMD_LIST_REPOS 0x09
#define REPO_CMD_CHREPO     0x0a

#define COMMAND_RESULT_NONE 0x00
#define COMMAND_RESULT_ACCEPTED 0x01
#define COMMAND_RESULT_IN_PROGRESS 0x02
#define COMMAND_RESULT_OK 0x03
#define COMMAND_RESULT_ERR 0x04

#define ERROR_DESCRIPTION_LN 32
#define FILENAME_LN 32
#define DEVICE_LN 8

typedef struct {
  uint16_t ln;
  void *data;
} comm_params_t;



typedef struct {
  uint8_t isDir;
  char filename[FILENAME_LN];
  uint32_t size;
} DIR_ENTRY;

typedef struct {
  char name[DEVICE_LN];
} DEV_ENTRY;

extern char error_description[ERROR_DESCRIPTION_LN];

uint8_t execute_command(uint8_t command, comm_params_t *in_params, comm_params_t *out_params);
uint8_t list_dir(char *path, uint16_t *entries_cnt, DIR_ENTRY *entries);
uint8_t list_dev(uint16_t *entries_cnt, DEV_ENTRY *entries);
uint8_t mount_entry(char *path);
void read_and_execute(void);
void execute_floppy(void);
void execute_quickdisk(void);

#endif
