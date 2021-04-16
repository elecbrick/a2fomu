//
// main.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <generated/soc.h>
#include <generated/mem.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <a2fomu.h>
#include <irq.h>
#include <time.h>
#include <tusb.h>
#include <bsp/board.h>
#include <rgb.h>
#include <rtc.h>
#include <cli.h>
#include <disk.h>
#include <morse.h>
#include <fsfat.h>
#include <flash.h>

// Performance analysis is ongoing. The system will speed if if performance
// monitoring is disabled but this also means the cli command "times" has
// nothing to display.
//#define ISR_TIME_TRACKING
//#define ACCURATE_PERFMON
#define DISABLE_PERFMON
#include <perfmon.h>

a2time_t task_runtime[max_task];
volatile a2time_t isr_runtime;
volatile int isr_count;

int debug_counter[max_application_error];
enum scroll_mode scroll_mode;

FILE *disk_fd;

// A2Fomu has no data cache so pack variables that are used together into words
// hoping the compiler will only issue a single memory read for all members.
// TODO Verify this assumption.
signed char prev_h, prev_v, cursor_h, cursor_v;
int prev_c;

unsigned int minsp;
static inline unsigned int read_stack_pointer(void) {
  unsigned int value;
  asm volatile ("move %0, sp" : "=r"(value));
  return value;
}

void reboot(void)
{
  void _start(void);
  // Restart program preserving persistent memory. Reinitialize stack and vars.
  #ifndef DEBUG
  _start();
  #else
  TU_BREAKPOINT();
  #endif
  __builtin_unreachable();
}

#ifdef USING_ATTRIBUTE_INTERRUPT
__attribute__ ((interrupt ("machine")))
#endif
void isr(void) {
  unsigned int irqs;
  #ifdef ISR_TIME_TRACKING
  a2time_t isr_start = activetime();
  #endif
  isr_count++;
  if(read_stack_pointer()<minsp) {
    minsp = read_stack_pointer();
  }
  irqs = irq_pending() & irq_getmask();
  if(irqs & (1 << USB_INTERRUPT)) {
    tud_int_handler(0);
    //dcd_int_handler(0); // tud_int_handler calls this
  } else {
    if(irqs & (1 << TIMER0_INTERRUPT)) {
      if(usb_next_ev_read()) {
        tud_int_handler(0);
        debug_counter[usb_interrupt_lost]++;
      }
    }
  }
  if(irqs & (1 << TIMER0_INTERRUPT)) {
    system_ticks++;
    timer0_ev_pending_write(1);
    watchdog_timer++;
    if(watchdog_timer>watchdog_max) {
      fprintf(persistence, "\n%d Watchdog timeout at %08x sp %08x\n",
          (int)system_ticks, (unsigned int)csrr(mepc), minsp);
      reboot();
    }
    morse_isr();
  }
  if(csrr(mcause) != 0x8000000b) {  // Machine external interrupt
    // Any trap or exception that other than USB and timer is logged.
    fprintf(persistence, "%d Exception %d at %08x referencing %08x\n",
        (int)system_ticks, (int)csrr(mcause), (unsigned int)csrr(mepc),
        (unsigned int)csrr(mtval));
    reboot();
  }
  #ifdef ISR_TIME_TRACKING
  isr_runtime += (activetime()-isr_start);
  #endif
}

// Convert from screen memory character to 8-bit clean ASCII.
// This does not work well as a macro so must be an inline function.
// Although the primary goal is to create a printable character, it has a
// secondary objective.
// First, the Apple II character memory is only pseudo-ASCII. Normal characters
// have bit 7 is set.  Reverse video character have bits 6 and 7 clear.
// Flashing characters have bit 7 clear but bit 6 set. This means the 3 hex
// values of space are (a0, 20, 60) but the values for letters, such as A are
// (c1, 01, 81). As such, it is necessary to flip bit 5 if bit 7 is not set and
// just clear bit 7 otherwise.  Any flashing character is assumed to be the
// cursor.
int a2toascii(int c, int h, int v) {
  if((c&0xC0)==0x40) {
    // Any flashing character is taken to be the cursor
    cursor_h=h;
    cursor_v=v;
  } else if(cursor_h==h && cursor_v==v) {
    cursor_h=-1;
    cursor_v=-1;
  }
  return (c&0x20 ? c&0x3f : (c&0x1f)|0x40);
}

