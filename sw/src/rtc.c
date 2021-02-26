//
// rtc.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <sys/types.h>
#include <stdint.h>
#include <rtc.h>
#include <irq.h>                // Risc-V CPU internal privileged CSR registers
#include <csr-defs.h>           // Risc-V CPU internal privileged CSR registers
#include <generated/csr.h>      // LiteX SoC user CSR registers
//#include "fomu.h"

//#define RTC_FREQUENCY (CONFIG_CLOCK_FREQUENCY/4)
#define RTC_FREQUENCY (CONFIG_CLOCK_FREQUENCY)
// FIXME -- slow clock for testing

volatile a2time_t system_ticks = 0;
int watchdog_timer = 0;


void timer_isr(void) {
  system_ticks++;
  timer0_ev_pending_write(1);
}

void rtc_init(void)
{
  int t;

  timer0_en_write(0);
  #ifdef SIMULATION
  // Use a shorter interval to test interrupts in simulation
  //t = CONFIG_CLOCK_FREQUENCY/10000; // 100 kHz tick (100us)
  t = CONFIG_CLOCK_FREQUENCY/1000;  // 1000 kHz tick (1ms)
  #else
  // Config_clock runs 4x the speed of the timer and the CPU cores so a divide
  // by 4 is requred as well as the divide for number of jiffies per second.
  // This gives us a 1ms jiffie clock.
  t = RTC_FREQUENCY/1000;  // 1000 kHz tick (1ms) 
  #endif
  timer0_reload_write(t);
  timer0_load_write(t);
  timer0_en_write(1);
  timer0_ev_enable_write(1);
  timer0_ev_pending_write(1);
  irq_setmask(irq_getmask() | (1<<TIMER0_INTERRUPT));
}

a2time_t activetime(void) {
#ifdef RISCV_CSR_MCYCLE
  uint32_t low, high;
  high = csrr(mcycleh);
  low = csrr(mcycle);
  if(high!=csrr(mcycleh)) {
    // Wraparound ocurred as nonatomic op - retry and both should be consistent
    high = csrr(mcycleh);
    low = csrr(mcycle);
    // No need to check again. Wrap occurs once every 6 minutes.
  }
  return ((a2time_t)high<<32)|low;
#else
  a2time_t ms;
  int ck;
  ms=system_ticks;
  timer0_update_value_write(1);
  ck = timer0_value_read();
  if (ms != system_ticks) {
    // Wraparound ocurred as nonatomic op - retry and both should be consistent
    ms=system_ticks;
    timer0_update_value_write(1);
    ck = timer0_value_read();
    // No need to check again as system is unstable if this takes more than 1ms
  }
  return (ms+1)*(RTC_FREQUENCY/1000)-ck;
#endif
}

#ifdef WANT_TIME_OF_DAY
int gettimeofday (struct timeval  *__tv, void *__tz) {
  (void)gettimeofdayfast (__tv, __tz);
  // Convert system clock ticks to ns then add ms*1000 from ms counter
  __tv->tv_usec = (__tv->tv_usec*1000) + (__tv->tv_sec%1000)*1000000;
  // convert ms counter to s
  __tv->tv_sec = __tv->tv_sec/1000;
  return 0;
}
#endif
