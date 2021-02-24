//╔═══════════════════════════════════════════════════════════════════════════╗
//║ cli.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton               ║
//║                                                                           ║
//║ This file is part of a2fomu which is released under the two clause BSD    ║
//║ licence.  See file LICENSE in the project root directory or visit the     ║
//║ project at https://github.com/elecbrick/a2fomu for full license details.  ║
//╚═══════════════════════════════════════════════════════════════════════════╝

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <a2fomu.h>
#include <cli.h>
#include <errno.h>
#include <flash.h>
#include <fsfat.h>
#include <disk.h>

#define ISR_TIME_TRACKING
#include <perfmon.h>

int cli_active;
char cli_escape = '\\';
char cli_execute = '\r';
char cli_command[CMD_BUFFER_LEN];
char *cmd_ptr;

int atox(const char *nptr) {
  int digit, number = 0;
  while(*nptr) {
    if(*nptr>='0' && *nptr<='9') {
      digit = *nptr-'0';
    } else if(*nptr>='a' && *nptr<='f') {
      digit = *nptr-'a'+10;
    } else if(*nptr>='A' && *nptr<='F') {
      digit = *nptr-'A'+10;
    } else {
      break;
    }
    nptr++;
    number = (number<<4)+digit;
  }
  return number;
}

void cli_bload(void) {
  char *filename = strtok(NULL, ", ");
  FILE *file;
  int address, size=INT_MAX;
  if(!filename) {
    printf("file?\n");
    return;
  }
  file = fopen(filename, "rb");
  if(!file) {
    printf("file: errno %d\n", errno);
    return;
  }
  char *string = strtok(NULL, ", ");
  if(!string) {
    printf("address?\n");
    fclose(file);
    return;
  }
  address = atox(string);
  // If the address is in the Risc-V boot ROM region, treat it as Apple RAM
  // and set the default size to be everything from that point to the end of
  // available RAM.
  if(address<A2RAM_SIZE) {
    address += A2RAM_BASE;
    size = A2RAM_SIZE-address;
  }
  // No checks are performed to ensure the operation system is not overwritten.
  // Care is advised.
  string = strtok(NULL, ", ");
  if(string) {
    size = atox(string);
  }
  // Avoid stdio and just copy the entire raw file, or the subset given by
  // size, to the destination address
  read(fileno(file), (void*)address, size);
  fclose(file);
}

//                        1  2  3  4  5  6  7  8  9  10 11 12
uint8_t clockM[] = {255, 11, 5, 3, 2, 3, 1, 1, 1, 1, 1, 1, 0};
// MHz * 10           0   1   2   3   4   5   6   7   8   9  10  11 12 13 14 15
uint8_t clockR[] = {120, 60, 40, 30, 24, 20, 17, 15, 13, 12, 11, 10, 9, 8, 8,7};

void cli_call(void) {
  char *token = strtok(NULL, ", ");
  unsigned addr = atox(token) & 0xfffffffc;
  (*(void(*)(void))addr)();
}

void cli_catalog(void) {
  DIR *root;
  struct dirent *file;
  root = opendir("/");
  if(!root) {
    printf("opendir: error %d\n", errno);
    return;
  }
  printf("ino   size name\n");
  while((file=readdir(root))) {
    //if(!file) {
    //  // Last file already processed
    //  break;
    //}
    if(file->attributes&e_volume) {
      // Ignore volume labels and long filenames.
      continue;
    }
    printf("%3d %6d ", (int)file->first_cluster, (int)file->file_size);
    for(int i=0; i<8; i++) {
      if(file->filename[i]==' ') {
        break;
      }
      putchar(file->filename[i]);
    }
    putchar('.');
    putchar(file->filename[8]);
    putchar(file->filename[9]);
    putchar(file->filename[10]);
    putchar('\n');
    yield();
  };
  closedir(root);
}

