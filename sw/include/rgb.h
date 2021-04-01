//
// rgb.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _RGB_H_
#define _RGB_H_

#include <generated/csr.h>

#define RGB_RAW_BLACK   (0)
#define RGB_RAW_RED     (1)
#define RGB_RAW_GREEN   (2)
#define RGB_RAW_YELLOW  (3)
#define RGB_RAW_BLUE    (4)
#define RGB_RAW_MAGENTA (5)
#define RGB_RAW_CYAN    (6)
#define RGB_RAW_WHITE   (7)

enum rgb_mode {
  LED_OFF,
  LED_CONSTANT,
  LED_FADE,
  LED_RAW,
  LED_MORSE
};

enum led_registers {
  LEDDCR0 = 8,
  LEDDBR = 9,
  LEDDONR = 10,
  LEDDOFR = 11,
  LEDDBCRR = 5,
  LEDDBCFR = 6,
  LEDDPWRR = 1,
  LEDDPWRG = 2,
  LEDDPWRB = 3,
};

static inline void rgb_write(uint8_t value, uint8_t addr) {
  rgb_addr_write(addr);
  rgb_dat_write(value);
}

void rgb_switch_mode(uint8_t mode, uint8_t ontime, uint8_t offtime,
    uint8_t onrate, uint8_t offrate);

void rgb_init(enum rgb_mode mode);
void rgb_set(int color);
void rgb_override(int color);

#endif /* _RGB_H_ */
