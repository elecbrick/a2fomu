//
// morse.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <stdint.h>
#include <stdbool.h>
#include <rtc.h>
#include <rgb.h>
#include <irq.h>
#include <morse.h>
// Fomu Hardware
#include <a2fomu.h>
#include <generated/csr.h>
#include <stdio.h>

// The symbol EOF must be "an integer constant expression,
// with type int and a negative value"
//#include <stdio.h>
#define EOF (-1)
//#define NULL (0)

/*============================================================================*
 * Morse Code table                                                           *
 *============================================================================*
 * The most significant set bit is a start bit. This is used to determine     *
 * the number of valid symbols in a character. The Apple II only has 63       *
 * printable characters but 10 of these are not present in the ISO standard   *
 * for Morse code. These 10 characters are substituted with international     *
 * characters that have a mnemonic (Number, Dollar, Percent, Asterisk, Less   *
 * than, Greater than, Open brace, Close brace, Hat, Slash). The two non-     *
 * printing prosigns Start and Stop are used for spacing control. The Start   *
 * symbol is placed between consecutive spaces or after the last space before *
 * a pause.  End is used as Carriage Return plus Line Feed (Enter).           *
 *============================================================================*/

enum morse_space_times {
  SYMBOL_SPACE = 1,     // 1 dit time between dit and dah symbols
  LETTER_SPACE = 3,     // 3 dit times (+2) to terminate character
  WORD_SPACE   = 7,     // 7 dit times (+4) as blank space between words
};

#define MORSE_START     0x35
#define MORSE_END       0x45
#define MORSE_NEWLINE   0x15
#define MORSE_SPACE     0x01

#define MORSE_START_FIRST_BIT 0x10
#define MORSE_END_FIRST_BIT   0x20

// buffer must be a power of 2 to avoid extremely slow division
#define MORSE_BUF_SIZE 128

/*============================================================================*
 * ASCII to Morse Code translation table.                                     *
 *----------------------------------------------------------------------------*
 * This table has two parts. The first 64 entries are direct indexed by their *
 * ASCII code minus 32 to return the Morse Code pattern to transmit that      *
 * character. The rest of the table contains Morse Code patterns for the      *
 * non-printable characters on the Apple keyboard and other editing features. *
 *----------------------------------------------------------------------------*
 * The second use of the table is to search for a match to determine a key    *
 * stroke to be sent to the Apple when a given pattern is detected on the     *
 * touch pads. The entries after number 64 give an index into the non-        *
 * printables table that follows this one.                                    *
 *============================================================================*/