void cli_clock(void) {
  char *speed;
  unsigned int clock=0;
  speed = strtok(NULL, ", ");
  while(*speed>='0' && *speed<='9') {
    clock = (10*clock)+(*speed++-'0');
  }
  if(!*speed) {
    speed = strtok(NULL, ", ");
  }
  switch(*speed&0xDF) {
    case 0:   break;    // No units, use raw value
    case 'M': if(clock<sizeof(clockM))
                clock = clockM[clock];
              break;
    //case 'K': clock
    default:  printf("Units must be M or blank\n");
              return;
  }
  uint32_t control = apple2_control_read();
  control &= ((1<<CSR_APPLE2_CONTROL_DIVISOR_SIZE)-1)<<
      CSR_APPLE2_CONTROL_DIVISOR_OFFSET;
  control |= clock<<CSR_APPLE2_CONTROL_DIVISOR_OFFSET;
  apple2_control_write(control);
  // Convert raw clock delay cycles to MHz in fixed point
  if(clock>=sizeof(clockR)) {
    printf("Clock speed set to %d\n", clock);
    return;
  }
  int clockkHz = clockR[clock];  // temporary
  int clockMHz = clockkHz/10;
  clockkHz -= 10*clockMHz;
  printf("Clock speed set to %d.%dMHz\n", clockMHz, clockkHz);
}

void cli_dfu(void) {
#ifdef CSR_REBOOT_BASE
  reboot_ctrl_write(0xac);
#else
  printf("Reboot disabled in simulation but try this: \\x e0006000 ac");
#endif
}

void cli_echo(void) {
  printf("%s\n", strtok(NULL, ""));
}

void cli_exec(void) {
  exec(strtok(NULL, ""));
}

void cli_floppy(void) {
  disk_init();
}

void cli_hex(void) {
  int addr, data;
  char *token;
  token = strtok(NULL, ", ");
  //fprintf(stderr, "a[%s]", token);
  if(token) {
    // TODO FIXME XXX addr must be a multiple of 4 or else address exception
    // will occur. This is a silent patch that drops the 2 bits XXX FIXME TODO
    addr = atox(token) & 0xfffffffc;
    token = strtok(NULL, ", ");
    //fprintf(stderr, "d{%s}", token);
    // 1345216 Exception 4 at 100044ec referencing 10000012
    if(token) {
      data = atox(token);
      printf("poke 0x%08x, 0x%08x\n", addr, data);
      *(int*)addr = data;
    } else {
      data = *(int*)addr;
      printf("peek 0x%08x = 0x%08x\n", addr, data);
    }
  }
}

void cli_fp(void) {
  // TODO Reload ROM with Applesoft
}

void cli_go(void) {
#ifdef CSR_APPLE2_BASE
  // Take A2 out of reset
  uint32_t control = apple2_control_read();
  apple2_control_write(control & ~(1<<CSR_APPLE2_CONTROL_RESET_OFFSET));
#endif
}

void cli_int(void) {
  // TODO Reload ROM with Integer Basic
}

void cli_install(void) {
  // Copy a file into a special region of flash
  uint32_t start, size;
  // TODO buffer management is an issue in a 64kB system. For now, disable the
  // internal floppy drive as that will hang if it needs a seek anyway.
  char *src = (char*)track_cache[disk_internal];
  char *region = strtok(NULL, ", ");
  switch(region[0]) {
    case 'a': start = APPLESOFT_ROM_AREA; size=3*4096; break;
    case 'i': start = INT_BASIC_ROM_AREA; size=3*4096; break;
    case 'd': start = DOS33_PRELOAD_AREA; size=3*4096; break;
    case 'b': // Bootloader
    case 'l': // Library
    case 'f': start = FOBOOT_MAIN_LOADER; size=1*4096; break;
    default:  printf("Invalid region\n"); return;
  }
  char *filename = strtok(NULL, "");
  FILE *file = fopen(filename, "rb");
  if(!file) {
    printf("File error: %d\n", errno);
    size = 0;
  }
  // First, the file must be copied into RAM as the flash cannot be read while
  // being written to.
  while(size>0) {
    printf("Reading 4k\n");
    read(fileno(file), src, FLASHFS_SECTOR_SIZE);
    printf("Writing 4k\n");
    write_flash_unsafe(start, src, FLASHFS_SECTOR_SIZE);
    start += FLASHFS_SECTOR_SIZE;
    size -= FLASHFS_SECTOR_SIZE;
    int loop=0;
    while(flash_busy()) {
      yield();
      loop++;
    }
    printf("Flash updated after %d iterations\n", loop);
  }
  fclose(file);
}

void cli_morse(void) {
  // Force output to LED even if tty is connected
  stdout->device = a2dev_led;
}

// Print application error counters. The static assert takes no space in the
// executable but causes the compile to fail if a new counter is added and this
// file is not updated to display it.
static_assert(max_application_error==4, "Please display new debug counter");

