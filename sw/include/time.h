//
// time.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _TIME_H_
#define _TIME_H_

// POSIX 2001 time functions. Should be <sys/time.h>

#include <stdint.h>
#include <sys/types.h>

#ifndef __time_t_defined
#define __time_t_defined
typedef uint32_t time_t;
typedef uint32_t suseconds_t;

struct timeval {
  time_t      tv_sec;     // seconds
  suseconds_t tv_usec;    // microseconds
};
#endif

// structure passed for compatability - ignored
struct timezone {
  int tz_minuteswest;     // minutes west of Greenwich
  int tz_dsttime;         // type of DST correction
};

extern int gettimeofday(struct timeval  *__tv,
			struct timezone *__tz);

// Set the current time of day
extern int settimeofday(const struct timeval *__tv,
			const struct timezone *__tz);

extern int gettimeofdayfast(struct timeval  *__tv,
 		            struct timezone *__tz);

#if 0
// Obsolete functions replaced by rtc_init(), perfmon() and yield()
void time_init(void);
int elapsed(int *last_event, int period);
#endif

unsigned int sleep(unsigned int s);     // Task waits s seconds allowing others
unsigned int msleep(unsigned int ms);   // Wait ms milliseconds allowing others
unsigned int nsleep(unsigned int ns);   // Busy wait ns nanoseconds, CPU freezes

#endif /* _TIME_H_ */
