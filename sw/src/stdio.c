//
// stdio.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

FILE _file[FOPEN_MAX];
static unsigned char _buffer[FOPEN_MAX][BUFSIZ];
static int stdio_initialized;

static void _init_stdio(void) {
  stdin ->_max = BUFSIZ-1;
  stdout->_max = BUFSIZ-1;
  stderr->_max = BUFSIZ-1;
  stdin ->buffer=_buffer[0];
  stdout->buffer=_buffer[1];
  stderr->buffer=_buffer[2];
}

#if 0
// Future Improvement: allow opening files and changing stdout between devices
FILE *fopen(const char *pathname, const char *mode) {
}

FILE *fdopen(int fd, const char *mode) {
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
  // Handling of file system should be in a different file
  if(strncmp("/dev/", pathname, 5)==0) {
    if(strcmp("tty", pathname+5)==0) { }
    if(strcmp("led", pathname+5)==0) { }
    if(strcmp("touch", pathname+5)==0) { }
    if(strcmp("floppy", pathname+5)==0) { }
  }
}
#endif

void clearerr(FILE *stream) {
  stream->_flags &= ~(__SEOF|__SERR);
}

int feof(FILE *stream) {
  return stream->_flags&__SEOF;
}

int ferror(FILE *stream) {
  return stream->_flags&__SERR;
}

int fileno(FILE *stream) {
  // A NULL stream will return a negative number signifying error
  return stream-_file;
}


/*============================================================================*
 * Unbuffered, Non-blocking C89 compatible output of characters and strings   *
 * Returns immediately with EOF if output buffer is full                      *
 *============================================================================*/
int fputc(int c, FILE *stream) {
  int rollback;
  if(!stream->buffer) {
    if(!stdio_initialized) {       // Initialize library on first access
      _init_stdio();
    }
    if(!stream->buffer) {  // Second check now that library has been initialized
      errno = EBADF;
      return EOF;
    }
  }
  rollback = stream->tail;
  stream->buffer[stream->tail] = (unsigned char)c;
  stream->tail = stream->tail >= stream->_max ? 0 : stream->tail+1;
  if(stream->tail == stream->head) {
    // Full buffer actually has one empty slot that was just overwriten
    stream->tail = rollback;
    errno = EAGAIN;
    c = EOF; // queue full
  }
  return c; // success
}

int fputs(const char* s, FILE *stream) {
  int rc=0;
  while(*s) {
    rc|=fputc(*s, stream);
    s++;
  }
  return rc;
}

int putc(int c, FILE *stream) {
  return fputc(c, stream);
}

int putchar(int c) {
  return fputc(c, stdout);
}

int puts(const char* s) {
  return fputs(s, stdout);
}

int canputc(FILE *stream) {
  if(stream->head > stream->tail) {
    return stream->head-stream->tail-1;
  }
  return stream->_max-stream->head+stream->tail+1;
}

/*============================================================================*
 * Non-blocking versions of C89 input of characters and strings.              *
 * Returns immediately with EOF if interactive input buffer is empty.         *
 * Functions here act as if underlying file has O_NONBLOCK active.            *
 *============================================================================*/
int fgetc(FILE *stream) {
  int c;
  if(!stream->buffer) {
    if(!stdio_initialized) {       // Initialize library on first access
      _init_stdio();
    }
    if(!stream->buffer) {  // Second check now that library has been initialized
      errno = EBADF;
      return EOF;
    }
  }
  if(stream->head == stream->tail) {
    errno = EAGAIN;
    return EOF; // queue empty
  }
  c = stream->buffer[stream->head];
  stream->head = stream->head >= stream->_max ? 0 : stream->head+1;
  return c; // success
}

char *fgets(char *s, int size, FILE *stream) {
  int c;
  char *p;
  p = s;
  while(size>1) {
    c = fgetc(stream);
    if(c==EOF) {
      *p='\0';
      if(p==s)
        s = NULL; // End of file occured with no characters read
    }
    if(c=='\0')
      break;
    if(c=='\n') {
      *p++ = c;
      break;
    }
    *p++ = c;
    size--;
  }
  *p = '\0';
  return s;
}

int getc(FILE *stream) {
  return fgetc(stream);
}

int getchar(void) {
  return fgetc(stdin);
}


// FIXME WARNING: NOT TESTED
int ungetc(int c, FILE *stream) {
  int rollback;
  rollback = stream->tail;
  //return EOF; // TODO unsupported
  stream->head = stream->head == 0 ? stream->_max : stream->head-1;
  if(stream->tail == stream->head) {
    // queue full, abort
    stream->tail = rollback;
    errno = EAGAIN;
    c = EOF; // queue full
  } else {
    stream->buffer[stream->head] = c;
  }
  return c; // success
}

int cangetc(FILE *stream) {
  return stream->head != stream->tail;
}

enum pad {
  pad_left=0,
  pad_right=1,
  pad_zero=2,
};

