//
// crc.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _CRC_H_
#define _CRC_H_

// Simple implementation of industry standard 32-bit CRC algorithm.

#include <crc.h>

unsigned int crc32(const unsigned char *data, unsigned int length) {
  unsigned int crc, bit, mask, i;

  crc = -1;                             // Initialize result to all ones.
  for(i=0; i<length; i++) {             // Repeat for each byte of input data.
    crc ^= data[i];
    for(bit=0; bit<8; bit++) {          // Repeat for each bit.
      mask = -(crc&1);
      crc = (crc>>1)^(0xEDB88320&mask); // XOR with polinomial (reversed)
    }
  }
  return ~crc;
}

#endif /* _CRC_H_ */