void cli_overflow(void) {
  printf("tty_input_overflow    %d\n", debug_counter[tty_input_overflow]);
  printf("floppy_input_overflow %d\n", debug_counter[disk_input_overflow]);
  printf("video_output_overflow %d\n", debug_counter[video_output_overflow]);
  printf("usb_interrupt_lost    %d\n", debug_counter[usb_interrupt_lost]);
}

// Display the persistent log possibly showing what led up to a crash.
void cli_persistence(void) {
  dump_persistence();
}

// Place A2 in reset
void cli_reset(void) {
#ifdef CSR_APPLE2_BASE
  uint32_t control = apple2_control_read();
  apple2_control_write(control | (1<<CSR_APPLE2_CONTROL_RESET_OFFSET));
#endif
}

void cli_scroll(void) {
  char *token;
  token = strtok(NULL, ", ");
  if(token) {
    if(token[0]=='e') {
      scroll_mode = scroll_enhanced;
    } else {
      scroll_mode = scroll_standard;
    }
  } else {
    // Flip on/off
    scroll_mode = !scroll_mode;
  }
  if(scroll_mode==scroll_enhanced) {
    printf("Enhanced scroll\n");
  } else {
    printf("24 line scroll\n");
  }
}

void cli_sector(void) {
  extern uint8_t track_cache[2][4096];
  int drive, sector, i;
  char *token;
  uint8_t *p;
  token = strtok(NULL, ", ");
  if(token) {
    drive = 0;
    sector = atox(token);
    token = strtok(NULL, ", ");
    if(token) {
      drive = sector;
      sector = atox(token);
    }
    p = &track_cache[drive][sector*256];
    for(i=0; i<16; i++) {
      for(int j=0; j<4; j++) {
        printf("%02x%02x%02x%02x ", p[0], p[1], p[2], p[3]);
        p+=4;
      }
      putchar('\n');
    }
  }
}

const char *task_name[] = {
  "USB", "TTY", "LED", "Touch", "CLI", "Keybd", "Video", "Disk" };
static_assert(sizeof(task_name)/sizeof(task_name[0])==max_task,
    "Missing task from list of names");

typedef union lw {
  long long unsigned int q;
  unsigned int l[2];
} q2l;
  
void cli_times(void) {
  a2time_t total=0, pct;
  enum task_num task;
  for(task=0; task<max_task; task++) {
    total += task_runtime[task];
  }
  #ifdef ISR_TIME_TRACKING
  total += isr_runtime;
  #endif
  for(task=0; task<max_task; task++) {
    pct=(100*task_runtime[task]+50)/total; ///total; TODO compiles to ebreak
    printf("%5s %2u%% %u\n", task_name[task],
        (unsigned)pct, (unsigned)(task_runtime[task]));
  }
  #ifdef ISR_TIME_TRACKING
  printf("ISR   %2d%% %u\n", (int)((100*isr_runtime+50)/total), (int)(isr_runtime));
  #endif
  printf("Time  --- %d\n", (unsigned)rtc_read()/1000); // (int)(total>>32)
  printf("Total Interrupts: %d Time: %08x %08x\n", isr_count,
      ((q2l)total).l[1], ((q2l)total).l[0]);
}

void cli_upload(void) {
}

void cli_zero(void) {
  char *token;
  token = strtok(NULL, ", ");
  if(!token) {
    printf("A2 or Persistence?");
  } else if(token[0]=='p') {
    fprintf(stderr, "\nFILE %08x h=%d, t=%d, m=%d\n",
        (unsigned)persistence->buffer, persistence->head, persistence->tail,
        persistence->_max);
    persistence->head = 0;
    persistence->tail = 0;
  } else if(token[0]=='a') {
    // Zero out a memory block inside the Apple
    token = strtok(NULL, ",- ");
    if(!token) {
      printf("Addr?\n");
      return;
    }
    int start = atox(token);
    token = strtok(NULL, ",- ");
    if(!token) {
      printf("Range\n");
      return;
    }
    int end = atox(token);
    if(end<=start || end>=A2RAM_SIZE) {
      printf("Range?\n");
      return;
    }
    fprintf(persistence, "memset a=%08x, v=%d, s=%x\n",
        (unsigned)(A2RAM_BASE+start), 0, end-start);
#define USE_MEMSET
#ifdef USE_MEMSET
    memset((void*)(A2RAM_BASE+start), 0, end-start);
#else
    for( ; start>=end; start+=4) {
      *(uint32_t*)(A2RAM_BASE+start) = 0;
    }
#endif
  } else {
    printf("A2 or Persistence?");
  }
  return;
}

