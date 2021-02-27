//
// crc.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _CRC_H_
#define _CRC_H_

unsigned int crc16(const unsigned char *data, unsigned int length);
unsigned int crc32(const unsigned char *data, unsigned int length);

#endif /* _CRC_H_ */
