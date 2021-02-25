//
// flash.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _FLASH_H_
#define _FLASH_H_

// Routines for reading and writing flash memory. Flash cannot be read while a
// write is in progress due to the limitations of the LiteX flash controller.
// Given this, a check must be perfomed to ensure no write is in progress
// before attempting to read and likewise, a check must be performed by a
// writer to ensure no process is reading before switching the flash to write
// mode.

// Also here are constants containing the flash memory usage and reserved
// locations.

#include <generated/mem.h>

#define ERASE_SECTOR_SIZE 4096
#define PROGRAM_PAGE_SIZE 256
#define FATFS_SECTOR_SIZE 512

// Flash drive currently at location beyond where Foboot DFU places images
//#define FIRST_SAFE_ADDRESS 0x5a000
#define FIRST_SAFE_ADDRESS 0x80000

#define FLASHFS_START_ADDRESS FIRST_SAFE_ADDRESS
#define FLASHFS_SECTOR_SIZE   ERASE_SECTOR_SIZE
#define FLASHFS_NUM_SECTORS  ((SPIFLASH_SIZE-FLASHFS_START_ADDRESS)/FLASHFS_SECTOR_SIZE)

#define FOBOOT_MAIN_LOADER 0x1A000
#define INT_BASIC_ROM_AREA 0x1B000
#define DOS33_PRELOAD_AREA 0x39000
#define APPLESOFT_ROM_AREA 0x3D000

enum flash_state {
  FLASH_USER_MODE = 0,
  FLASH_ERASE_TRACK,
  FLASH_WRITE_SECTOR,
  FLASH_VERIFY_TRACK,
};

enum spi_flash_mode {
  FLASH_MEMORY_MAPPED = 0,
  FLASH_WRITE_ENABLED = 1,
};

int write_flash(int dst, const void *src, int size);
int read_flash(void *dst, int src, int size);
int flash_busy(void);
void flash_task(void);
void flash_init(void);

// WARNING: bypass safety checks to write over configuration data
int write_flash_unsafe(int dst, const void *src, int size);

#endif /* _FLASH_H_ */