struct cli_command_entry {
  const char *name;
  void(*handler)(void);
};

struct cli_command_entry cli_command_list[] = {
  {"bload",     cli_bload},
  {"clock",     cli_clock},
  {"call",      cli_call},
  {"catalog",   cli_catalog},
  {"dfu",       cli_dfu},
  {"dir",       cli_catalog},
  {"echo",      cli_echo},
  {"exec",      cli_exec},
  {"floppy",    cli_floppy},
  {"fp",        cli_fp},
  {"go",        cli_go},
  {"hex",       cli_hex},
  {"int",       cli_int},
  {"install",   cli_install},
  {"ls",        cli_catalog},
  {"morse",     cli_morse},
  {"overflow",  cli_overflow},
  {"persistence", cli_persistence},
  {"reset",     cli_reset},
  {"scroll",    cli_scroll},
  {"sector",    cli_sector},
  {"times",     cli_times},
  {"upload",    cli_upload},
  {"x",         cli_hex},
  {"zero",      cli_zero},
};


// Determine command and jump to handler for that command
void cli_parse(char *command_line) {
  int i;
  char *command = strtok(command_line, " ");
  fprintf(persistence, "CLI>%s\n", command);
  for(i=0; i<(int)(sizeof(cli_command_list)/sizeof(cli_command_list[0])); i++) {
    if(!strncmp(command, cli_command_list[i].name, strlen(command))) {
      (cli_command_list[i].handler)();
      return;
    }
  }
  puts("Command?\n");
}

// Collect characters one at a time to build command in buffer
// Handle editing via backspace and (TODO) arrow keys.
int cli(char *in, int s) {
  char *end = in+s;
  char *p = in;
  int alerted = 0;
  //fputs("cli:", stderr);
  if(!cli_active) {
    puts(CLI_PROMPT);
    cli_active=1;
    cmd_ptr=cli_command;
    //fputs("active:", stderr);
    if(*p==cli_escape) {
      p++;
    }
  }
  while(p<end && *p!=cli_execute) {
    if(*p=='\b' || *p=='\x7f') {
      // Handle backspace
      if(cmd_ptr>cli_command) {
        //fputs("bs:", stderr);
        puts("\b \b");
        cmd_ptr--;
      } else {
        // Too many backspaces, buffer underrun - ring bell
        //fputs("ur:", stderr);
        if(!alerted) {
          putchar('\a');
          alerted = 1;
        }
      }
      p++;
      continue;
    }
    if(cmd_ptr<cli_command+CMD_BUFFER_LEN) {
      //fprintf(stderr, "%02X:", *p);
      putchar(*p);
      *cmd_ptr++ = *p++;
    } else {
      // Too many characters, buffer overrun - ring bell
      //fputs("or:", stderr);
      if(!alerted) {
        putchar('\a');
        alerted = 1;
      }
    }
  }

  // Complete command received - execute it
  if(*p==cli_execute) {
    putchar('\n');
    *cmd_ptr = '\0';
    p++;
    //fprintf(stderr, "parse(%s):", cli_command);
    cli_parse(cli_command);
    cli_active=0;
  }

  return p-in;
}

int exec(const char*script_name) {
  FILE *script;
  script = fopen(script_name, "r");
  if(script) {
    do {
      fgets(cli_command, sizeof(cli_command), script);
      if(cli_command[0]=='\0') {
        break;
      }
      // Print the command and execute it. If the command starts with @, do
      // not print the command and remove this character before passing to the
      // interpreter.
      int suppress=0;
      if(cli_command[0]=='@') {
        suppress = 1;
      } else {
        puts(cli_command);
      }
      // Yield control to allow the LED or USB task to transmit stdout.
      // Very useful for debug should something go awry.
      yield();
      cli_parse(cli_command+suppress);
      // Process stdout again.
      yield();
    } while(1);
  } else {
    if(errno==ENOENT) {
      printf("Failed to execute HELLO: errno %d\n", errno);
    }
  }
  return 0;
}