char morse_hw[72] = {
        0x01,   //    <space>
        0x6b,   // !  −·−·−−
        0x52,   // "  ·−··−·
        0x3b,   // #  −−·−−   Ń Number
        0x26,   // $  ··−−·   Ð Dollar
        0x2c,   // %  ·−−··   Þ Percent
        0x28,   // &  ·−···
        0x5e,   // '  ·−−−−·
        0x36,   // (  −·−−·
        0x6d,   // )  −·−−·−
        0x2d,   // *  ·−−·−   À Asterisk
        0x2a,   // +  ·−·−·
        0x73,   // ,  −−··−−
        0x61,   // -  −····−
        0x55,   // .  ·−·−·−
        0x32,   // /  −··−·
        0x3f,   // 0  −−−−−
        0x2f,   // 1  ·−−−−
        0x27,   // 2  ··−−−
        0x23,   // 3  ···−−
        0x21,   // 4  ····−
        0x20,   // 5  ·····
        0x30,   // 6  −····
        0x38,   // 7  −−···
        0x3c,   // 8  −−−··
        0x3e,   // 9  −−−−·
        0x78,   // :  −−−···
        0x6a,   // ;  −·−·−·
        0x29,   // <  ·−··−   Ł Less Than
        0x31,   // =  −···−
        0x3a,   // >  −−·−·   Ĝ Greater Than
        0x4c,   // ?  ··−−··
        0x5a,   // @  ·−−·−·
        0x05,   // A  ·−
        0x18,   // B  −···
        0x1a,   // C  −·−·
        0x0c,   // D  −··
        0x02,   // E  ·
        0x12,   // F  ··−·
        0x0e,   // G  −−·
        0x10,   // H  ····
        0x04,   // I  ··
        0x17,   // J  ·−−−
        0x0d,   // K  −·−
        0x14,   // L  ·−··
        0x07,   // M  −−
        0x06,   // N  −·
        0x0f,   // O  −−−
        0x16,   // P  ·−−·
        0x1d,   // Q  −−·−
        0x0a,   // R  ·−·
        0x08,   // S  ···
        0x03,   // T  −
        0x09,   // U  ··−
        0x11,   // V  ···−
        0x0b,   // W  ·−−
        0x19,   // X  −··−
        0x1b,   // Y  −·−−
        0x1c,   // Z  −−··
        0x1e,   // [  −−−·    Ó Open  - not on keyboard, rarely seen
        0x88,   // \  ···−··· Ś Slash - not on keyboard, rarely seen
        0x34,   // ]  −·−··   Ĉ Close - system prompt, regularly seen
        0x1f,   // ^  −−−−    Ĥ Hat
        0x4d,   // _  ··−−·−
/*----------------------------------------------------------------------------*
 * Entries following this location map to the non-printables table below.     *
 * These can be keyed in but will not be transmitted.                         *
 * Prosigns used for non-printing characters on keyboard and control flow.    *
 *----------------------------------------------------------------------------*/
        0x13,   // Ü  ··−−      <UT>                ^U Forward Arrow
        0x15,   // Æ  ·−·−      <AA> Newline:       ^M Enter
        0x22,   // Ŝ  ···−·     <SN> Understood:    ^S Pause
        0x24,   // É  ··−··     <EL>                ^[ Escape
        0x25,   //    ··−·−     <FT>                ^I Tab
        0x35,   //    −·−·−     <CT> Start:         ^X Cancel Input
        0x45,   //    ···−·−    <SK> Silencing Key: ^C Stop Execution
        0
/*----------------------------------------------------------------------------*
 * Codes beyond this point are ignored as they do not have a definition in    *
 * the Apple. In particular, there are 5 unused codes with 5 symbols which    *
 * are reserved here for completeness. Additionally, there is one accented    *
 * letter and a code to change receving mode. Any mode change must be handled *
 * as a special case so including it in a general lookup slows down the norm. *
 *----------------------------------------------------------------------------*
 *      0x2b,   //    ·−·−−                                                   *
 *      0x2e,   // Ĵ  ·−−−·                                                   *
 *      0x33,   //    −··−−                                                   *
 *      0x37,   //    −·−−−     <NO>                                          *
 *      0x39,   // Ż  −−··−                                                   *
 *      0x67,   //    −··−−−    <NJ> Shift to Wabun code                      *
 *      0x72,   // Ź  −−··−·                                                  *
 *----------------------------------------------------------------------------*
 * The final Prosigns are more than 7 signals each and thus overflow the      *
 * 8-bit char when the start bit is included. These must be handled specially *
 * so this final list is only a comment.                                      *
 *----------------------------------------------------------------------------*
 *      0x100,  //    ........  <HH> Error: Back Arrow (overflows 8-bit char) *
 *      0x278,  //    ···−−−··· <SOS> Only transmitted when life at stake     *
 *----------------------------------------------------------------------------*/
};

/*----------------------------------------------------------------------------*
 * Other Prosigns are defined and in common use but are NOT used because they *
 * are composed of printable characters and are conversation oriented         *
 *      0x28,   // &  ·−···     <AS> Wait                                     *
 *      0x0d,   // K  −·−       K    Invitation for any station to transmit   *
 *    0x18,0x0d,// BK −··· −·−  B K  Break (Morse abbreviation)               *
 *    0x1a,0x14,// CL −·−· ·−·· C L  Closing (Morse abbreviation)             *
 *      0x31,   // =  −···−     <BT> New Paragraph                            *
 *      0x2a,   // +  ·−·−·     <AR> New Page / Message Separator (Form Feed) *
 *      0x36,   // (  −·−−·     <KN> Invitation for named station to transmit *
 *----------------------------------------------------------------------------*/

/*============================================================================*
 * Morse Code to ASCII translation table part 2                               *
 *----------------------------------------------------------------------------*
 * Entries after the first 64 in the previous table map to an entry in this   *
 * table to return the key code to be used when the corresponding code is     *
 * received.                                                                  *
 *============================================================================*/
