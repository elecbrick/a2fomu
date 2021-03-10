//
// string.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <string.h>

#define USE_PRIVATE_ATOI
#define USE_PRIVATE_STRLEN
#define USE_PRIVATE_STRNCMP
#define USE_PRIVATE_STRTOK

#ifdef USE_PRIVATE_ATOI
int atoi(const char *nptr) {
  int rc = 0;
  while(*nptr>='0' && *nptr<='9') {
    rc = rc*10+*nptr++-'0';
  }
  return rc;
}
#endif

#ifdef USE_PRIVATE_STRLEN
size_t strlen(const char *s) {
  size_t len=0;
  while(*s++) {
    len++;
  }
  return len;
}
#endif

#ifdef USE_PRIVATE_STRCMP
int strcmp(const char *s1, const char *s2) {
  while(*s1 && *s1==*s2) {
    s1++;
    s2++;
  }
  return *s1-*s2;
}
#endif

#ifdef USE_PRIVATE_STRNCMP
int strncmp(const char *s1, const char *s2, size_t n) {
  while(*s1 && *s1==*s2 && --n>0) {
    s1++;
    s2++;
  }
  return *s1-*s2;
}
#endif

#ifdef USE_PRIVATE_MEMSET
void *memset(void *s, int c, size_t n) {
  char *p = s;
  while(n-->0) {
    *p++=c;
  }
  return s;
}
#endif

#ifdef USE_PRIVATE_MEMCPY
void *memcpy(void *dest, const void *src, size_t n) {
  char *d = dest;
  const char *s = src;
  while(n-->0) {
    *d++=*s++;
  }
  return dest;
}
#endif

#ifdef USE_PRIVATE_STRTOK
char *strtok(char *str, const char *delim) {
  static char *stored;
  const char *p;
  char *token;
  if(str) {
    stored = str;
  }
  // Skip past any leading delimiters
  while(*stored) {
    // Check all possible delimiters
    for(p=delim; *p; p++) {
      if(*stored==*p) {
        // It is a match - delimeter found. Proceed to next character.
        stored++;
        break;
      }
    }
    // Check if delimiter found
    if(!*p) {
      // Valid character but not valid delimiter means start of token
      token = stored;
      // Find end of token
      while(*++stored) {
        for(p=delim; *p; p++) {
          if(*stored==*p) {
            // Found delimiter - stop
            break;
          }
        }
        if(*p) {
          // This is a token - replace with end of string and advance past it
          *stored++ = '\0';
          break;
        }
      }
      // End of line reached. Token already properly terminated.
      return token;
    }
    // Delimiter found, pointer already incremented, check next character
  }
  // End of string reached with no token found - return NULL
  return NULL;
}
#endif
