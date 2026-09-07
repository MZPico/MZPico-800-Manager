#ifndef __PICO_COMM_H__
#define __PICO_COMM_H__

#include <stdint.h>

// Unicard-compatible repository (MZPico `unicard` device): command/status
// port and data port. Protocol: mz800emu unimgr.c / unimgr_commands.h and
// docs/unicard-migration-plan.md in the firmware repo.
#define UC_CMD_PORT  0x50
#define UC_DATA_PORT 0x51

#define cmdRESET     0x00
#define cmdASCII     0x01
#define cmdSHASCII   0x02
#define cmdSTSR      0x03
#define cmdSTORNO    0x04
#define cmdREV       0x05
#define cmdREVD      0x06
#define cmdFDDMOUNT  0x10
#define cmdGETFREE   0x20
#define cmdCHDIR     0x21
#define cmdGETCWD    0x22
#define cmdSTAT      0x30
#define cmdREADDIR   0x41
#define cmdNEXT      0x43
#define cmdOPEN      0x50
#define cmdSEEK      0x51
#define cmdCLOSE     0x54
#define cmdSIZE      0x56
// MZPico extensions
#define cmdX_LISTVOL    0x90
#define cmdX_GETCONFIG  0x92
#define cmdX_WIFISTATUS 0x93
#define cmdX_INFO       0x95
#define cmdX_SETSORT    0x96
#define cmdX_SERVEDSUM  0x97

#define UC_FA_READ 0x01

// Status byte 0 bits
#define UC_ST_BUSY     0x01
#define UC_ST_OUTPUT   0x02
#define UC_ST_STREAM   0x04
#define UC_ST_READFILE 0x08
#define UC_ST_INPROG   0x40   // MZPico: command running on core 0 (cloud) - poll
#define UC_ST_ERROR    0x80

// FDDMOUNT device ids (uc3 numbering)
#define UC_DEV_FD1 0
#define UC_DEV_QD  5

#define ERROR_DESCRIPTION_LN 32
#define FILENAME_LN 32
#define DEVICE_LN 8
// Listing cap shared by list_dir() and the explorer's entries[]: 37 bytes each,
// and the explorer must keep a few KB between its BSS and the stack at 0xD000
#define MAX_DIR_ENTRIES 800

#define MAX_CONFIG_KEY_LENGTH 16
#define MAX_CONFIG_VALUE_LENGTH 64

#define WIFI_STATUS_INIT         0
#define WIFI_STATUS_STARTING     1
#define WIFI_STATUS_CONNECTING   2
#define WIFI_STATUS_CONNECTED    3
#define WIFI_STATUS_DISCONNECTED 4
#define WIFI_STATUS_ERROR        5
#define WIFI_STATUS_NOT_SUPPORTED 6

typedef struct {
  uint8_t isDir;
  char filename[FILENAME_LN];
  uint32_t size;
} DIR_ENTRY;

typedef struct {
  char name[DEVICE_LN];
} DEV_ENTRY;

typedef struct {
  char key[MAX_CONFIG_KEY_LENGTH];
  char value[MAX_CONFIG_VALUE_LENGTH];
} ConfigEntry;
#define CONFIG_ENTRY_SIZE (MAX_CONFIG_KEY_LENGTH + MAX_CONFIG_VALUE_LENGTH)

extern char error_description[ERROR_DESCRIPTION_LN];

// Low-level transport (also usable by other Z80 programs)
void uc_cmd(uint8_t command);
void uc_wr(uint8_t data);
uint8_t uc_rd(void);
void uc_status4(uint8_t *status);          // STSR, then the 4 status bytes
void uc_read(uint8_t *dst, uint16_t n);    // n data-port bytes via INIR
void uc_wstr(const char *s);               // string parameter, 0x0D terminated

uint8_t list_dir(const char *path, uint16_t *entries_cnt, DIR_ENTRY *entries);
uint8_t list_dev(uint16_t *entries_cnt, DEV_ENTRY *entries);
uint8_t get_config(const char *section, uint16_t *entries_cnt, ConfigEntry *entries);
uint8_t get_wifi_status(void);
uint8_t read_file_head(const char *path, uint8_t *buf, uint8_t n); // first n bytes of a file
uint8_t mount_entry(const char *path);
void read_and_execute(void);
void execute_floppy(void);
void execute_quickdisk(void);

#endif