#define A2TOASCII(c) a2toascii(c, h, v)

void redraw(void) {
  union {
    unsigned int c4;
    unsigned char c[4];
  } convert;
  int v, h;
  void *vram = (void*)(A2RAM_BASE+0x400);
  puts("\033[H\033[J");
  for(v=0; v<24; v++) {
    for(h=0; h<40; ) {
      while(canputc(stdout)<4) {
        yield();
      }
      convert.c4=*(int*)(vram+v/8*40+v%8*128+h);
      putchar(A2TOASCII(convert.c[0])); h++;
      putchar(A2TOASCII(convert.c[1])); h++;
      putchar(A2TOASCII(convert.c[2])); h++;
      putchar(A2TOASCII(convert.c[3])); h++;
    }
    if(v<23) {
      putchar('\n');
    }
  }
  // We should have retrieved one flashing character from screen memory: cursor
  if(cursor_v>=0 && cursor_h>=0) {
    prev_h=cursor_h-1;
    prev_v=cursor_v;
  }
  // Position to cursor or last character updated on screen if not present
  printf("\033[%d;%dH", prev_v+1, prev_h+2);
}

void function_key(int n) {
  (void)n;
}

#define ESC_START  1
#define ESC_CSI 0x40
#define ESC_APP 0x80
// Keyboard ESC O +  jklmnopqrstuvwxy
char keypad_map[] = "*+,-./0123456789";

void tty_task(void) {
  static int in_esc;
  int c, rc;
  if(tud_cdc_n_connected(cdc_tty)) {
    if(tud_cdc_n_available(cdc_tty)) {
      // connected and data is available
      uint8_t buf[64];
      int count = tud_cdc_n_read(cdc_tty, buf, sizeof(buf));
      for(int i=0; i<count; i++) {
        if(cli_active || buf[i]==cli_escape) {
          rc=cli((char*)&buf[i], count-i);
          i+=rc;
          if(rc==0 || i>=count) {
            break;
          }
        }
        // Force upper case as that is all Apple II+ understands
        c = toupper(buf[i]);
        if(in_esc) {
          // In an escape sequence - detect arrow keys, etc
          if(in_esc & (ESC_CSI | ESC_APP)) {
            // Treat Application and Command mode as equivalent
            if(c>='0' && c<='9') {
              in_esc = (c-'0' + (10*(in_esc&~(ESC_CSI|ESC_APP)))) |
                (in_esc&(ESC_CSI|ESC_APP));
            } else {
              if(c=='A') {
                // Up Arrow becomes Apple ESC code for command line editing
                rc=fputs("\033D", stdin);
              } else if(c=='B') {
                // Down Arrow becomes Apple ESC code for command line editing
                rc=fputs("\033C", stdin);
              } else if(c=='C') {
                // Right Arrow - ^U (high bit set)
                rc=fputc(0x95, stdin);
              } else if(c=='D') {
                // Left Arrow - ^H (high bit set)
                rc=fputc(0x88, stdin);
              } else if(c=='M') {
                // Keypad Enter
                rc=fputc('\r', stdin);
              } else if(c=='X') {
                // Keypad Equal
                rc=fputc('=', stdin);
              } else if(c>='P' && c<='S') {
                // Function key 1-4
                function_key(c-'P'+1);
              } else if(c>='j' && c<='y') {
                // Keypad digits and symbols
                rc=fputc(keypad_map[c-'j'], stdin);
              } else if(c=='~') {
                switch(in_esc&~(ESC_CSI|ESC_APP)) {
                  case 1:
                    // Home
                    rc=fputs("\033@", stdin);
                    break;
                  case 2:
                    // Insert
                    rc=fputs("\033F", stdin);
                    break;
                  case 3:
                    // Delete become Left Arrow
                    rc=fputc(0x88, stdin);
                    break;
                  case 4:
                    // End
                    rc=fputs("\033E", stdin);
                    break;
                  case 5:
                    // Page Up
                    rc=fputs("\033I", stdin);
                    break;
                  case 6:
                    // Page Down
                    rc=fputs("\033M", stdin);
                    break;
                  case 15:
                    // F5
                    function_key(5);
                    break;
                  case 17:
                  case 18:
                  case 19:
                  case 20:
                  case 21:
                    // F6 - F10
                    function_key(c-17+6);
                    break;
                  case 23:
                  case 24:
                    // F11 - F12
                    function_key(c-23+11);
                    break;
                  default:
                    // Ignore unrecognized key
                    break;
                }
              } else {
                // Unrecognized sequence - user may have pushed ESC key
                // Reconstruct sequence of keys user likely pressed
                if(in_esc&~(ESC_CSI|ESC_APP)) {
                  rc = fprintf(stdin, "\033%c%d%c", in_esc&ESC_CSI?'[':'O',
                      in_esc&~(ESC_CSI|ESC_APP), c);
                } else {
                  rc = fprintf(stdin, "\033%c%c", in_esc&ESC_CSI?'[':'O', c);
                }
              }
              in_esc = 0;
            }
          } else if(c=='[') {
            in_esc = ESC_CSI;
          } else if(c=='O') {
            in_esc = ESC_APP;
          } else {
            // Unrecognized sequence - user likely pushed ESC key
            rc = fprintf(stdin, "\033%c", c);
            in_esc = 0;
          }
        } else if(c=='\033') {
          in_esc = ESC_START;
        } else if(c=='\22' || c=='\0') {
          // NUL / Control-R: Send reset pulse, activate and release reset
          // Take A2 out of reset
          uint32_t control = apple2_control_read();
          apple2_control_write(control | (1<<CSR_APPLE2_CONTROL_RESET_OFFSET));
          apple2_control_write(control & ~(1<<CSR_APPLE2_CONTROL_RESET_OFFSET));
        } else if(c=='\b' || c=='\x7f') {
          // Backspace and Delete become Left Arrow
          rc=putc(0x88, stdin);
        } else if(c=='\f') {
          // Control-L: Redraw screen
          redraw();
        } else {
          rc=putc(c, stdin);
          if(rc<0)
            debug_counter[tty_input_overflow]++;
        }
      }
    }
    int n = tud_cdc_n_write_available(cdc_tty);
    int written=0;
    while(n-->0 && (c=fgetc(stdout)) != EOF) {
      if(c=='\n') {
        if(n--<=0) {
          ungetc(c, stdout);
          break;
        }
        tud_cdc_n_write_char(cdc_tty, '\r');
      }
      tud_cdc_n_write_char(cdc_tty, c);
      written=1;
    }
    if(written) {
      tud_cdc_n_write_flush(cdc_tty);
    }
  }
}