static int format_s(FILE *out, const char *string, int width, int pad) {
  int chars_printed=0, padchar=' ';
  if(width>0) {
    int len = 0;
    const char *ptr;
    for(ptr=string; *ptr; ptr++) {
      len++;
    }
    if(len>=width) {
      width = 0;
    } else {
      width -= len;
    }
    if(pad&pad_zero) {
      padchar = '0';
    }
  }
  if(!(pad&pad_right)) {
    for( ; width > 0; --width) {
      fputc(padchar, out);
      chars_printed++;
    }
  }
  for( ; *string ; string++) {
    fputc(*string, out);
    chars_printed++;
  }
  for( ; width>0; --width) {
    fputc(padchar, out);
    chars_printed++;
  }
  return chars_printed;
}

// Buffer for decimal 32-bit integer conversion "-4294967295" including sign
// and terminating NULL
#define PRINT_BUF_LEN 12

static int format_n(FILE *out, int i, int b, int sign,
    int width, int pad, int letbase)
{
  char print_buf[PRINT_BUF_LEN];
  char *s;
  int t, negative=0, chars_printed=0;
  unsigned int u=i;

  if(i==0) {
    print_buf[0] = '0';
    print_buf[1] = '\0';
    return format_s(out, print_buf, width, pad);
  }
  if(sign && b==10 && i<0) {
    negative = 1;
    u = -i;
  }
  s = print_buf + PRINT_BUF_LEN-1;
  *s = '\0';
  while(u) {
    t = u%b;
    if(t>=10)
      t += letbase-'0'-10;
    *--s = t+'0';
    u /= b;
  }
  if(negative) {
    if(width && (pad & pad_zero)) {
      fputc('-', out);
      chars_printed++;
      --width;
    } else {
      *--s = '-';
    }
  }
  return chars_printed + format_s(out, s, width, pad);
}


int vfprintf(FILE *file, const char *format, va_list ap) {
               //__attribute__ ((format (printf, 2, 0)))
  int width, pad;
  int chars_printed = 0;

  for(; *format!=0; format++) {
    if(*format == '%') {
      format++;
      width = pad = 0;
      if (*format=='\0') {
        break;
      }
      if(*format=='%') {
        fputc(*format, file);
        chars_printed++;
      }
      if(*format=='-') {
        format++;
        pad = pad_right;
      }
      while (*format == '0') {
        format++;
        pad |= pad_zero;
      }
      for( ; *format>='0' && *format<='9'; format++) {
        width *= 10;
        width += *format-'0';
      }
      if(*format=='s') {
        char *s = va_arg(ap, char*);
        chars_printed += format_s(file, s, width, pad);
      } else if(*format=='c') {
        fputc(va_arg(ap, int), file);   // Ignore width and alignment for char
        chars_printed ++;
      } else if(*format=='d') {
        chars_printed += format_n(file, va_arg(ap, int), 10, 1, width, pad, 0);
      } else if(*format=='u') {
        chars_printed += format_n(file, va_arg(ap, int), 10, 0, width, pad, 0);
      } else if(*format=='x') {
        chars_printed += format_n(file, va_arg(ap, int), 16, 0, width, pad,'a');
      } else if(*format=='X') {
        chars_printed += format_n(file, va_arg(ap, int), 16, 0, width, pad,'A');
      }
    }
    else {
      fputc(*format, file);
      chars_printed++;
    }
  }
  return chars_printed;
}

int fprintf(FILE *file, const char *format, ...) {
               //__attribute__ ((format (printf, 2, 3))) {
  int rc;
  va_list ap;
  va_start (ap, format);
  rc = vfprintf (file, format, ap);
  va_end (ap);
  return rc;
}

int printf(const char *format, ...) {
               //__attribute__ ((format (printf, 1, 2))) {
  int rc;
  va_list ap;
  va_start(ap, format);
  rc = vfprintf (stdout, format, ap);
  va_end(ap);
  return rc;
}

int snprintf(char *str, size_t size, const char *format, ...) {
  FILE sprintf_file;
  int rc;
  va_list ap;
  va_start(ap, format);
  // set head, tail and _max
  //memset((void*)&sprintf_file, 0, sizeof(sprintf_file));
  sprintf_file.head = 0;
  sprintf_file.tail = 0;
  sprintf_file._max = size;
  sprintf_file.buffer = (unsigned char*)str;
  rc = vfprintf (&sprintf_file, format, ap);
  str[rc] = '\0';
  va_end(ap);
  return rc;
}

#ifdef sprintf
#undef sprintf
#endif
int sprintf(char *str, const char *format, ...) {
  FILE sprintf_file;
  int rc;
  va_list ap;
  va_start(ap, format);
  sprintf_file.head = 0;
  sprintf_file.tail = 0;
  sprintf_file._max = 100;
  sprintf_file.buffer = (unsigned char*)str;
  rc = vfprintf (&sprintf_file, format, ap);
  str[rc] = '\0';
  va_end(ap);
  return rc;
}

#ifdef FWRITE_IN_STDIO
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  (void)ptr;
  (void)size;
  (void)nmemb;
  (void)stream;
  return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  (void)ptr;
  (void)size;
  (void)nmemb;
  (void)stream;
  return 0;
}
#endif


#ifdef PERROR_IN_STDIO
void perror(const char *s) {
  morse_init(7,0,300);
  while(true) {
    (void)morse_putchar(c);
    while(!morse_isidle())
      ;
    set_timer(WORD_SPACE);
  }
}
#endif
