//
// ctype.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// C89 compatabile case conversion functions that work with the 7-bit USASCII
// character set of 1980's computers. These are meant to be as compact and fast
// as possible.

#ifndef _CTYPE_H_
#define _CTYPE_H_

// Short routines for character case conversion

#ifndef PRIVATE_TOLOWER
int toupper(int c);
int tolower(int c);

#else /* PRIVATE_TOLOWER */
static inline int toupper(int c) {
  if(c>='a' && c<='z') {
    c = c+'A'-'a';
  }
  return c;
}

static inline int tolower(int c) {
  if(c>='A' && c<='Z') {
    c = c+'a'-'A';
  }
  return c;
}

#endif /* PRIVATE_TOLOWER */
#endif /* _CTYPE_H_ */