char morse_nonprintable[8] = {
        0x95,   // ^U ··−−          Forward Arrow
        '\r',   // ^M ·−·−          Enter
        0x93,   // ^S ···−·         Pause
        0x9B,   // ^[ ··−··         Escape
        '\t',   // ^I ··−·−         Tab
        0x98,   // ^X −·−·−         Cancel Input
        0x83,   // ^C ···−·−        Stop Execution
        // The last entry is not indexed by the table above but it pads word
        '\b',   // ^H ........      Backward Arrow
};


/*============================================================================*
 * Operating System Interface                                                 *
 *============================================================================*/
a2time_t key_down_start[morse_key_max];         // time when key press detected
unsigned char touch_debounce[morse_key_max];    // filter state updated by ISR
unsigned char touch[morse_key_max];             // binary state updated by task

/*============================================================================*
 * Interrupt Service Routine                                                  *
 *----------------------------------------------------------------------------*
 * Read the status of the touch pads. Debounce using an 8-bit finite impulse  *
 * response (FIR) filter and change the status if a hysterisis threshold is   *
 * passed. The formua: x=0.25s+0.75x is calculated representing 1.0 with 0xff *
 * and thus uses the coeficients 0x3F for 0.25 and 0xC0 for 0.75. This acts   *
 * like analog RC filter. Followed this, hysteresis will be added during the  *
 * main task loop emulating schmitt trigger.                                  *
 *============================================================================*/
void morse_isr(void) {
  int i, button_state;
  button_state = touch_i_read();        // LiteX/Fomu read button state
  for(i=0; i<morse_key_max; i++) {
    // new = 0.25*current + 0.75*previous
    touch_debounce[i] -= touch_debounce[i] >> 2;  // .75X = X-X/4
    if(button_state & (1<<i)) {
      // Add 1/4 of new value (1.0) if touch pad is active
      touch_debounce[i] += (0xFF>>2);
    }
  }
}

/*============================================================================*
 * Second part of key switch press/release detection. Examine the FIR results *
 * that are accumulated in the ISR above and apply hysteresis to the output   *
 * to detect when keys are pressed and released. Use the difference in time   *
 * between press and release to determine if it is a dit or a dah (dot or     *
 * dash).                                                                     *
 *============================================================================*/

void morse_input(int c) {
  int i;
  for(i=0; morse_hw[i]; i++) {
    if(c==morse_hw[i]) {
      if(i>=64) {
        fputc(morse_nonprintable[i-64], stdin);
      } else {
        fputc(i+' ', stdin);
      }
      return;
    }
  }
}

int max_dit_time = 400;
// Globally visible variable, technically static to the following function
int partial_char;

void morse_key_switch_task(void) {
  a2time_t now;
  int i, hold_time;
  now = rtc_read();
  // Finish key debounce and interpret key presses
  for(i=0; i<morse_key_max; i++) {
    if(i==1) {
      continue;
    }
    if((touch_debounce[i]<0x0F) && (touch[i]==0)){
      // Key press detected. Start timer on this key to determine dit or dah
      touch[i]=1;
      key_down_start[i] = now;
      fputc('.', stderr);
      //fprintf(stderr, "start %d - ", (int)now);      // FIXME Debug
    }
    if((touch_debounce[i]>0xF0) && touch[i]){
      // Key released. Determine if this was dit, dah or long
      touch[i]=0;
      hold_time = now-key_down_start[i];
      fputc('.', stderr);
      fprintf(stderr, "%d: %d %d %d\n\r", i, (int)key_down_start[i], (int)now, hold_time);      // FIXME Debug
      if(i==morse_key_dit) {
        if(!partial_char) {
          // Need to initialize start bit
          partial_char = 1;
        }
        if(partial_char<0) {
          // Overflow - too many signals so start bit would be lost
          // The easy way to set second most significant bit is to set all
          partial_char = -1;
        }
        partial_char = (partial_char<<1) | (hold_time>max_dit_time);
      } else if(i==morse_key_space) {
        if(hold_time>max_dit_time) {
          // Long press on space key is Return/Enter
          // Accept any character in progress before sending <CR>
          // Do not treat empty input buffer as space before newline
          if(partial_char>1) {
            morse_input(partial_char);
            // Clear morse switch buffer to just the start bit
            partial_char = 1;
          }
          // Now place <CR> in input buffer
          fputc('\r', stdin);
        } else {
          // Short press on space key accepts character in progress
          // Empty input buffer will be treated as a space
          morse_input(partial_char);
          // Clear morse switch buffer to just the start bit
          partial_char = 1;
        }
        // Clear 
        touch[i]=1;
      } else if(i==morse_key_error) {
        if(hold_time>max_dit_time) {
          // Holding the Error key is a short-cut for <CT> Cancel Input
          // Officially, a Morse Code transmission would be <HH> <CT> but since
          // the <CT> is never expected in this application, the <HH> is
          // unnecessary and can be skipped. There is no harm if the operator
          // chooses to send the official sequence, it is just not required.
          fputc(0x98, stdin);
        } else {
          // Short press of Error key clears any partial input if present or
          // issues a backspace if no partial character in progress.
          if(partial_char==1) {
            fputc('\b', stdin);
          }
        }
        // Always clobber partial input whether or not some other edit occurs
        partial_char = 1;
      }
    }
  }
}