// Invoked when cdc line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  // connected
  if( dtr && rts ) {
    // initial message when connected
    if(itf==0) {
      // tty connected - redirect stdin and stdout across this link
      stdin->device = a2dev_usb;
      stdin->minor = itf;
      stdout->device = a2dev_usb;
      stdout->minor = itf;
      stderr->device = a2dev_usb;
      stderr->minor = itf;
      tud_cdc_n_write_str(itf, "A2Fomu connected\r\n");
    } else {
      // disk connected
      external_disk_state = ext_no_disk;
      #if 0
      stderr->device = a2dev_usb;
      stderr->minor = itf;
      #endif
      printf("(F)"); // Debug
      tud_cdc_n_write_str(itf, "A2F>");
    }
    tud_cdc_n_write_flush(itf);
  } else {
    if(itf==0) {
      // usb tty disconnected - revert communications to hard keys and lights
      if(stdin->device==a2dev_usb && stdin->minor==itf) {
        stdin->device = a2dev_touch;
      }
      if(stdout->device==a2dev_usb && stdout->minor==itf) {
        stdout->device = a2dev_led;
      }
    } else {
      // disk detatched
      if(stderr->device==a2dev_usb && stderr->minor==itf) {
        stderr->device = a2dev_led;
      }
      external_disk_state = ext_disconnected;
      printf("(f)"); // Debug
    }
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf) {
  (void) itf;
  // Do nothing at interupt level, wait for device task to drain buffers
  // TODO writes to disk are read back as reads from the same device
}

