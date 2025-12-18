#include <stdio.h>
#include <string.h>
#include "mz-comm.h"

#define MZF_HEADER_START 0x10f0
#define MZF_BODY_TARGET 0x1200
#define MZF_SIZE MZF_HEADER_START + 0x12
#define LOADER_TARGET 0x1000

char error_description[ERROR_DESCRIPTION_LN];

void _exec_command(uint8_t command) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ld a, (iy+0)
    out (COMMAND_PORT), a

    pop iy
    ret
  __endasm;
}

uint8_t _get_command_status(void) {
  __asm
    in a, (COMMAND_PORT)
    ld h, 0
    ld l, a
  __endasm;
}

void _feed_params(uint16_t ln, void *data) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ld c, 0x41       ; Port to write to
    ld l, (iy+0)          ; Load data pointer (low)
    ld h, (iy+1)          ; Load data pointer (high)
    ld e, (iy+2)          ; Load length (low byte)
    ld d, (iy+3)          ; Load length (high byte)

    out (0x44), a
    ld a, e
    out (0x41), a
    ld a, d
    out (0x41), a

    ; Send full 256-byte blocks via OTIR
_fp_send_blocks:
    ld b, 0               ; B = 0 means OTIR sends 256 bytes
    ld a, d
    or a
    jr z, _fp_send_remaining  ; If high byte is 0, skip to remaining
    otir
    dec d
    jr _fp_send_blocks

_fp_send_remaining:
    ld b, e               ; B = original low byte (remaining bytes)
    or b
    jr z, _fp_done            ; If no remaining, we're done
    otir

_fp_done:
    pop iy
    ret
  __endasm;
}

void _get_params(uint16_t *ln, void *data) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    out (0x44), a

    ld c, 0x41       ; Port to read from
    ld l, (iy+2)          ; Load ln pointer (low)
    ld h, (iy+3)          ; Load ln pointer (high)

    in a, (c)
    ld e, a
    ld (hl), a
    inc hl
    in a, (c)
    ld d, a
    ld (hl), a

    ld l, (iy+0)          ; Load data pointer (low)
    ld h, (iy+1)          ; Load data pointer (high)

    ; read full 256-byte blocks via INIR
_gp_read_blocks:
    ld b, 0               ; B = 0 means OTIR sends 256 bytes
    ld a, d
    or a
    jr z, _gp_read_remaining  ; If high byte is 0, skip to remaining
    inir
    dec d
    jr _gp_read_blocks

_gp_read_remaining:
    ld b, e               ; B = original low byte (remaining bytes)
    or b
    jr z, _gp_done            ; If no remaining, we're done
    inir

_gp_done:
    pop iy
    ret
  __endasm;
}

uint8_t execute_command(uint8_t command, comm_params_t *in_params, comm_params_t *out_params) {
  uint16_t err_ln;
  if (in_params) {
    _feed_params(in_params->ln, in_params->data);
  }
  _exec_command(command);
  while (_get_command_status() == COMMAND_RESULT_ACCEPTED);
  while (_get_command_status() == COMMAND_RESULT_IN_PROGRESS);
  uint8_t result = _get_command_status();
  if (result == COMMAND_RESULT_ERR) {
    _get_params(&err_ln, error_description);
    return result;
  }
  if (out_params)
    _get_params(&out_params->ln, out_params->data);
  return 0;
}

uint8_t list_dir(const char *path, uint16_t *entries_cnt, DIR_ENTRY *entries) {
  uint8_t ret;
  comm_params_t input;
  comm_params_t output;

  input.data = path;
  input.ln = strlen(path)+1;
  output.data = entries;
  ret = execute_command(REPO_CMD_LIST_DIR, &input, &output);
  if (ret)
    return ret;
  *entries_cnt = output.ln / sizeof(DIR_ENTRY);
  return 0;
}

uint8_t list_dev(uint16_t *entries_cnt, DEV_ENTRY *entries) {
  uint8_t ret;
  comm_params_t output;

  output.data = entries;
  ret = execute_command(REPO_CMD_LIST_DEV, NULL, &output);
  if (ret)
    return ret;
  *entries_cnt = output.ln / sizeof(DEV_ENTRY);
  return 0;
}

uint8_t get_config(const char *section, uint16_t *entries_cnt, ConfigEntry *entries) {
  uint8_t ret;
  comm_params_t input;
  comm_params_t output;

  input.data = section;
  input.ln = strlen(section)+1;
  output.data = entries;
  ret = execute_command(REPO_CMD_GET_CONFIG, &input, &output);
  if (ret)
    return ret;
  *entries_cnt = output.ln / sizeof(ConfigEntry);
  return 0;
}

uint8_t get_wifi_status(void) {
  uint8_t ret;
  comm_params_t output;
  uint8_t status;

  output.data = &status;
  ret = execute_command(REPO_CMD_GET_WIFI_STATUS, NULL, &output);
  if (ret)
    return 0xFF;
  return status;
}

uint8_t mount_entry(const char *path) {
  uint8_t ret;

  comm_params_t input;

  input.data = path;
  input.ln = strlen(path)+1;
  ret = execute_command(REPO_CMD_MOUNT, &input, NULL);
  if (ret)
    return ret;
  return 0;
}

void read_and_execute(void) __naked {
  __asm
    ld sp, MZF_HEADER_START

    ld hl, _read_and_execute_start
    ld de, LOADER_TARGET
    ld bc, _read_and_execute_end - _read_and_execute_start
    ldir
    in a, (0x41)
    in a, (0x41)         ; skip 2 inital bytes - size
    jp LOADER_TARGET

_read_and_execute_start:
    ld b, 128
    ld c, 0x41
    ld hl, MZF_HEADER_START
    inir

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
    exx
    ld bc,0x0600
    exx
    ld hl, MZF_SIZE
    ld sp, MZF_HEADER_START
    jp 0xecfc     ; relocate and execute
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
