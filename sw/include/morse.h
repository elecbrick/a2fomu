//
// morse.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _MORSE_H_
#define _MORSE_H_

// Morse Code module definitions.

// Fomu has four touchpads. Here, three are given names for the intended use
// and one is expected to be an output that is connected to the others forming
// a complete circuit.

enum morse_key {
  morse_key_dit = 0,
  morse_key_space = 1,
  morse_key_error = 2,
  morse_key_max
};

void morse_init(int on, int off, int ms);
//void morse_init(void);
void morse_isr(void);
void morse_task(void);
int morse_isidle(void);
int morse_putchar(int c);
void morse_error(int c);
int morse_puts(const char* s);

/* Configuration Parameters */
extern int rgb_morse_on;               // RGB on color - white
extern int rgb_morse_off;              // RGB off color - black
extern long dit_duration;              // dit time - 30 ms is standard

#endif /* _MORSE_H_ */
