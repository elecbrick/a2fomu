//
// perfmon.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _PERFMON_H_
#define _PERFMON_H_

// Performance monitoring for operating system tasks and diagnostic timing
// for critical path determination.
//
// Several operating modes are supported. The main differentiation is FAST
// vs ACCURATE modes of calculating deltas. The accurate mode returns a
// single 64-bit number representing the number of clock cycles elapsed since
// power on while the fast mode returns a pair of 32-bit numbers for clock
// cycles since the last timer interrupt and miliseconds since power on.
//
// The timer interrupt occurs one every millisecond and the litex clock runs
// in the 12MHz clock domain which means the accurate mode needs to multiply
// the current time in ms by 12000 and add this to the current value of the
// litex clock. The a2fomu CPU does not have hardware multiply so this is a
// slow operation in software, especially when performed in the ISR.
//
// Fast mode just stores and uses the 14-bit cycle count and 32-bits of the
// 64-bit ms counter. This allows simple math for events that occur within
// 49 days from power on.
//
// This code does not take advantage of the optional Risc-V mcycle register
// directly although the activetime routine can use it if available.

#include <rtc.h>
#include <irq.h>

//#define ISR_TIME_TRACKING
//#define ACCURATE_PERFMON
//#define FAST_PERFMON

#if defined(ISR_TIME_TRACKING) && !defined(ACCURATE_PERFMON)
#define ACCURATE_PERFMON
#endif
#if defined(FAST_PERFMON) && defined(ACCURATE_PERFMON)
#error "Both FAST and ACCURATE modes requested for perfmon"
#endif
#if !defined(FAST_PERFMON) && !defined(ACCURATE_PERFMON)
#error "One of FAST or ACCURATE modes must be requested for perfmon"
#endif

#ifdef FAST_PERFMON
typedef union a2perfq {
  a2time_t qw;
  struct {
    uint32_t ms, ck;
  };
} a2perf_t;
#else
typedef a2time_t a2perf_t;
#endif

#ifdef ISR_TIME_TRACKING
extern volatile a2perf_t isr_runtime;
#endif
extern volatile int isr_count;

static inline void perfmon_start(a2perf_t *start) {
  #ifdef ISR_TIME_TRACKING
    irq_setie(0);
    *start = (activetime()-isr_runtime);
    irq_setie(1);
  #else
  #ifdef ACCURATE_PERFMON
    *start = activetime();
  #else
    irq_setie(0);
    start->ms = system_ticks;
    timer0_update_value_write(1);
    irq_setie(1);
    start->ck = timer0_value_read();
  #endif
  #endif
}

static inline a2perf_t perfmon_end(a2perf_t start) {
  #ifdef ISR_TIME_TRACKING
    irq_setie(0);
    a2perf_t end = ((activetime()-isr_runtime)-start);
    irq_setie(1);
    return end;
  #else
  #ifdef ACCURATE_PERFMON
    return (a2perf_t)(activetime()-start);
  #else
    a2perf_t end;
    irq_setie(0);
    end.ms=system_ticks;
    timer0_update_value_write(1);
    irq_setie(1);
    end.ck = timer0_value_read();
    end.ms -= start.ms;
    if(end.ck>start.ck) {
      end.ck = end.ck-start.ck;
    } else {
      end.ck = start.ck-end.ck;
    }
    return end;
  #endif
  #endif
}

#endif /* _PERFMON_H_ */
