//
// rtc.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _RTC_H_
#define _RTC_H_

#include <stdint.h>

// Include <sys/time.h> to get the standard gettimeofday which will
// convert system clock ticks to seconds and nanoseconds
// int gettimeofday (struct timeval *__tv, void *__tz);

// A2Fomu time is system clock ticks since power on.
typedef uint64_t a2time_t;
extern volatile a2time_t system_ticks;
extern int watchdog_timer;
extern int watchdog_max;
extern int yield_max;
extern a2time_t yield_timeout;

static inline a2time_t rtc_read (void) {
  return system_ticks;
}

void timer_isr(void);
a2time_t activetime(void);
void rtc_init(void);

#endif /* _RTC_H_ */
