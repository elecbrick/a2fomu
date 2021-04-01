//
// bios.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <generated/mem.h>
#include <generated/csr.h>
#include <irq.h>
#include <rgb.h>
#include <spi.h>

// ICE40UP5K bitstream images (with SB_MULTIBOOT header) are
// 104250 bytes.  The SPI flash has 4096-byte erase blocks.
// The smallest divisible boundary is 4096*26.
#define FBM_OFFSET ((void *)(SPIFLASH_BASE + 0x1a000))

void bios_isr(void)
{
  // No interrupts are enabled so this will not be called. However, it must
  // be defined as the standard crt0.o requires a routine by this name.
  //unsigned int irqs;
  //irqs = irq_pending() & irq_getmask();
}

void load_runtime(uint32_t *src) {
  int32_t len;
  uint32_t magic;
  uint32_t sum;
  uint32_t *dst;
  void (*runtime)(void);
  runtime = 0;

  // Disable ISR as that touches global variables
  irq_setmask(0xffffffff);
  irq_setie(0);
  rgb_set(0x00ff00);
  while (1) {
    magic = *src++;
    // magic numbers: Apple2fomu 6502 and Application Binary Executable Object
    if ((magic == 0xa2f06502) || (magic == 0xa2f0abe0)) {
      dst = (uint32_t*)*src++;
      len = *src++;
      sum = *src++;
      // reboot to image if in main RAM and correct magic number
      if ((magic == 0xa2f0abe0) && ((char*)dst >= (char*)SRAM_BASE) &&
          ((((char*)dst)+len) < (char*)(SRAM_BASE+SRAM_SIZE))) {
        runtime = (void(*)(void))dst;
      }
      // s0: x8  src
      // a0: x10 runtime
      // a2: x12 intermediate value being copied and added
      // a3: x13 sum
      // a4: x14 dst+len
      // a5: x15 dst
      for ( ; len>0; len-=4) {
        sum += (*dst++ = *src++);
      }
      if (sum != 0) {
        rgb_set(0xff0000);
        if (runtime != 0) {
          // cancel reboot due to invalid checksum
          runtime = 0;
        }
      }
    } else {
      break;
    }
  }
  if (runtime) {
    rgb_set(0x0000ff);          // Pulse bright blue
    (*runtime)();
  }
  rgb_set(0xff0000);            // Pulse bright red
}

int bios_main(void) {
  rgb_init(LED_FADE);
  spiInit();
  spiFree();

#ifdef SIMULATION
  extern uint32_t _etext;
  load_runtime(&_etext);
#else
  // foboot and dfu-util write FPGA image at offset 0x40000 in flash.
  // Add bitstream length to this to get the starting offset of software
  // images that are to be copied into RAM.
  // Check location accoring to priority. Newer images take precidence.
  // Start by guessing Fomu EVT multi-boot enabled image is present.
  load_runtime((uint32_t*)(SPIFLASH_BASE +    160 + 104090 + 2)); // 2005969c
  // No, so check if Fomu PVT image loaded by DFU into slot 2.
  load_runtime((uint32_t*)(SPIFLASH_BASE + 262144 + 104090 + 2)); // 2005969c
#endif
  return 0;
}