void keyboard_task(void) {
  int c;
  // Do nothing if character has already been sent
  if(!apple2_strobe_read()) {
    // Get the next character and send it
    c=fgetc(stdin);
    if(c != EOF) {
      apple2_keyboard_write(c|0x80);
    }
  }
}

// Convert an integer in the range 0-99 into a 1 or 2 digit string
// Does not use division or multiplication
char *itoa99(char *s, int n) {
  char *p = s;
  int d = 0;
  while(n>10) {
    d+=10;
    n-=10;
  }
  if(d>0) {
    *p++='0'+d;
  }
  *p++='0'+n;
  *p='\0';
  return s;
}

void error(const char *msg) {
  fprintf(stdout, "Em:%s\n", msg);
}

void video_task(void) {
  static int vid;
  static char space_supress;
  // Pack these variables into a single memory access
  static unsigned char scroll_start, scroll_top, scroll_bottom, cursor_active;
  int c, h, v, rc=0;
  unsigned int flags;
  // Print stored character if output buffer was full on the last attempt
  if(!(vid&(1<<CSR_APPLE2_SCREEN_VALID_OFFSET))) {
    vid = apple2_screen_read();
  }
  if(stdout->device == a2dev_usb) {
    while((canputc(stdout)>40) && (vid & (1<<CSR_APPLE2_SCREEN_VALID_OFFSET))) {
      //fprintf(stderr, "%08x/", vid);
      h = (vid>>CSR_APPLE2_SCREEN_HORIZONTAL_OFFSET) & 0xff;
      v = (vid>>CSR_APPLE2_SCREEN_VERTICAL_OFFSET) & 0xff;
      // This macro uses h and v so they need to be updated first
      c = A2TOASCII(vid>>CSR_APPLE2_SCREEN_CHARACTER_OFFSET);
      // Ignore off-screen writes to frame buffer. In particular, the boot
      // sequence looking for disk controller writes to location 07F8.
      // This is an unlikely scenario so optimize for code space
      if(h>=40 || v>=24) {
        vid = apple2_screen_read();
        continue;
      }
      flags = vid&0x0000F800;
      if(flags) {
        #ifndef NOCURSORMOVEMENT
        //printf("%08x/", vid); // Debug
        //fputc('z', stderr);
        if(flags&(1<<CSR_APPLE2_SCREEN_REPEAT_OFFSET)) {
          //fprintf(stderr, "(r%d,%d)",v,h);
          if(v==23 && h==39 && prev_c==' ') {
            //fputc('w', stderr);
            // Clear from cursor to end of screen is the simple case
            // Normal video space (0x80|' ') and target is last position (23,39)
            puts("\033[J");
            if(c==' ') {
              // Clear did not move TTY cursor so update v,h accordingly
              // Chances are, the cursor is in the home position as this is
              // exactly where the program will be writing to next.
              v=prev_v;
              h=prev_h;
              if(vid & (1<<CSR_APPLE2_SCREEN_MORE_OFFSET)) {
                vid = apple2_screen_read();
                continue;
              }
              // No pending character so wait until next pass to check.
              // Need to clear valid but may as well clear the whole thing
              vid = 0;
              break;
            } else {
              // Clearing screen and writing to last character
              // Fall through for normal handling
            }
          } else {
            //
            if(v==prev_v) {
              //fputc('r', stderr);
              // Start and end are on the same line - fill with repeating char
              for( ; prev_h<h-1; prev_h++) {
                putchar(prev_c);
              }
              // Current character will be printed by regular means
            } else {
              if(prev_c==' ') {
                //fputc('s', stderr);
                // Character is a space so we can use the ANSI clear line command
                // for all lines except the last
                for( ; prev_v<v; prev_v++) {
                  puts("\033[K\r");
                }
                // Cursor moved to beginning of line to reset known H value
                prev_h=0;
                if(h==39) {
                  // The last line is also cleared to end so use sequence
                  puts("\033[K");
                  // Fall through to normal handling to print last character
                } else {
                  // Last line is only partially cleared so print spaces
                  for(prev_h=0; prev_h<h-1; prev_h++) {
                    putchar(' ');
                  }
                  // Fall through to normal handling to print last character
                }
              } else {
                //fputc('n', stderr);
                // Repeating non-space so loop for as many elements as required
                // Start by finishing line cursor was on
                for( ; prev_h<40; prev_h++) {
                  putchar(prev_c);
                }
                putchar('\r');
                // Continue pattern for each complete line
                for( ; prev_v<v; prev_v++) {
                  for(prev_h=0; prev_h<40; prev_h++) {
                    putchar(prev_c);
                  }
                  putchar('\r');
                }
                // Finish by printing the pattern until the desired end location
                for(prev_h=0; prev_h<h-1; prev_h++) {
                  putchar(prev_c);
                }
                // Fall through to normal handling to print last character
              }
            }
          }
        } else if(flags&(1<<CSR_APPLE2_SCREEN_SCROLLSTART_OFFSET)) {
          // Suppress this character. It would move the cursor to the top right
          // corner of the screen and write the character from the end of line
          // two.  This is unnecessary as the terminal scroll command will
          // perform this anyway.
          //fprintf(stderr, "[S%d,%d]",v,h);
          scroll_start = v+1;
          // Scroll takes about 20ms to copy 1000 characters at 1MHz so it is
          // highly unlikely there will be another character waiting but for
          // consistency, we check since the value of vid must be updated
          // anyway.
          vid = apple2_screen_read();
          continue;
        } else if(flags&(1<<CSR_APPLE2_SCREEN_SCROLLEND_OFFSET)) {
          //fprintf(stderr, "{e%d,%d}",v,h);
          if(scroll_mode==scroll_enhanced) {
            if(scroll_bottom>0) {
              scroll_bottom = 0;   // Limited scroll region is in force.
              printf("\033[r");    // Reset scroll region to whole screen.
            }
            putchar('\n');    // Just use a standard New Line character
            prev_h=-1;        // which implies Carriage Return.
          } else {
            if(scroll_start!=scroll_top || v!=scroll_bottom) {
              scroll_top = scroll_start;
              scroll_bottom = v;
              printf("\033[%d,%dr", scroll_top, scroll_bottom+2);
            }
            // Send scroll 
            printf("\033[S"); // S for Scroll - T for reverse-scroll
          }
          scroll_start = 0;
          //printf("\033[%d;%dH", v+1, h+1);
          // No need to clear the bottom line as the terminal scroll command
          // automatically performs this function as well.
          space_supress = 40;
          // Suppress this character also. Not suppressing would cause the
          // the cursor to redraw the character at line 23 column 1.
          vid = apple2_screen_read();
          continue;
        } else fprintf(stderr, "{vid:%08x}", vid);
        #else
        if(flags&(1<<CSR_APPLE2_SCREEN_SCROLLEND_OFFSET)) {
          space_supress = 40;
        }
        #endif
      }

      if(c==' ') {
        // This could be clearing to the end of a line without HW compress.
        // Perform software compress of clear to end of line.
        if(space_supress>0) {
          space_supress--;
          vid = apple2_screen_read();
          continue;
        }
      }
      //fprintf(stderr, "(%d,%d)", v,h);
      // Reposition cursor in easiest way possible
      if(v==prev_v) {
        if(h==prev_h+1) {
          // No repositioning necessary
          //fputc('.', stderr);
        } else if(h==0) {
          // Use Carriage Return to return to beginning of current line
          if(canputc(stdout) < 2) {
            error("cr");
            debug_counter[video_output_overflow]++;
            break;
          }
          putchar('\r');
        } else if(h<=prev_h && prev_h-h<5) {
          // Use one or more backspaces
          if(canputc(stdout) <= prev_h-h+1) {
            error("bs");
            debug_counter[video_output_overflow]++;
            break;
          }
          for(int i=0; i<=prev_h-h; i++) {
            putchar('\b');
          }
        } else {
          // Resposition with ANSI CSI CHA Cursor Horizontal Absolute
          if(canputc(stdout) < 6) {
            error("nl");
            debug_counter[video_output_overflow]++;
            break;
          }
          printf("\033[%dG", h+1);
        }
      } else if(h==0 && v==prev_v+1) {
        // Use Newline to get to beginning of next line
        if(canputc(stdout) < 2) {
          error("nl");
          debug_counter[video_output_overflow]++;
          break;
        }
        putchar('\n');
      } else {
        // Reposition with ANSI CSI CUP Cursor Position sequence
        if(canputc(stdout) < 9) {
          error("cup");
          debug_counter[video_output_overflow]++;
          break;
        }
        printf("\033[%d;%dH", v+1, h+1);
      }
      rc=putchar(c);
      if(rc<0) {
        debug_counter[video_output_overflow]++;
        error("ov");
        break;
      }
      prev_c=c;
      prev_h=h;
      prev_v=v;
      if(vid & (1<<CSR_APPLE2_SCREEN_MORE_OFFSET)) {
        vid = apple2_screen_read();
      } else {
      // Need to clear valid but may as well clear the whole thing
        vid=0;
      }
    }
    // At this point, vid will contain an unprinted character or 0 indicating
    // done. Reposition cursor if no characters pending and cursor is misplaced.
    if(~(vid & ((1<<CSR_APPLE2_SCREEN_VALID_OFFSET)|
               (1<<CSR_APPLE2_SCREEN_MORE_OFFSET))) &&
               (cursor_h==prev_h && cursor_v==prev_v)) {
      putchar('\b');
      prev_h--;
    }
  } else {
    // Output is not to a tty so it is likely single line or Morse Code output.
    // No curson positioning is available. Device is slow so no more than one
    // character needs to be handled each pass through this loop.
    while((canputc(stdout)>2) && (vid & (1<<CSR_APPLE2_SCREEN_VALID_OFFSET))) {
      // We have a character and buffer space to print it. Translate to ASCII.
      h = (vid>>CSR_APPLE2_SCREEN_HORIZONTAL_OFFSET) & 0xff;
      v = (vid>>CSR_APPLE2_SCREEN_VERTICAL_OFFSET) & 0xff;
      c = A2TOASCII(vid>>CSR_APPLE2_SCREEN_CHARACTER_OFFSET);
      if(h>=40 || v>=24) {
        // Ignore off-screen writes to frame buffer. In particular, the boot
        // sequence looking for disk controller writes to location 07F8.
        // This is an unlikely scenario so optimize for code space
        vid = apple2_screen_read();
        continue;
      }
      // Handle repeat, scroll, space and other movements
      flags = vid&0x0000F800;
      if(flags) {
        if(flags&(1<<CSR_APPLE2_SCREEN_REPEAT_OFFSET)) {
          if(prev_c==' ') {
            // Compress multiple spaces to a single one
            if(c==' ') {
              // Clear to end of line - supress extra space
              vid = apple2_screen_read();
              continue;
            }
            // Fall through for normal printing of the last character
          } else {
            // Repeating a character other than space - fill with previous
            // character from previous position to current position
            // 1. Start by finishing line cursor was on
            for( ; prev_h<40; prev_h++) {
              putchar(prev_c);
            }
            // 2. Continue pattern for each complete line
            // Let the output device take care of line wrapping
            for( ; prev_v<v; prev_v++) {
              for(prev_h=0; prev_h<40; prev_h++) {
                putchar(prev_c);
              }
            }
            // 3. Finish by printing the pattern until the desired end location
            for(prev_h=0; prev_h<h-1; prev_h++) {
              putchar(prev_c);
            }
            // Fall through to normal handling to print last character
          }
        } else if(flags&(1<<CSR_APPLE2_SCREEN_SCROLLSTART_OFFSET)) {
          // Nothing to do on scroll start, only on end
          vid=0;
          break;
        } else if(flags&(1<<CSR_APPLE2_SCREEN_SCROLLEND_OFFSET)) {
          // Scroll is a simple newline
          c = '\n';
        }
      } else {
        if(cursor_active || (vid&0xC0)==0x40) {
          // No need to show or remove cursor
          cursor_active = ~cursor_active;
          vid = apple2_screen_read();
          continue;
        }
        if(c==' ' && (prev_c==' ' || prev_c=='\n')) {
          // Suppress consecutive spaces
          vid = apple2_screen_read();
          continue;
        }
        if(h==prev_h+1 && v==prev_v) {
          // Normal next character
        } else if(h==0 && v==prev_v+1) {
          // Use Carriage Return to get to beginning of next line
          c = '\n';
        } else {
          // Suppress clear of last line that always follows scroll_end
          // TODO Reposition with space or newline unless one just printed
        }
      }
      rc=putchar(c);
      if(rc<0) {
        debug_counter[video_output_overflow]++;
        fputc('#', stderr);
        break;
      }
      prev_c=c;
      prev_h=h;
      prev_v=v;
      if(vid & (1<<CSR_APPLE2_SCREEN_MORE_OFFSET)) {
        vid = apple2_screen_read();
      } else {
        // Need to clear valid but may as well clear the whole thing as the
        // constant 0 saves instruction and cache space
        //vid &= ~(1<<CSR_APPLE2_SCREEN_VALID_OFFSET);
        vid=0;
      }
    }
  }
}

