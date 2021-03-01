//
// foboot.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// TODO Incorporate this file into foboot itself.
// Permission explicitly granted to incorporate this into any foboot based
// project using the same license as foboot, especially foboot itself. 

#ifndef _FOBOOT_H_
#define _FOBOOT_H_

// Magic numbers used by Foboot bootloader.

#define BOOTFLAGS_MAGIC   0xb469075a
#define FOBOOT_MAIN_MAGIC 0x032bd37d
#define BOOSTER_MAGIC     0xfaa999b1
#define RAMBOOT_MAGIC     0x17ab0f23
#define AUTOBOOT_MAGIC    0x4a6de3ac
#define BITSTREAM_1       0x6e99aa7f
#define BITSTREAM_2       0x7eaa997e

// The following options exist in the boot flags bitfield:
#define QPI_EN       0x01   // Enable QPI mode on the SPI flash
#define DDR_EN       0x02   // Enable DDR mode on the SPI flash
#define CFM_EN       0x04   // Enable CFM mode on the SPI flash
#define FLASH_UNLOCK 0x08   // Don't lock flash prior to running program
#define FLUSH_CACHE  0x10   // Issue "fence.i" prior to running user program
#define NO_USB_RESET 0x20   // Don't reset the USB prior to handoff

#endif /* _FOBOOT_H_ */
