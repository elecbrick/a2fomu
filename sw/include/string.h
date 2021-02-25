//
// string.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _STRING_H_
#define _STRING_H_

// C89 compatible string.h definitions

// Need size_t and NULL
#include <stdio.h>

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

void *memmove(void *dest, const void *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

char *strtok(char *str, const char *delim);

#endif /* _STRING_H_ */