void init(void) {
  rgb_init(LED_RAW);                    // Show successful handoff to main
  rgb_raw_write(RGB_RAW_YELLOW);
  persistence_init();                   // Recover or initialize error logging
  usb_pullup_out_write(0);              // Disable USB to allow new enumeration
  minsp = read_stack_pointer();         // Stack usage profiling / debugging

  // Enable interrupts
  // This is done before any interrupt could be generated as interrupts are
  // edge triggered. Interrupts will never be serviced if one comes in before
  // the interrupt handler is enabled.
#ifdef USING_ATTRIBUTE_INTERRUPT
  // Set the Machine Trap Vector to the Interrupt Service Routine defined above.
  csrw(mtvec, isr);
#endif
  irq_setmask(0);                       // Unmask (enable) all interrupts
  irq_setie(1);                         // Enable interrupts

  rtc_init();                           // Configure and enable timer itself
#ifndef SIMULATION
  // Use an extended busy-wait as interrupt handlers may not yet be enabled.
  // TODO The following is one full second, not the 10ms required
  nsleep(10000000);                     // Standard requires 10ms idle for reset
#endif
  morse_init();                         // LED goes BLACK
#ifndef SIMULATION
  puts("A2");                 // Blink A2 on LED at powerup
#endif
  tusb_init();
  disk_init();
#ifndef SIMULATION
  // Mount root filesystem
  mount((void*)(FLASHFS_START_ADDRESS+SPIFLASH_BASE), 0);
  // Run startup program script
  exec("HELLO");
#endif
}


