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
#include <a2fomu.h>             // OS call yield() used by sleep and msleep

// There are two different methods available for keeping time in the SoC
// containing LiteX and Risc-V. A timer module with interrupt is provided
// that is used as a milisecond counter primarily for the Morse Code module.
//
// Additionally, there is a 64-bit counter in a special register in the Risc-V
// architecture definition that is not manditory. If this register is present,
// the process of reading a coherent time is simplified by not reqireing
// interrupts to be disabled. However, the 64-bit counter can only be read
// 32-bits at a time and there could be a wrap between reads. Thus, the means
// of returning consistent values requires reading the high order word before
// and after reading the low order word. If the two values are identical, then
// the whole 64-bit number is consistent. If the two halves are different, then
// a carry into the upper bits occurred and the lower bits should be read again.
// There is no need to read the upper bits again as a wrap only occurs about
// once every five minutes given the core is running at 12MHz.  This logic is
// optional in the Vex Risc-V processor. There are two 64-bit registers that are
// either both present or both removed from the design at the same time. It
// takes 128 logic elements just for the flip-flops needed for these two
// registers plus anciliary decoding logic. A design that is tight on space in
// the 5000 logic element ice5kup FPGA could see this as wasting 2.5% of the
// available logic and chose to disable these counters.
//
// The timer needs to follow a similar methodology; however, this time there is
// the additional overhead of requiring interrupts to be disabled first. When
// the timer expires, an interrupt fires and the interrupt handler resets the
// timer and increments the milisecond counter that is kept by software. If
// interrupts are not disabled, a race condition could occur that would result
// in the loss of a tick.

#ifndef RISCV_CSR_MCYCLE
#define RISCV_CSR_MCYCLE
#endif

#ifdef SIMULATION
// Use shorter interval to test interrupts in simulation - 100 kHz tick (100us)
#define RTC_FREQUENCY (1200000)
#else
#if CONFIG_CLOCK_FREQUENCY == 12000000
// CPU and timer both in 12MHz clock domain. This gives us a 1ms jiffie clock.
#define RTC_FREQUENCY (CONFIG_CLOCK_FREQUENCY)
#else
#if CONFIG_CLOCK_FREQUENCY == 48000000
// Config_clock runs 4x the speed of the timer and the CPU cores so a divide
// by 4 is requred for a 1ms jiffie clock updated directly by the timer.
// This gives us a 1ms jiffie clock.
#define RTC_FREQUENCY (CONFIG_CLOCK_FREQUENCY/4)
#else
#error "Clock frequency does not correspond to known configuration"
#endif /* 48MHz */
#endif /* 12MHz */
#endif /* SIMULATION */


volatile a2time_t system_ticks = 0;
int watchdog_timer = 0;
int watchdog_max = 500;  // 0.5 seconds
int yield_max = 1000;    // 1.0 seconds
a2time_t yield_timeout;


void timer_isr(void) {
  system_ticks++;
  timer0_ev_pending_write(1);
}

void rtc_init(void)
{
  int t;

  timer0_en_write(0);
  t = RTC_FREQUENCY/1000;  // 1000 kHz tick (1ms) 
  timer0_reload_write(t);
  timer0_load_write(t);
  timer0_en_write(1);
  timer0_ev_enable_write(1);
  timer0_ev_pending_write(1);
  irq_setmask(irq_getmask() | (1<<TIMER0_INTERRUPT));
}


// Suspend task until the require time has passed
// Parameter is time in seconds
unsigned int sleep(unsigned int s) {
  a2time_t end = rtc_read()+1000*s;
  while(rtc_read()<end) {
    yield();
  }
  return 0;
}

// Suspend task until the require time has passed
// Parameter is time in milliseconds
unsigned int msleep(unsigned int ms) {
  a2time_t end = rtc_read()+ms;
  while(rtc_read()<end) {
    yield();
  }
  return 0;
}

// Busy wait until the required time has passed
// Parameter is time in nanoseconds
unsigned int nsleep(unsigned int ns) {
  // Approximate HZ with a compile time constant of 84 rather than 83.333
  a2time_t end = activetime()+(ns*1000000)/RTC_FREQUENCY;
  while(activetime()<end) {
    ;
  }
  return 0;
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
