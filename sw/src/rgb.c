//
// rgb.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <rgb.h>
#include <generated/csr.h>

#define BREATHE_ENABLE (1 << 7)
#define BREATHE_EDGE_ON (0 << 6)
#define BREATHE_EDGE_BOTH (1 << 6)
#define BREATHE_MODE_MODULATE (1 << 5)
#define BREATHE_RATE(x) ((x & 7) << 0)

#define RGB_SWITCH_MODE(x) do { \
    if (rgb_mode == x) \
        return; \
    rgb_mode = x; \
    /* Toggle LEDD_EXE to force the mode to switch */ \
    rgb_ctrl_write(           (1 << 1) | (1 << 2)); \
    rgb_ctrl_write((1 << 0) | (1 << 1) | (1 << 2)); \
} while(0)

static enum {
    INVALID = 0,
    IDLE,
    WRITING,
    ERROR,
    DONE,
} rgb_mode;

void rgb_init(void) {
    // Turn on the RGB block and current enable, as well as enabling led control
    rgb_ctrl_write((1 << 0) | (1 << 1) | (1 << 2));

    // Enable the LED driver, and set 250 Hz mode.
    // Also set quick stop, which we'll use to switch patterns quickly.
    rgb_write((1 << 7) | (1 << 6) | (1 << 3), LEDDCR0);

    // Set clock register to 12 MHz / 64 kHz - 1
    rgb_write((12000000/64000)-1, LEDDBR);
}

void rgb_switch_mode(uint8_t mode,
        uint8_t onr, uint8_t ofr,
        uint8_t onrate, uint8_t offrate,
        uint8_t r, uint8_t g, uint8_t b) {
    RGB_SWITCH_MODE(mode);
    rgb_write(onr, LEDDONR);
    rgb_write(ofr, LEDDOFR);

    rgb_write(BREATHE_ENABLE | BREATHE_EDGE_BOTH
            | BREATHE_MODE_MODULATE | BREATHE_RATE(onrate), LEDDBCRR);
    rgb_write(BREATHE_ENABLE | BREATHE_MODE_MODULATE | BREATHE_RATE(offrate), LEDDBCFR);

    rgb_write(r, LEDDPWRR); // Red
    rgb_write(g, LEDDPWRG); // Green
    rgb_write(b, LEDDPWRB); // Blue
}

void rgb_set(int color) {
    rgb_write(color>>18, LEDDPWRR); // Red
    rgb_write(color>>10, LEDDPWRG); // Green
    rgb_write(color>>2,  LEDDPWRB); // Blue
}