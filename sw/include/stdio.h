//
// stdio.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _STDIO_H_
#define _STDIO_H_

#define EOF (-1)
#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned int size_t;

#include <stdarg.h>

// First start with a few POSIX defines that stdio relies on.
// The following should be in <limits.h> to be POSIX compliant.
#define MAX_INPUT 1024
#define LINE_MAX (MAX_INPUT-2)
#define OPEN_MAX 6

// Back to stdio.h proper.
#define FOPEN_MAX OPEN_MAX
#define	BUFSIZ MAX_INPUT

#ifndef _UNISTD_H_              /* <unistd.h> has the same definitions.  */
# define SEEK_SET       0       /* Seek from beginning of file.  */
# define SEEK_CUR       1       /* Seek from current position.  */
# define SEEK_END       2       /* Seek from end of file.  */
#endif

// Flags is the combination of:
#define	__SLBF	0x01		// line buffered
#define	__SNBF	0x02		// unbuffered
#define	__SRD	0x04		// OK to read
#define	__SWR	0x08		// OK to write
#define	__SRW	0x10		// open for reading & writing
#define	__SEOF	0x20		// found End Of File
#define	__SERR	0x40		// found error
#define	__SSTR	0x80		// this is a sprintf/snprintf string

struct __FILE {
  int   head;                   // read pointer for getc()
  int   tail;                   // write pointer for putc()
  int   _max;                   // buffer size
  unsigned char *buffer;        // the buffer
  int   minor;                  // device minor is cookie passed to io functions
  short _loc;                   // used by io driver
  char  _flags;                 // things like line buffering, in-use, etc
  char  device;                 // device driver that drains or fills buffer
};

typedef struct __FILE FILE;
extern FILE _file[FOPEN_MAX];

#define	stdin	(&_file[0])
#define	stdout	(&_file[1])
#define	stderr	(&_file[2])

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *pathname, const char *mode, FILE *stream);

int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
int canputc(FILE *stream);

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);
int cangetc(FILE *stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);

int vfprintf (FILE *file, const char *format, va_list ap);
               // gcc is supposed to accept these but complains of redefinition
               //__attribute__ ((__format__ (__printf__, 2, 0)));
int fprintf (FILE *file, const char *format, ...);
               //__attribute__ ((__format__ (__printf__, 2, 3)));
int printf (const char *format, ...);
               //__attribute__ ((__format__ (__printf__, 1, 2)));
int snprintf(char *str, size_t size, const char *format, ...);

// The following size restriction violates C89 but the function is obsolete and
// snprintf should be used directly instead.
#define sprintf(buf, fmt, ...) snprintf( \
    (buf), ((size_t)100), (fmt), ##__VA_ARGS__)

void perror(const char *s);

#endif /* _STDIO_H_ */
