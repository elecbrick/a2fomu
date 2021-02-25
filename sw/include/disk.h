//
// disk.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _DISK_H_
#define _DISK_H_

// Structures and constants for Disk II controller and drive emulation.

#define SECTOR_SIZE 256
#define TRACK_SIZE (16*SECTOR_SIZE)
#define DISK_SIZE (35*TRACK_SIZE)
#define DISK_CACHE_LINES disk_max

// Minor devices of the a2dev_usb major device category
enum a2_disk {
  disk_external = 0,
  disk_internal,
  disk_max
};

// Disk Drive Status
struct drive {
  int8_t track2x;       // Arm motor turns twice per track
  int8_t phase;         // Arm motor phase (four of them)
  int8_t motor;         // Disk is spinning
  int8_t wanted;        // DOS is actively reading data from drive
  int8_t volume;        // Disk Volume that is currently in drive
};

// Disk State Machine
enum disk_state {
  ext_disconnected = 0,
  ext_no_disk,
  ext_inserted,
  ext_seeking,
  ext_reading,
  ext_writing,
};

// Disk Cache
// Current implementation is one line of cache per drive.  Line is invalidated
// on seek. Sectors are marked valid one by one as they are read into cache.
struct track_cache {
  uint8_t volume;
  uint8_t track;
  uint16_t sector_valid;
};

struct partial_sector {
  uint8_t *sector_start;
  uint16_t current_byte;
  uint8_t current_sector;
  uint8_t half_byte;
};

enum disk_diag_flags {
  disk_diag_usb,
  disk_diag_controller,
  disk_diag_track_change,
};

extern int disk_diagnostics;

extern enum disk_state external_disk_state;
extern struct drive disk_drive[disk_max];
extern struct partial_sector partial_sector;
extern struct track_cache cache_index[DISK_CACHE_LINES];
extern uint8_t track_cache[DISK_CACHE_LINES][TRACK_SIZE];

// External entry point for task manager
void disk_task(void);
void disk_init(void);

#endif /* _DISK_H_ */
