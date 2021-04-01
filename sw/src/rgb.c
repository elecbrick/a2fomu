//
// rgb.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <rgb.h>
#include <generated/csr.h>

// Field definitions for LEDDCR0
#define CR0_LEDDEN (1<<7)
#define CR0_FR250 (1<<6)
#define CR0_OUTPOL (1<<5)
#define CR0_OUTSKEW (1<<4)
#define CR0_QUICK_STOP (1<<3)
#define CR0_PWM_MODE (1<<2)
#define CR0_BRMSBEXT (1<<0)

#define BREATHE_ENABLE        (1<<7)
#define BREATHE_EDGE_ON       (0<<6)
#define BREATHE_EDGE_BOTH     (1<<6)
#define BREATHE_MODE_MODULATE (1<<5)
#define BREATHE_RATE(x)   ((x&7)<<0)

static enum rgb_mode rgb_mode;

void rgb_set_mode(enum rgb_mode mode) {
  // Turn on the RGB block and current enable, as well as enabling led control
  // Also, put LED in RAW mode unless breathing is selected. Morse Code needs
  // raw mode for faster color changes.
  rgb_ctrl_write((mode==LED_OFF ? 0 : ((1<<CSR_RGB_CTRL_EXE_OFFSET)|
      (1<<CSR_RGB_CTRL_CURREN_OFFSET)|(1<<CSR_RGB_CTRL_RGBLEDEN_OFFSET)))|
      ((mode!=LED_RAW&&mode!=LED_MORSE) ? ((1<<CSR_RGB_CTRL_RRAW_OFFSET)|
      (1<<CSR_RGB_CTRL_GRAW_OFFSET)|(1<<CSR_RGB_CTRL_BRAW_OFFSET)) : 0));
  rgb_mode = mode;
}

void rgb_init(enum rgb_mode mode) {
  // Turn LED on unless mode is disabled and set raw if Morse or raw mode.
  rgb_set_mode(mode);
  // Enable the LED driver, set to 250Hz mode, enable fast mode switch
  rgb_write(CR0_LEDDEN|CR0_FR250|CR0_QUICK_STOP, LEDDCR0);
  // Set clock register to 12 MHz / 64 kHz - 1
  rgb_write((12000000/64000)-1, LEDDBR);
}

void rgb_switch_mode(uint8_t mode, uint8_t ontime, uint8_t offtime,
    uint8_t onrate, uint8_t offrate) {
  if(rgb_mode!=mode) {
    // Clear RGB_CTRL_EXE first for instantaneous mode switch.
    rgb_ctrl_write(0);
    rgb_set_mode(mode);
  }
  // Blink ON time and OFF time are configurable from 0 to 8.16 seconds,
  // in 0.032 second increments.
  rgb_write(onrate, LEDDONR);
  rgb_write(offrate, LEDDOFR);

  rgb_write(BREATHE_ENABLE|BREATHE_EDGE_BOTH|BREATHE_MODE_MODULATE|
      BREATHE_RATE(onrate), LEDDBCRR);
  rgb_write(BREATHE_ENABLE|BREATHE_MODE_MODULATE|BREATHE_RATE(offrate), LEDDBCFR);
}

// Set the 8:8:8 RGB color in breathe mode.
void rgb_set(int color) {
  rgb_write(color>>18, LEDDPWRR); // Red
  rgb_write(color>>10, LEDDPWRG); // Green
  rgb_write(color>>2,  LEDDPWRB); // Blue
}