//
// fmbs.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// Foboot Main, Second stage loader - invoked by failsafe bootloader to install
// and hand execution over to A2Fomu gateware and its third stage loader.
//
// This program is designed to work with the CSR registers of Foboot 1.3 and
// A2Fomu 1.0. These use different versions of LiteX which have different sizes
// for the CSR registers and thus, they are also have different definitions.
//
// 20000000    7eaa997e 92000044 030000a0 82000001    ~..~...D........
// 20000010    08000000 00000000 00000000 00000000    ................
// 20000020    7eaa997e 92000044 030000a0 82000001    ~..~...D........
// 20000030    08000000 00000000 00000000 00000000    ................
// 20000040    7eaa997e 92000044 03026800 82000001    ~..~...D..h.....
// 20000050    08000000 00000000 00000000 00000000    ................
// 20000060    7eaa997e 92000044 03040000 82000001    ~..~...D........
// 20000070    08000000 00000000 00000000 00000000    ................
// 20000080    7eaa997e 92000044 03048000 82000001    ~..~...D........
// 20000090    08000000 00000000 00000000 00000000    ................
//
// The Fomu official boot offsets are 030000a0, 03026800, 03040000, 03048000
// and are found in locations 20000028, 20000048, 20000068, 20000088
// respectiveley. Location 20000008 contains the default image loaded when
// power is applied which is the same as slot 0, the Foboot failsafe image.
// Unfortunately, those addresses are bit-endian so byte swapping is necessary
// to get a pointer.
//
// This code was written specifically to transfer control from the autoboot
// image to the actual platform. As such, entries 0 and 1 will be ignored and
// only images 2, 3, 4 will be checked for a valid bitstream.
//
// A valid ice40 bitstream starts with the magic number ff0000ff which will
// be looked for at the three locations above.

#include <foboot.h>
#include <generated/mem.h>
#include <generated/csr.h>
#include <rgb.h>

#define RAW_BLACK   0
#define RAW_RED     1
#define RAW_GREEN   2
#define RAW_BLUE    4
#define RAW_YELLOW  3
#define RAW_MAGENTA 5
#define RAW_CYAN    6
#define RAW_WHITE   7

#define REGISTER_WIDTH_TEST_VALUE 0x12345678
#define ICE40_MAGIC 0xFF0000FF
#define IMAGE0_LOCATION (SPIFLASH_BASE+0x28)
#define FBM_MAGIC_MARKER 0x032bd37d

#define read_reg(addr) (*(volatile uint32_t*)addr)

#define write_reg(addr, val) (*(volatile uint32_t*)addr = val)

uint32_t htonl(uint32_t big) {
  return (big>>24)|((big&0x00FF0000)>>8)|((big<<8)&0x00FF0000)|(big<<24);
}

void fbms_isr(void) {
}

int fbms_main(void) {
  int image_index;
  // Start off by indicating successful handoff to fbms
  write_reg(CSR_RGB_CTRL_ADDR, 63);
  write_reg(CSR_RGB_RAW_ADDR, RGB_RAW_MAGENTA);
  // Determine which version of LiteX was used to generate this Soc.
  // CONFIG_CSR_DATA_WIDTH is 8 in the failsafe gateware but 32 in A2Fomu.
  // Helper functions compile with the configured width so cannot be used.
  // Write a 32-bit value to the 32-bit r/w reboot address register.
  write_reg(CSR_REBOOT_ADDR_ADDR, REGISTER_WIDTH_TEST_VALUE);
  // And read back either an 8-bit or 32-bit value.
  int value = read_reg(CSR_REBOOT_ADDR_ADDR);
  if(value==(REGISTER_WIDTH_TEST_VALUE&0xFF)) {
    // Failsafe gateware detected
    write_reg(CSR_RGB_RAW_ADDR, RGB_RAW_YELLOW);
  } else if(value==REGISTER_WIDTH_TEST_VALUE) {
    // Target gateware detected
    write_reg(CSR_RGB_RAW_ADDR, RGB_RAW_CYAN);
  }
  // Find the first valid gateware after the failsafe one and load it.
  for(image_index=1; image_index<4; image_index++) {
    uint32_t addr = htonl(*(uint32_t*)(IMAGE0_LOCATION+(image_index*32)));
    if(*(uint32_t*)(SPIFLASH_BASE|(addr&(SPIFLASH_SIZE-1)&~3))==ICE40_MAGIC) {
      break;
    }
  }
  // The key 0x2B must be in the 6 upper bits of this 8 bit register in order
  // for the reboot to happen. If the above loop does not find a valid image,
  // the key will contain 0x2C so the image will halt at the following loop.<F3>
  write_reg(CSR_REBOOT_CTRL_ADDR, 0xac+image_index);
  // This routine was called from foboot so could in theory return and restore
  // USB communication if handoff was not successful.
  while(1)
    ;
  __builtin_unreachable();
}
