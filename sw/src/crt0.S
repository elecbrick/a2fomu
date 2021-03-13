//
// crt0.S - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

//#include "rgb.S"
#include <foboot.h>

#ifndef CRT0_PREFIX
#define CRT0_PREFIX
#endif

// Pick a color that identifies stage in case of hang
#define RGB_RAW_BLACK   (0)
#define RGB_RAW_RED     (1)
#define RGB_RAW_GREEN   (2)
#define RGB_RAW_YELLOW  (3)
#define RGB_RAW_BLUE    (4)
#define RGB_RAW_MAGENTA (5)
#define RGB_RAW_CYAN    (6)
#define RGB_RAW_WHITE   (7)

fbms_load_color = RGB_RAW_GREEN
bios_load_color = RGB_RAW_MAGENTA
load_color = RGB_RAW_CYAN
main_color = RGB_RAW_YELLOW

#define PASTER(x,y) x ## y
#define EVALUATOR(x,y)  PASTER(x,y)
#define NAME(fun) EVALUATOR(fun, VARIABLE)
#define PREFIX(x) EVALUATOR(CRT0_PREFIX, x)

.global PREFIX(main)
.global PREFIX(isr)

.section .text.start
.global _start

_start:
  j crt_init

/* Allow any valid combination of the three different types of constants that
 * Foboot recognises:
 *
 *  1. Start Address may be set by compiling with one of three options:
 *       -DLOAD_BOOSTER             Program must be at address 0x2005a000
 *       -DLOAD_FOBOOT_MAIN         Program must be at address 0x2001a000
 *       -DLOAD_RAM_ADDR=0x10000100 Program loads into RAM at the given address
 *                                  ***Only recognized while loading with DFU***
 *
 *  2. Boot Timeout (Foboot newer than October 2020, not available pre-built)
 *       -DBOOT_TIMEOUT=9           Foboot starts in DFU mode for 9 seconds
 *
 *  3. Boot configuration flags including SPI mode and keeping USB alive
 *       -DLOAD_BOOT_CONFIG=NO_USB_RESET  Pass a combination of settings
 */
#ifdef BOOT_TIMEOUT
  .word AUTOBOOT_MAGIC          // Must be bytes 4-7
  .word BOOT_TIMEOUT
#elif defined LOAD_BOOSTER
  .word BOOSTER_MAGIC           // Must be bytes 4-7
  nop
#elif defined LOAD_FOBOOT_MAIN
  .word FOBOOT_MAIN_MAGIC       // Can be in first 256 bytes
  nop
#else
  nop                           // Always use 8 bytes no matter which path taken
  nop
#endif
  nop                           // Pad to start next group at byte 16
#ifdef LOAD_BOOT_CONFIG
  .word BOOTFLAGS_MAGIC         // Can be in first 128 bytes
  .word LOAD_BOOT_CONFIG
#else
  nop                           // Always use 8 bytes no matter which path taken
  nop
#endif
#ifdef LOAD_RAM_ADDR
  .word RAMBOOT_MAGIC           // Can be in first 64 bytes
  .word LOAD_RAM_ADDR
#else
  nop                           // Always use 8 bytes no matter which path taken
  nop
#endif

// Program cache line is 32 bytes. The above will only ever be run once so
// should never be allowed to enter the cache after that point. Waste 16 bytes
// to ensure that is the case. Delete these if the program runs out of space.
// This also aligns the trap handler to the start of a cache line.
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop

