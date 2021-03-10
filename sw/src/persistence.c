//
// persistence.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <stdio.h>
#include <a2fomu.h>
#include <tusb.h>

struct log {
  unsigned int magic;
  FILE persistent;
  unsigned char log[];
};

FILE *persistence;
extern struct log _end;         // Defined by linker - end of bss
extern unsigned char _fstack[]; // Defined by linker - top of stack
register long sp asm ("sp");

#define LOG_MAGIC 0xa2f0f11e
//#define HEAP_LOG (struct log)_end

void persistence_init(void) {
  if(_end.magic == LOG_MAGIC) {
    // Do nothing until log is read
  } else {
    _end.magic = LOG_MAGIC;
    persistence = &_end.persistent;
    persistence->buffer = _end.log;
    persistence->head = 0;
    persistence->tail = 0;
    persistence->_max = _fstack-_end.log-2048;  // Allow 2kB for stack
  }
}

void dump_persistence(void) {
  int n;
  int c;
  // Ensure integrity of persistance file pointer and reset if corrupted
  if(persistence!=&_end.persistent) {
    fprintf(stderr, "corrupt P %08x->%08x ", (unsigned int)persistence,
        (unsigned int)&_end.persistent);
    persistence = &_end.persistent;
  }
  if(persistence->buffer!=_end.log) {
    fprintf(stderr, " B %08x->%08x ", (unsigned int)persistence->buffer,
        (unsigned int)_end.log);
    persistence_init();
    persistence->tail=persistence->_max;
  }
  c=fgetc(persistence);
  tud_cdc_n_write_str(cdc_tty, "\r\n");
  while(c != EOF) {
    // Fill as much of the USB buffer as we can
    n = tud_cdc_n_write_available(cdc_tty);
    while(n-->0) {
      if(c=='\n') {
        if(!n) {
          break;
        }
        // Add the <CR> in <CR><LF>
        tud_cdc_n_write_char(cdc_tty, '\r');
        n--;
      }
      tud_cdc_n_write_char(cdc_tty, c);
      c=fgetc(persistence);
      if(c == EOF) {
        break;
      }
    }
    tud_cdc_n_write_flush(cdc_tty);
    // Run TUD task which transfers the partial log to the host
    yield();
  }
}