/*============================================================================*
 *                                                                            *
 * Scheduler: Reentrant Operating System Main Loop                            *
 *                                                                            *
 *============================================================================*/

int active_tasks;

void run_task(void(*task)(void), enum task_num num) {
  int mask = 1<<num;
  if(!(active_tasks & mask)) {
    active_tasks|=mask;
    a2perf_t starttime;
    perfmon_start(&starttime);
    (*task)();
    task_runtime[num] += perfmon_end(starttime);
    active_tasks&=~mask;
  }
}

void run_task_list(void) {
  // Generic FOMU operating system tasks
  run_task(tty_task, tty_task_active);
  run_task(tud_task,      tud_task_active);
  // TODO Separate morse task into generic LED and Touch with Morse Code modes
  run_task(morse_task,    led_task_active);
  //run_task(touch_task,    touch_task_active);  // TODO Need to write this
  //run_task(cli_task,      cli_task_active);    // TODO Need to write this
  run_task(keyboard_task, keyboard_task_active);
  run_task(video_task,    video_task_active);
  run_task(disk_task,     disk_task_active);
}

void yield(void) {
  static int last_active, ny, next_ny;  // not yet
  // Some tasks need to wait longer than a watchdog timeout. A secondary timer 
  // keeps track of a task that keeps yielding longer than allowed.
  watchdog_timer = 0; // system reboots if this is not cleared every so often
  if(active_tasks!=last_active) {
    ny = 0;
    next_ny = 2;
  }
  if(++ny==next_ny) {
    fprintf(persistence, "Yield: %02x %d\n", active_tasks, ny);
    // Use exponential decay in logging frequency
    next_ny*=2;
  }
  //if(rtc_read()>yield_timeout) {
    //fprintf(persistence, "Yield timeout: %02x %d\n", active_tasks, ny);
    //reboot();
  //}
  run_task_list();
}

int main(void) {
  init();
  watchdog_max = 5000;
  yield_max = 1000;
  while(1) {
    watchdog_timer = 0; // reboot if main loop not called every so often
    yield_timeout = rtc_read()+yield_max;
    run_task_list();
  }
  return 0;
}