/*============================================================================*
 * Non-blocking STDIO Interaction                                             *
 * Monitor stdout and stderr for output to LED. stderr has precidence.        *
 * Monitor touchpads and place any detected symbols in stdin                  *
 *============================================================================*/
static inline int dequeue(void) {
  int code = 0;
  #if 0
  if((stderr->device == a2dev_led) && (cangetc(stderr))) {
    code = fgetc(stderr);
  } else
  #endif
  if((stdout->device == a2dev_led) && (cangetc(stdout))) {
    code = fgetc(stdout);
  }
  if((code == '\r') || (code == '\n')) {
    code = MORSE_END;
  } else if(code) {
    if(code > 96) {
      // convert lower case to upper case: if(isupper(c)) tolower(c)
      code -= 32;
    }
    code = morse_hw[(code-32) & 63];
  }
  return code;
}


/*============================================================================*
 * Timing Routines                                                            *
 * Prerequisite: assumes 1ms timer is running                                 *
 *============================================================================*/
#define CLOCK_NS 1000000

/* Configuration - volatile to keep them out of read-only memory and able to
 * be changed at runtime                                                      */
int rgb_morse_on;
int rgb_morse_off;
long dit_duration;

/* Class Variables */
a2time_t next_event_time = 0;
int current_character = 0;
int previous_character = 0;
int morse_on = false;


static inline void set_timer(int dit_times) {
  next_event_time = rtc_read() + dit_times * dit_duration;
}

static inline int timer_expired(void) {
  a2time_t now;
  now = rtc_read();
  return now >= next_event_time;
}

static void set_morse(bool on, int t) {
  rgb_raw_write(on ? rgb_morse_on : rgb_morse_off);
  morse_on = on;
  set_timer(t);
}

/*============================================================================*
 * TX State                                                                   *
 *============================================================================*
 * Idle: do nothing then                                                      *
 *     received <space> -> Start                                              *
 *     received any other character -> Character                              *
 *     nothing pending -> Idle                                                *
 * Start: transmit start then                                                 *
 *     always -> Space                                                        *
 * Character: transmit character then letter space then                       *
 *     received <space> -> Space                                              *
 *     received any other character -> Character                              *
 *     nothing pending -> Idle                                                *
 * Space: transmit space then                                                 *
 *     received <space> -> Start                                              *
 *     received any other character -> Character                              *
 *     nothing pending -> End                                                 *
 * Stop: transmit start then                                                  *
 *     always -> Idle                                                         *
 *============================================================================*/

enum morse_state {
  IDLE,
  START,
  STOP,
  CHARACTER,
  SPACE,
};

enum morse_state morse_state;
int current_pattern;
int current_bit;

void morse_init(void) {
  rgb_morse_off = 0;            // Black
  rgb_morse_on = 7;             // White
  dit_duration = 300;           // 30 ms is standard speed
  max_dit_time = 400;
#ifdef SIMULATION
  dit_duration = 1;             // 0.1 ms, not humanly detectable
#else
  //dit_duration = 100;         // 30 ms, standard speed
#endif

  // Turn on RGB block and current enable, enable led control enable LED
  // driver, set 250 Hz mode, enable quick stop, set clock to 12 MHz/64 kHz-1.
  rgb_init();
  rtc_init();

  // Activate override allowing raw control.
  // Disable breathing so LED is steady on or steady off.
  // The three LSB turn on the current enable and enabling PWM.
  rgb_ctrl_write(
      (1 << CSR_RGB_CTRL_EXE_OFFSET) |
      (1 << CSR_RGB_CTRL_CURREN_OFFSET) |
      (1 << CSR_RGB_CTRL_RGBLEDEN_OFFSET) |
      (1 << CSR_RGB_CTRL_RRAW_OFFSET) |
      (1 << CSR_RGB_CTRL_GRAW_OFFSET) |
      (1 << CSR_RGB_CTRL_BRAW_OFFSET));

  rgb_raw_write(rgb_morse_off);

  // Initialize touch pads
  touch_oe_write(2);
  touch_o_write(0);
}

