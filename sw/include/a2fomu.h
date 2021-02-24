//╔═══════════════════════════════════════════════════════════════════════════╗
//║ a2fomu.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton            ║
//║                                                                           ║
//║ This file is part of a2fomu which is released under the two clause BSD    ║
//║ licence.  See file LICENSE in the project root directory or visit the     ║
//║ project at https://github.com/elecbrick/a2fomu for full license details.  ║
//╚═══════════════════════════════════════════════════════════════════════════╝

#include <stdio.h>
#include <stdio.h>
#include <rtc.h>

// OS Task List - TODO abstract via API so CLI can access without exposing
enum task_num {
  tud_task_active = 0,
  tty_task_active,
  led_task_active,
  touch_task_active,
  cli_task_active,
  keyboard_task_active,
  video_task_active,
  disk_task_active,
  max_task
};
extern a2time_t task_runtime[max_task];

// Major Device Types used by stdio. Occupies one byte in FILE structure.
// Since there are so few devices, three of the bits serve as flags indicating
// the capabilities of the device.
// Bit 7: 0:Character, 1: Block device
// Bit 6: 1:Write possible, 0:Read-only device
// Bit 5: 1:Read possible, 0:Write-only device
typedef enum __attribute__((packed)) {
  a2dev_none  = 0,      // Indicates no active device (unused)
  a2dev_touch = 0x21,   // Read only character
  a2dev_led   = 0x42,   // Write only character
  a2dev_usb   = 0x63,   // Read/Write character
  a2dev_flash = 0xE4,   // Read/Write block
} a2dev_t;

// Minor devices of the a2dev_usb category
enum cdc_channel {
  cdc_tty = 0,          // /dev/ttyACM0
  cdc_disk,             // /dev/ttyACM1
};

enum application_error {
  tty_input_overflow,
  disk_input_overflow,
  video_output_overflow,
  usb_interrupt_lost,
  max_application_error
};

enum scroll_mode {
  scroll_standard = 0,
  scroll_enhanced = 1,
};

extern int debug_counter[max_application_error];
extern enum scroll_mode scroll_mode;

extern FILE *persistence;
void persistence_init(void);
void dump_persistence(void);

// Operating system call to process all other tasks and reset watchdog timer
void yield(void);
// As above but does not reset watchdog timer
void run_task_list(void);