.section .text
.global  trap_entry
// External interrupt and machine exception handler. Save all registers caller
// is responsible for on the stack and call C isr() interrupt service routine.
// See "Table 18.2: RISC-V calling convention register usage" for details.
trap_entry:
  sw   x1,   -1*4(sp)   // Save registers Caller is responsible for.
  sw   x5,   -2*4(sp)
  sw   x6,   -3*4(sp)
  sw   x7,   -4*4(sp)
  sw   x10,  -5*4(sp)
  sw   x11,  -6*4(sp)
  sw   x12,  -7*4(sp)
  sw   x13,  -8*4(sp)
  sw   x14,  -9*4(sp)
  sw   x15, -10*4(sp)
  sw   x16, -11*4(sp)
  sw   x17, -12*4(sp)
  sw   x28, -13*4(sp)
  sw   x29, -14*4(sp)
  sw   x30, -15*4(sp)
  sw   x31, -16*4(sp)
  addi sp, sp, -16*4    // Link stack frame
  call PREFIX(isr)      // Call C routine
  lw   x1 , 15*4(sp)    // Restore registers
  lw   x5,  14*4(sp)
  lw   x6,  13*4(sp)
  lw   x7,  12*4(sp)
  lw   x10, 11*4(sp)
  lw   x11, 10*4(sp)
  lw   x12,  9*4(sp)
  lw   x13,  8*4(sp)
  lw   x14,  7*4(sp)
  lw   x15,  6*4(sp)
  lw   x16,  5*4(sp)
  lw   x17,  4*4(sp)
  lw   x28,  3*4(sp)
  lw   x29,  2*4(sp)
  lw   x30,  1*4(sp)
  lw   x31,  0*4(sp)
  addi sp, sp, 16*4     // Remove frame from stack
  mret                  // Return to user mode.

.text
crt_init:
  // Indicate successful hand-off from Foboot to user program by setting the
  // LED to a different color. Also, set LED mode to 3-bit constant color.
  // TODO run "cpp -dM rgb.h > rgb.asm.h" to keep the #define but lose the C.
  #define RGB_BASE a1
  #define CSR_RGB_BASE      0xe0006800
  #define CSR_RGB_CTRL_ADDR 0xe0006808
  #define CSR_RGB_RAW_ADDR  0xe000680c
  la    RGB_BASE, CSR_RGB_BASE
  li	a0, 0x3F                                // Raw-R|G|B | ON | Enable
  sw	a0, CSR_RGB_CTRL_ADDR&0xFF(RGB_BASE)    // Set LED to RAW mode
  li	a0, PREFIX(load_color)                  // Chose color per to program
  sw	a0, CSR_RGB_RAW_ADDR&0xFF(RGB_BASE)     // Set LED RAW color
  #undef RGB_BASE

  // Initialize stack pointer to end of RAM
  la    sp, _fstack + 4
  // Set interrupt handler to ASM routine that calls C isr()
  la    a0, trap_entry
  csrw  mtvec, a0

bss_init:
  // Zero all global variables not explicitly initializesd as per C standard.
  la    a0, _fbss
  la    a1, _ebss
bss_loop:
  beq   a0, a1, bss_done
  sw    zero, 0(a0)
  add   a0, a0, 4
  j     bss_loop

bss_done:
  // Copy Data section (preinitialized strings and variables not marked const)
  // to RAM. Needed if executing from ROM or Flash but linker will have the
  // same value for _erodata and _fdata and thus prevent a second copy of the
  // variables when executing from RAM. This loop could be eliminated if so but
  // is kept in case multiple conflicting start addresses are defined.
//#ifdef LOAD_RAM_ADDR
  la    t0, _frodata
  la    t1, _fdata
  la    t2, _edata
data_loop:
  lw    t3, 0(t0)       // Transfer word at a time - pointers are aligned.
  sw    t3, 0(t1)
  addi  t0, t0, 4
  addi  t1, t1, 4
  bltu  t1, t2, data_loop
//#endif /* LOAD_RAM_ADDR */

  // Enable timer and external interrupt sources. Interrupts are still masked
  // so interrupt handler will not be called until unmaksed by user application.
  li    a0, 0x880
  csrw  mie, a0

  // Program enviornment initialized as required by standard C. Hand off to C.
  call  PREFIX(main)

  // Prevent machine fault or worse should main return.
infinite_loop:
  j     infinite_loop