int morse_isidle(void) {
  return morse_state == IDLE;
}

/*============================================================================*
 * Morse Code TX                                                              *
 *----------------------------------------------------------------------------*
 * Called when the transmit timer expires to flip the transmit LED on/off as  *
 * required. Also handles pulling characters from the transmit buffers.       *
 *============================================================================*/
void morse_transmit_task(void) {
  if (morse_state == IDLE) {
    current_pattern = dequeue();
    if (!current_pattern) {
      return;
    }
    morse_state = CHARACTER;
    if (current_pattern == MORSE_SPACE) {
      morse_state = START;
      current_pattern = MORSE_START;
      // Begin with space after start bit
      current_bit = MORSE_START_FIRST_BIT-1;
    } else {
      // advance to start bit
      current_bit = 0x80;
      while(!(current_pattern&current_bit)) {
        current_bit>>=1;
      }
    }
    // Wait until start of next period to prevent partial bit time
    set_morse(false, 1);
    return;
  }

  // If bit was just transmitted, turn off light and wait appropriately
  if (morse_on) {
    int t;
    t = SYMBOL_SPACE;
    if (current_bit == 1) {
      // Last bit was sent. Turn off and wait appropriate time
      t = LETTER_SPACE;
      if (morse_state == START) {
        t = WORD_SPACE;
        morse_state = SPACE;
      } else if (morse_state == STOP) {
        t = WORD_SPACE;
        morse_state = IDLE;
      }
    }
    set_morse(false, t);
    return;
  }

  // Intra-character break just completed - send next bit
  current_bit>>=1;
  if (current_bit) {
    set_morse(true, (current_pattern&current_bit) ? 3 : 1);
    return;
  }

  // Get next character to send
  current_pattern = dequeue();
  if (!current_pattern) {
    // No more to send - go to idle unless last character was a space
    // To prevent a small break from being interpreted as space, go STOP
    // whether or not the last character was a space
    if (morse_state == SPACE) {
      morse_state = STOP;
      current_pattern = MORSE_END;
    } else {
      // Wait longer than a word space before restarting to prevent spaces
      set_timer(2*WORD_SPACE+1);
      return;
    }
  }

  if (current_pattern == MORSE_SPACE) {
    if (morse_state == SPACE) {
      morse_state = START;
      current_pattern = MORSE_START;
    } else {
      // inter-character space just sent so wait difference
      morse_state = SPACE;
      set_morse(false, WORD_SPACE-LETTER_SPACE);
      return;
    }
  }

  // advance to start bit
  current_bit = 0x80;
  while(!(current_pattern&current_bit)) {
    current_bit>>=1;
  }
  // skip start bit
  current_bit>>=1;
  set_morse(true, (current_pattern&current_bit) ? 3 : 1);
}


/*============================================================================*
 * Main Task                                                                  *
 *----------------------------------------------------------------------------*
 * Called by the scheduler periodically to read the key switches and update   *
 * the LED status.                                                            *
 *============================================================================*/
void morse_task(void) {
  // FIXME TODO XXX morse_key_switch_task();
  if (timer_expired()) {
    morse_transmit_task();
  }
}


#if 0
/*============================================================================*
 * Debug routine for a restricted environment that only has one LED for status*
 * and no other means of giving output to the user.                           *
 *----------------------------------------------------------------------------*
 * Repeatedly print an error code. Error handler that does not return.        *
 * Reinitializes timer and interrupt handler for guaranteed operation.        *
 *============================================================================*/
void morse_error(int c) {
  morse_init(7,0,300);
  while(true) {
    (void)morse_putchar(c);
    while(!morse_isidle())
      ;
    set_timer(WORD_SPACE);
  }
}
#endif
