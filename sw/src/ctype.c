//
// ctype.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// C89 compatable routines for character identification and case conversion

#include <ctype.h>

int toupper(int c) {
  if(c>='a' && c<='z') {
    c = c+'A'-'a';
  }
  return c;
}

int tolower(int c) {
  if(c>='A' && c<='Z') {
    c = c+'a'-'A';
  }
  return c;
}


int isdigit(int c) {
  return c>='0'&&c<='9';
}

int isupper(int c) {
  return c>='A'&&c<='Z';
}

int islower(int c) {
  return c>='a'&&c<='z';
}

int isalpha(int c) {
  return isupper(c)||islower(c);
}

int isalnum(int c) {
  return isalpha(c)||isdigit(c);
}

int iscntrl(int c) {
  return c<' '||c>'~';
}

int isprint(int c) {
  return !iscntrl(c);
}

int ispunct(int c) {
  return isprint(c)&&!isalnum(c);
}

int isgraph(int c) {
  return isprint(c);
}

int isspace(int c) {
  return c==' '||c=='\t'||c=='\r'||c=='\n';
}

int isxdigit(int c) {
  c=toupper(c);
  return isdigit(c)||(c>='A'&&c<='F');
}
