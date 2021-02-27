//
// disk.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _DISK_C_
#define _DISK_C_

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <a2fomu.h>
#include <tusb.h>
#include <disk.h>
#include <flash.h>
#include <generated/mem.h>
#include <crc.h>

#define FAST_PERFMON
#include <perfmon.h>

struct drive disk_drive[2];
enum disk_state external_disk_state;

struct track_cache cache_index[DISK_CACHE_LINES];
uint8_t track_cache[DISK_CACHE_LINES][TRACK_SIZE];
uint8_t cache_validated[DISK_CACHE_LINES];
struct partial_sector partial_sector;
uint32_t last_crc;
int disk_diagnostics;  // Debug and Performance flags

const char *disk_state_n[] = { "xDisconnected", "yNo-disk", "zIdle",
  "sSeeking", "rReading", "wWriting", };

// Physical to Logical sector address translation table
uint8_t interleave33_p2l[16] = { 0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
                                 0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF };
uint8_t interleave33_l2p[16] = { 0x0, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x1,
                                 0xE, 0xC, 0xA, 0x8, 0x6, 0x4, 0x2, 0xF };


// Returns location in cache if the requested LOGICAL sector is cached.
uint8_t *iscached(int drive, int track, int sector) {
  uint8_t *addr = NULL;
  #ifdef SIMULATION
    // Avoid USB transfers for faster startup during simulation
    addr = &track_cache[drive][sector*SECTOR_SIZE];
  #endif
  if((cache_index[drive].track==track) && (cache_index[drive].sector_valid &
        (1<<sector))) {
    addr = &track_cache[drive][sector*SECTOR_SIZE];
  } else {
    //printf("{t%d:%d;%c}", cache_index[drive].track, track,
    //    (int)((cache_index[drive].sector_valid & (1<<sector)) ? 'T':'F'));
  }
  return addr;
}

// Load a physical sector (linear on disk) into the cache. A negative sector
// number loads the entire track.
void cache_request(int drive, int track, int sector) {
  char command[12];
  // Verify a transfer is not already in progress
  if(external_disk_state!=ext_reading) {
    if(cache_index[drive].track!=track ||
        cache_index[drive].volume!=disk_drive[drive].volume) {
      // requested sector is on a track that is not on the cache.
      // Invalidate existing cache line and request new track.
      cache_index[drive].track = track;
      cache_index[drive].volume = disk_drive[drive].volume;
      cache_index[drive].sector_valid = 0;
    }
    if(sector<0) {
      // Load entire track request received (negative sector number) or
      if(drive==disk_external) {
        // Issue request if USB buffer space available for command
        if(tud_cdc_n_write_available(cdc_disk)>4) {
          snprintf(command, 12, "<%x\n", track);
          tud_cdc_n_write_str(cdc_disk, command);
          // Send command immediately rather than waiting for buffer to fill
          tud_cdc_n_write_flush(cdc_disk);
          printf("$%x", track);
          partial_sector.sector_start = NULL;
          external_disk_state = ext_reading;
        }
      }
    } else {
      // Load requested sector into track cache
      if(drive==disk_external) {
        // Issue request if USB buffer space available for command
        if(tud_cdc_n_write_available(cdc_disk)>5) {
          snprintf(command,12, "<%02x%x\n", track, sector);
          tud_cdc_n_write_str(cdc_disk, command);
          // Send command immediately rather than waiting for buffer to fill
          tud_cdc_n_write_flush(cdc_disk);
          printf("$%02x%x", track, sector);
          partial_sector.sector_start = NULL;
          external_disk_state = ext_reading;
        }
      }
    }
  }
}

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

int sector_checksum(uint8_t *sector) {
  int sum=0, byte;
  for(byte=0; byte<SECTOR_SIZE; byte++) {
    sum ^= sector[byte];
  }
  return sum;
}

void dump(const char *text, uint8_t *byte) {
    puts(text);
    int i;
    for(i=0; i<8; i++) {
      printf(" %02x%02x%02x%02x", byte[0], byte[1], byte[2], byte[3]);
      byte+=4;
    }
    putchar('\n');
}

// Fast hex conversion routine that assumes only ASCII characters '0'-'9' and
// 'a'-'f' or 'A'-'F' are present. Any other other character will be treated as
// a valid hex digit based on its four low order bits.
int get_hex(int c) {
  if(c>='0' && c<='9') {
    // ASCII digits (0x30-0x39) are less than letters (0x41-0x5A and 0x61-0x7A)
    c = c-'0';
  } else {
    // Convert to upper case to reduce the following code size
    c &= 0xDF;
    if(c>='A' && c<='F') {
      c = c-'A'+10;
    } else {
      //printf("E:hd '%c'\n", c);
      c = -1;
    }
  }
  return c;
}

// Interact with flash filesystem
void internal_disk_task(void) {
  if(disk_drive[disk_internal].wanted) {
  }
}

// Convert 256 byte data buffer to 342 bytes that are written to disk
// Pre-nibilization is the act of converting 265 bytes of data to 342 6-bit
// "nibbles" as used by Apple DOS. These are then encoded into a bit pattern
// that has no more than one pair of consecutive zeros as the Shuggart disk
// drive would often lose sync in the presence of consecutive zeros.
//
// This assembly code is from DOS 3.3 (c)Apple and is used for reference since
// no reference material adequately describes how the prenibilization routine
// is supposed to work.  0x102 bytes are encoded, where bytes 0 and 1 of the
// buffer are encoded twice due to 8-bit wrap-around.
/******************************************************************************
PRENIB16  LDX   #$0        ;START NBUF2 INDEX. CHANGED BY WOZ            $B800
          LDY   #2         ;START USER BUF INDEX. CHANGED BY WOZ.
PRENIB1   DEY   NEXT       ;USER BYTE.
          LDA   (BUF),Y
          LSR   A          ;SHIFT TWO BITS OF
          ROL   NBUF2,X    ;CURRENT USER BYTE                    ROL   $BC00,X
          LSR   A          ;INTO CURRENT NBUF2
          ROL   NBUF2,X    ;BYTE.
          STA   NBUF1,Y    ;(6 BITS LEFT).
          INX              ;FROM 0 TO $55.
          CPX   #$56       
          BCC   PRENIB1    ;BR IF NO WRAPAROUND.
          LDX   #0         ;RESET NBUF2 INDEX.
          TYA              ;USER BUF INDEX.
          BNE   PRENIB1    ;(DONE IF ZERO)
          LDX   #$55       ;NBUF2 IDX $55 TO 0.
PRENIB2   LDA   NBUF2,X   
          AND   #$3F       ;STRIP EACH BYTE
          STA   NBUF2,X    ;OF NBUF2 TO 6 BITS.
          DEX              
          BPL   PRENIB2    ;LOOP UNTIL X NEG.
          RTS              ;RETURN.
******************************************************************************/
uint8_t nbuf0[4];
uint8_t nbuf1[256];
uint8_t nbuf2[86];
uint8_t nbuf3[4];
const uint8_t data_prologue[4] = { 0xFF, 0xD5, 0xAA, 0xAD };
const uint8_t data_epilogue[4] = { 0xDE, 0xAA, 0xEB, 0xFF };

void nibblize(uint8_t *buf) {
  int a,x,y,yp;
  for(yp=0x102; yp>0; ) {
    for(x=0; x<0x56; x++) {
      yp--;
      y=yp&0xff; // Y' is used for the loop and down-converted to a byte
      a = buf[y];
      nbuf2[x] = (nbuf2[x]<<2) | ((a&2)>>1) | ((a&1)<<1);
      nbuf1[y] = a>>2;
    }
    //printf("Y%d", y);
    //dump("b2:", nbuf2);
  }
  for(x=0x55; x>=0; x--) {
    nbuf2[x] &= 0x3F;
  }
}

// Convert 342 bytes read from disk to 256 byte data buffer
// Parameters are destination buffer and the byte count expected, 0 for all.
/******************************************************************************
POSTNB16  LDY   #0         USER DATA BUF IDX.
POST1     LDX   #$56       INIT NBUF2 INDEX.
POST2     DEX   NBUF       IDX $55 TO $0.
          BMI   POST1      WRAPAROUND IF NEG.
          LDA   NBUF1,Y    
          LSR   NBUF2,X    SHIFT 2 BITS FROM
          ROL   A          CURRENT NBUF2 NIBL
          LSR   NBUF2,X    INTO CURRENT NBUF1
          ROL   A          NIBL.
          STA   (BUF),Y    BYTE OF USER DATA.
          INY   NEXT       USER BYTE.
          CPY   T0         DONE IF EQUAL T0.
          BNE   POST2
          RTS   RETURN.
******************************************************************************/
void denibblize(uint8_t *buf, int t0) {
  int x,y;
  if(t0==0) {
    t0 = 256;    // Byte count of 256 wrapped to 0 on 8-bit systems
  }
  x = 0x56;
  for(y=0; y<t0; y++) {
    x--;
    if(x<0) {
      x = 0x55;
    }
    buf[y] = (nbuf1[y]<<2) | ((nbuf2[x]&2)>>1) | ((nbuf2[x]&1)<<1);
    nbuf2[x] >>= 2;
  }
}

uint8_t nibl[] = {
  0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
  0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
  0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
  0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
  0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
  0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
  0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
  0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

enum head_state {
  head_inactive,
  head_header,
  head_read,
  head_write,
};

enum head_state sector_state;
char active_drive;
char active_track;
char active_sector;
short active_byte;
int active_READwrite;

// Direct read of ZP variables used by Disk ][ ROM (aka Integrated Woz Machine)
#define IWMDATAPTR (A2RAM_BASE+0x0026)
#define IWMBITS    (A2RAM_BASE+0x003C)
#define IWMSECTOR  (A2RAM_BASE+0x003D)
#define IWMTRKFND  (A2RAM_BASE+0x0040)
#define IWMTRACK   (A2RAM_BASE+0x0041)
// (37EC) B7EC Track number ($00 to $22).
// (37ED) B7ED Sector number ($00 to $0F).
#define RWTSTRACK  (A2RAM_BASE+0x37EC)
#define RWTSSECTOR (A2RAM_BASE+0x37ED)
#define CURTRK     (A2RAM_BASE+0x0478)
#define CSSTV      (A2RAM_BASE+0x002C)
#define SECT       (A2RAM_BASE+0x002D)
#define TRACK      (A2RAM_BASE+0x002E)
#define VOLUME     (A2RAM_BASE+0x002F)
#define RWTSVOLUME (A2RAM_BASE+0x37EB)

#define SECTOR_HEADER_SIZE 16
uint8_t hbuf[SECTOR_HEADER_SIZE] =
    { 0xFF, 0xFF, 0xD5, 0xAA, 0x96, 0,0, 0,0, 0,0, 0,0, 0xDE, 0xAA, 0xEB };

// R/W state machine - pass sector headers and sector data to read/write head
void disk_update_head(int drive) {
  /*--------------------------------------------------------------------------
  | Timing of the Disk Head
  |
  | The Disk II controller needs a byte every 32 cycles of the 1MHz 6502. This
  | is once every 384 instructions on the 12MHz Risc-V. As such, we assume ony
  | one byte will be sent per time though this function and that the OS may
  | run other tasks between bytes.
  +-------------------------------------------------------------------------*/
  static uint8_t prev;
  #ifndef SIMULATION
  #if 0
  // Read A2 memory to ensure data is being received as expected - Debug 
  static char last_iwmtrack, last_iwmsector; // last_iwmtrackfind;
  static int last_csstv;
  //static uint16_t last_iwmbyte;
  char iwmtrack, iwmsector; // iwmtrackfind;
  int csstv;  // Volume, Track, Sector, Checksum of sector currently under head
  //uint16_t iwmbyte;
  //iwmtrackfind = *(char*)IWMTRKFND;
  iwmtrack = *(char*)IWMTRACK;
  iwmsector = *(char*)IWMSECTOR;
  if(iwmtrack!=last_iwmtrack || iwmsector!=last_iwmsector) {
    printf("[h1:%02x%x]\n", iwmtrack, interleave33_p2l[(int)iwmsector]);
    last_iwmtrack=iwmtrack;
    last_iwmsector=iwmsector;
    //last_iwmbyte=iwmbyte;
    //last_iwmtrackfind=iwmtrackfind;
  }
  csstv = *(int*)CSSTV;
  if(csstv!=last_csstv) {
    printf("[V%x:T%x:S%x:C%x]", csstv>>24, (csstv>>16)&255, (csstv>>8)&255, csstv&255);
    last_csstv=csstv;
  }
  static char last_curtrk;
  static char last_yetanother_TRACK, last_sect;
  curtrk = *(char*)CURTRK;
  char yetanother_TRACK;
  yetanother_TRACK = *(char*)TRACK;
  char curtrk, sect;    // RWTS and Boot 2
  sect = *(char*)SECT;
  if(sect!=last_sect || curtrk!=last_curtrk) {
    char volume = *(char*)VOLUME;
    (void)last_yetanother_TRACK;
    printf("[v%x:t%x:%x:s%x]\n", volume, curtrk, yetanother_TRACK, sect);
    last_curtrk=curtrk;
    last_yetanother_TRACK = yetanother_TRACK;
    last_sect = sect;
  }
  #endif
  #if 0
  static char last_rwtstrack, last_rwtssector;
  char rwtstrack, rwtssector;                  // RWTS and Boot 2
  rwtstrack = *(char*)RWTSTRACK;
  rwtssector = *(char*)RWTSSECTOR;
  if(rwtstrack!=last_rwtstrack || rwtssector!=last_rwtssector) {
    printf("[h2:%02x%x]\n", rwtstrack, interleave33_p2l[(int)rwtssector]);
    last_rwtstrack=rwtstrack;
    last_rwtssector=rwtssector;
  }
  #endif
  #if 0
  static char last_iwmdataptr;
  char iwmdataptr;
  iwmdataptr = *(char*)IWMDATAPTR;
  if(iwmdataptr!=last_iwmdataptr) {
    printf("~%02x", iwmdataptr);
    last_iwmdataptr = iwmdataptr;
  }
  #endif
  #endif
  #ifdef DEBUG_DISK
  #ifndef SIMULATION
  static char last_iwmbits;
  char iwmbits;
  iwmbits = *(char*)IWMBITS;
  if(iwmbits!=last_iwmbits) {
    printf("[%02x]", iwmbits);
    last_iwmbits=iwmbits;
  }
  #endif
  #endif
  if(active_track!=disk_drive[drive].track2x/2) {
    // Arm moved to new track - discard any sector in progress
    sector_state = head_inactive;
  }  
  if(sector_state==head_inactive) {
    a2perf_t perftime;
    perfmon_start(&perftime);
    // Set state to current track of active drive
    // Advance sector - set to 0 if drive or track changed
    if(drive!=active_drive || active_track!=disk_drive[drive].track2x/2) {
      active_drive = drive;
      active_track = disk_drive[drive].track2x/2;
      // Use RWTS parameters as a hint of which sector to send first
      if(active_track==*(char*)RWTSTRACK) {
        active_sector = *(char*)RWTSSECTOR;
      }
    } else {
      //active_sector = (active_sector+1)&15;
    }
    active_byte = 0;
    /*------------------------------------------------------------------------
      | Sector Address Field:
      |
      | Each sector is preceeded with a 14 byte meta-data identifier so DOS can
      | identify the sector data. The byte D5 never appears anywhere on the disk
      | except as the first byte of a sector address header or data header.
      |
      | Prologue Volme Track Sectr Chksm Epilogue
      | D5 AA 96 XX YY XX YY XX YY XX YY DE AA EB
      |
      | Odd-Even Encoded
      | Data Byte: D7 D6 D5 D4 D3 D2 D1 D0
      |        XX:  1 D7  1 D5  1 D3  1 D1      (D>>1)|0xAA
      |        YY:  1 D6  1 D4  1 D2  1 D0      D|0xAA
      |
      | Checksum is XOR of the three data (Volume, Track, Sector) bytes
      +-----------------------------------------------------------------------*/
    int sector = interleave33_l2p[(int)active_sector];
    int checksum = disk_drive[drive].volume ^ active_track ^ sector;
    hbuf[5]  = (disk_drive[drive].volume>>1)|0xAA;
    hbuf[6]  = (disk_drive[drive].volume   )|0xAA;
    hbuf[7]  = (active_track >>1)|0xAA;
    hbuf[8]  = (active_track    )|0xAA;
    hbuf[9]  = (sector       >>1)|0xAA;
    hbuf[10] = (sector          )|0xAA;
    hbuf[11] = (checksum     >>1)|0xAA;
    hbuf[12] = (checksum        )|0xAA;
    // prenibblise sector in preparation for passing under head
    uint8_t *raw_sector = iscached(drive, active_track, active_sector);
    if(!raw_sector) {
      // Not in cache yet, wait for it to arrive
      cache_request(drive, active_track, active_sector);
      // Write a valid byte just in case. Send standard FF auto-sync preamble.
      apple2_diskdata_write(0xFF);
      return;
    }
    nibblize(raw_sector);
    //putchar('\n');
    //dump("Raw:  ", iscached(drive, active_track, active_sector));
    //dump("nbuf1:", nbuf1);
    //dump("nbuf2:", nbuf2);
    sector_state = head_header;
    a2perf_t delay = perfmon_end(perftime);
    if(delay.ms>2) {
      printf("{i%d.%u}", (int)delay.ms, (unsigned)delay.ck);
    }
  }
  if(sector_state==head_header) {
    //printf("h%d:%02x;", active_byte, hbuf[active_byte]);
    apple2_diskdata_write(hbuf[active_byte++]);
    if(active_byte>=SECTOR_HEADER_SIZE) {
      // XXX Show which sector is currently being fed
      //printf("[t%ds%d]", active_track, active_sector);   // Debug
      putchar('a'+active_sector);
      sector_state = head_read;
      active_byte = -4;
      prev = 0;
    }
  } else if(sector_state==head_read) {
    int data;
    if(active_byte<0) {
      // Prologue - 3 bytes
      data = data_prologue[active_byte+4];
    } else if(active_byte<86) {
      // nbuf2 - 86 bytes
      data = nibl[prev ^ nbuf2[85-active_byte]];
      prev = nbuf2[85-active_byte];
    } else if(active_byte<342) {
      // nbuf1 - 256 bytes
      data = nibl[prev ^ nbuf1[active_byte-86]];
      prev = nbuf1[active_byte-86];
    } else if(active_byte==342) {
      // Checksum
      data = nibl[nbuf1[active_byte-87]];
      //printf("{c:%d}", data);
    } else {
      // Epilogue - 3 bytes
      data = data_epilogue[active_byte-343];
      if(active_byte>=345) {
        sector_state=head_inactive;
//      if(active_track>2) {
//        // Advance to next sector as this one has completed
//        active_sector = (active_sector+1)&15;
//      } else {
//        // During boot, this is actually backwards. DOS is on first 3 tracks
          active_sector = (active_sector-1)&15;
//      }
        //printf("\n\n");
      }
    }
    #ifndef SIMULATION
    //printf(":%02x", data);
    #endif
    apple2_diskdata_write(data);
    active_byte++;
  } else {
    printf("E:duh%d\n", sector_state);
  }
}

#define debugfile stdout

// Eternal drive buffer management:
// Read commands and data from the second serial device into the track cache.
// TODO Enable transfer using Consistent Overhead Byte Stuffing (COBS) for
// 0.5% overhead rather than the current hex ascii with 150% overhead.
void external_disk_buffer_management(void) {
  int n;
  static enum disk_state old_disk_state;
  static int transfer_sector, retries;
  if(external_disk_state!=old_disk_state) {
    printf("%c", disk_state_n[external_disk_state][0]);
    old_disk_state = external_disk_state;
  }
  if(external_disk_state==ext_reading) {
    //putchar('-');  // Debug
    // filling cache
    if((n=tud_cdc_n_available(cdc_disk))!=0) {
      // data is available
      #ifdef DEBUG
         static uint16_t dolar_compress;
         if(++dolar_compress>4000) {
           putchar('$');  // Debug
           dolar_compress=0;
         }
      #endif
      //printf("{$%d}", n);
      retries = 0;
      uint8_t buf[CFG_TUD_CDC_RX_BUFSIZE+4];
      uint8_t *p = buf;
      int bits;
      int size=(CFG_TUD_CDC_RX_BUFSIZE<SECTOR_SIZE-partial_sector.current_byte)?
        CFG_TUD_CDC_RX_BUFSIZE:SECTOR_SIZE-partial_sector.current_byte;
      uint32_t count = tud_cdc_n_read(cdc_disk, buf, CFG_TUD_CDC_RX_BUFSIZE);
      buf[count+0] = '\0';
      //printf("{%d'%s'}", (int)count, (char*)buf);
      if(count>0) {
        //puts((char*)buf);     // Debug
      } else {
        printf("count=%d, size=%d\n", (int)count, (int)size);
      }
      while(p<buf+count) {
        //printf("{%c}", *p);
        if(*p=='#') {
          // Expect three character track/sector, hex encoded
          int track = (get_hex(p[1])<<4) | get_hex(p[2]);
          int sector = get_hex(p[3]);
          if(track<0 || sector<0) {
            printf("E:ph %d:%d\n", track, sector);
            // Only skip past the single token assuming more could follow
            //p++;
            //continue;
          }
          p+=4;
          // Verify complete sector, uninterrupted
          if(transfer_sector>=0) {
            // Something was in progress but did not complete
            //
            // Keep pointer to current sector and byte count. make pointer null
            // on success or error
            //
          }
          if((partial_sector.current_byte&0xFF)!=0) {
            printf("E:ps1 %x\n", partial_sector.current_byte);
          }
          if(track!=cache_index[disk_external].track) {
            // Arm moved after cache request was made. Ignore this sector.
            //printf("E:tr %d:%d\n", track, cache_index[disk_external].track);
          }
          // Write to new sector and clear any partial data
          partial_sector.sector_start=&track_cache[disk_external][sector<<8];
          partial_sector.current_sector = sector;
          partial_sector.current_byte = 0;
          partial_sector.half_byte = 0;
          //printf("sector %d\n", sector);
          continue;
        } else if(*p=='=') {
          // Expect two characters that are hex encoded
          int checksum = get_hex(p[1])<<4 | get_hex(p[2]);
          p+=3;
          // Verify complete sector, uninterrupted
          if(partial_sector.current_byte!=SECTOR_SIZE) {
            // Partial Sector Error
            printf("E:ps2 %x\n", partial_sector.current_byte);
          }
          int sector = partial_sector.current_sector;
          if(checksum==sector_checksum(partial_sector.sector_start)) {
            cache_index[disk_external].sector_valid |= (1<<sector);
            if(cache_index[disk_external].sector_valid==0xFFFF) {
              printf("Track %d cached\n", cache_index[disk_external].track);
              ////printf("sector %d:%d valid - %04x\n",
              //    cache_index[disk_external].track, sector,
              //    cache_index[disk_external].sector_valid); //Debug
            }
          } else {
            printf("E:cs %02x:%02x\n", checksum,
                sector_checksum(partial_sector.sector_start));
            dump("sector:", partial_sector.sector_start);
          }
          #if 0
          if(partial_sector.current_byte==0) {
            // whole track read, mark all sectors available if checksums ignored
            //cache_index[disk_external].sector_valid = 0xFFFF;
            external_disk_state = ext_inserted;
            printf("Track %d cached: %04x\n",
                cache_index[disk_external].track,
                cache_index[disk_external].sector_valid); //Debug
            printf("Checksums:");
            for(sector=0; sector<16; sector++) {
              // use raw sector number rather than interleaved sector
              printf(" %02x", sector_checksum(iscached(disk_external,
                    cache_index[disk_external].track, 0)+SECTOR_SIZE*sector));
            }
            putchar('\n');
          }
          #endif
          continue;
        } else if(*p=='*') {
          // All expected response has been received or lost.  Return to idle.
          // An optional CRC follows if a complete track was just transmitted.
          external_disk_state = ext_inserted;
          unsigned int crc = 0;
          while(++p<buf+count) {
            int c = get_hex(*p);
            if(c<0) {
              break;
            }
            crc = (crc<<4)|c;
          }
          // Save CRC for later. Only verifying when requested
          if(crc) {
            last_crc = crc;
          }
          if(cache_index[disk_external].sector_valid==0xFFFF) {
            // Complete track in cache that should match CRC
            if(crc==crc32(&track_cache[disk_external][0], TRACK_SIZE)) {
              cache_validated[disk_external]=1;
            } else {
              printf("E:crc %08x %08x\n", crc,
                  crc32(&track_cache[disk_external][0], TRACK_SIZE));
            }
          }
          continue;
        } else if(*p=='@') {
          disk_drive[disk_external].volume = (get_hex(p[1])<<4) | get_hex(p[2]);
          p+=4;
        } else if(*p==' ' || *p=='\n') {
          // Consume the space. If an odd number of hex characters was present
          // in number, we cannot infer leading zeros as this may have been a
          // string of digits that was missing one.
          p++;
          if(partial_sector.half_byte) {
            printf("E:hb %2x '%s'\n", partial_sector.half_byte, (char*)buf);
            partial_sector.half_byte = 0;
          }
          continue;
        }
        bits = get_hex(*p++);
        if(partial_sector.half_byte) {
          // high bit is silently shifted out leaving a zero that is true
          partial_sector.sector_start[partial_sector.current_byte++] =
              (partial_sector.half_byte<<4)|bits;
          partial_sector.half_byte = 0;
        } else {
          // Set high bit to ensure a zero nibble is seen as valid on next pass
          partial_sector.half_byte = bits|0x80;
        }
        //printf("nibble %d\n", bits);
      }
    } else {
      #if 0
      printf("{%s d%d;t%d;p%d;m%d,v%d;%x}",
          disk_state_n[external_disk_state],
          disk_external,
          disk_drive[disk_external].track2x/2,
          disk_drive[disk_external].phase,
          disk_drive[disk_external].motor,
          disk_drive[disk_external].volume,
          cache_index[disk_external].sector_valid);
      #endif
      //putchar('-');  // Debug
      // Timeout if data does not arrive after a few times through.
      // also set expected byte count to 0 allowing cache to fill
      if(retries++>1000) {
        // Watchdog timeout means we lost communication
        external_disk_state = ext_inserted;
        partial_sector.sector_start = NULL;
        // Do not interrupt current sector being read as whole sector must pass
        // under drive head before DOS will start looking for a new sector
        //sector_state = head_inactive;
        printf("W");
        retries = 0;
      }
    }
  } else if(external_disk_state==ext_writing) {
  } else if(external_disk_state==ext_seeking) {
  } else {
    // Check for commands such as disk inserted
    if(tud_cdc_n_available(cdc_disk)) {
      // connected and data is available
      uint8_t buf[65];
      uint32_t count = tud_cdc_n_read(cdc_disk, buf, 64); // sizeof(buf));
      if(count>64) {
        printf("Error: CDC size is %d\n", (int)count); // Debug
        count=0;
      }
      buf[count]=0;
      //if(buf[0]=='\n' || buf[0]=='\r') return; // Debug
      //printf("[in:%s]",buf); // Debug
      if(buf[0]=='@') {
        disk_drive[disk_external].volume = atoi((char*)&buf[1]);
        external_disk_state = ext_inserted;
        cache_index[disk_external].sector_valid = 0;        // Flush cache
        partial_sector.sector_start = NULL;
        sector_state = head_inactive;                 // Allow cache to fill
        printf("Inserted\n"); // Debug
      }
    }
    #if 0
    if(external_disk_state==ext_inserted) {
      if(disk_drive[disk_external].wanted) {
        if(!iscached(disk_external, disk_drive[disk_external].track2x/2,
              0) && !partial_sector.sector_start) {
          // Load requested track into buffer
          // Issue request to external program
          n = tud_cdc_n_write_available(cdc_disk);
          // Wait if no buffer space for command
          if(n>5) {
            char command[8];
            snprintf(command,8, "<%x\n", disk_drive[disk_external].track2x/2);
            tud_cdc_n_write_str(cdc_disk, command);
            tud_cdc_n_write_flush(cdc_disk);
            partial_sector.sector_start = NULL;
            partial_sector.current_sector = 0;
            partial_sector.current_byte = 0;
            partial_sector.half_byte = 0;
            external_disk_state = ext_reading;
            // Invalidate cache line
            cache_index[disk_external].track =
              disk_drive[disk_external].track2x/2;
            cache_index[disk_external].volume =
              disk_drive[disk_external].volume;
            cache_index[disk_external].sector_valid = 0;
            //printf("{r}"); // Debug;
          }
        }
      }
    }
    #endif
#if 0
    // Drive is idle - handle stderr
    if((stderr->device == a2dev_usb) && (cangetc(stderr))) {
      int c;
      n = tud_cdc_n_write_available(cdc_disk);
      while(n-->0 && (c=fgetc(stderr)) != EOF) {
        tud_cdc_n_write_char(cdc_disk, c);
      }
      tud_cdc_n_write_flush(cdc_disk);
    }
#endif
  }
}

// Read state from disk controller and move arm or place read data under head
void disk_controller_task(void) {
  int drive;
  int status=apple2_diskctrl_read();
  if(disk_diagnostics&disk_diag_controller) {
    static int last_status;
    if(status!=last_status) {
      // Print the status when anything changes
      //printf("&%02x",status);  // Debug
      if((status&0xf0)!=(last_status&0xf0)) {
        // Print the status when anything other than a track change occurs.
        printf("{fd%02x}",status);
      }
      last_status=status;
    }
  }
  drive = (status>>CSR_APPLE2_DISKCTRL_DRIVE_OFFSET)&1;
  disk_drive[drive].motor = (status>>CSR_APPLE2_DISKCTRL_MOTOR_OFFSET)&1;
  disk_drive[drive].wanted = (status>>CSR_APPLE2_DISKCTRL_WANTED_OFFSET)&1;
  if(status&0xF /*(1<<CSR_APPLE2_DISKCTRL_PHASE_OFFSET)*/) {
    int track2x = disk_drive[drive].track2x;
    switch(status) {
      case 1<<(CSR_APPLE2_DISKCTRL_PHASE_OFFSET+0):
        // Phase 0
        switch(disk_drive[drive].phase) {
          case 1: disk_drive[drive].track2x--; break;
          case 3: disk_drive[drive].track2x++; break;
          default: break;
        }
        disk_drive[drive].phase = 0;
        break;
      case 1<<(CSR_APPLE2_DISKCTRL_PHASE_OFFSET+1):
        // Phase 1
        switch(disk_drive[drive].phase) {
          case 2: disk_drive[drive].track2x--; break;
          case 0: disk_drive[drive].track2x++; break;
          default: break;
        }
        disk_drive[drive].phase = 1;
        break;
      case 1<<(CSR_APPLE2_DISKCTRL_PHASE_OFFSET+2):
        // Phase 2
        switch(disk_drive[drive].phase) {
          case 3: disk_drive[drive].track2x--; break;
          case 1: disk_drive[drive].track2x++; break;
          default: break;
        }
        disk_drive[drive].phase = 2;
        break;
      case 1<<(CSR_APPLE2_DISKCTRL_PHASE_OFFSET+3):
        // Phase 3
        switch(disk_drive[drive].phase) {
          case 0: disk_drive[drive].track2x--; break;
          case 2: disk_drive[drive].track2x++; break;
          default: break;
        }
        disk_drive[drive].phase = 3;
        break;
      default:
        // Do nothing while multiple phases are active
        break;
    }
    if(disk_diagnostics&disk_diag_track_change) {
      if(disk_drive[drive].track2x<track2x) {
        fputc('<', debugfile); 
      } else if(disk_drive[drive].track2x>track2x) {
        fputc('>', debugfile); 
      }
    }
    if(disk_drive[drive].track2x<0) {
      disk_drive[drive].track2x = 0;
      sector_state = head_inactive;  // Reset arm - seeking track 0
      active_sector = 0;             // Probably looking for sector 0 as well
    }
    // Disk has 35 tracks, 0-34
    if(disk_drive[drive].track2x>68) {
      disk_drive[drive].track2x = 68;
    }
  }
  if(disk_drive[drive].wanted) {
    if(!(status&(1<<CSR_APPLE2_DISKCTRL_PENDING_OFFSET))) {
      disk_update_head(drive);
      if(sector_state!=head_inactive) {
        a2perf_t perftime;
        perfmon_start(&perftime);
        a2time_t ms=system_ticks;
        timer0_update_value_write(1);
        unsigned ck = timer0_value_read();
        //uint64_t start = (ms+1)*(CONFIG_CLOCK_FREQUENCY/1000)-ck;
        int local_watchdog=0;
        // Speed up apple clock so it can read a sector in less than 1ms to
        // prevent the watchdog timer from tripping.
        uint32_t control = apple2_control_read();
        apple2_control_write(control&((1<<CSR_APPLE2_CONTROL_DIVISOR_SIZE)-1)<<
            CSR_APPLE2_CONTROL_DIVISOR_OFFSET);
        // Now send entire sector as long as each byte is read within a few
        // microseconds.
        while(local_watchdog++<5 && sector_state!=head_inactive) {
          //fputc('s'+local_watchdog, debugfile);
          status=apple2_diskctrl_read();
          if(status&(1<<CSR_APPLE2_DISKCTRL_PENDING_OFFSET)) {
            disk_update_head(drive);
            local_watchdog=0;
          }
        }
        // Restore clock to configured value
        apple2_control_write(control);
        a2time_t mse=system_ticks;
        timer0_update_value_write(1);
        unsigned cke = timer0_value_read();
        //uint64_t start = (ms+1)*(CONFIG_CLOCK_FREQUENCY/1000)-ck;
        a2perf_t delay = perfmon_end(perftime);
        if(mse-ms>10) {
          printf("{t%dms, %dus/12 -- w%d -- d%d.%u}\n",
              (int)(mse-ms), (int)(ck-cke), local_watchdog,
              (int)delay.ms, (unsigned)delay.ck);
        };
        if(delay.ms>0) {
          //printf("{ds%u}", (unsigned)delay);
        }
      }
    } else {
      //fputc('w', debugfile);
    }
  } else if(sector_state!=head_inactive) {
    // TODO Timing is terrible - use a static variable or interrupt
    //sector_state = head_inactive;
    //fputc('-', debugfile);
  }
}

void disk_task(void) {
  disk_controller_task();
  internal_disk_task();
  external_disk_buffer_management();
  flash_task();
}

void disk_init(void) {
  active_drive = disk_max;      // not internal or external
  // Clear any reading or writing status if disk is attached
  if(external_disk_state>ext_inserted) {
    external_disk_state = ext_inserted;
  }
  external_disk_state = ext_no_disk;
#ifdef SIMULATION
  external_disk_state = ext_inserted;
#endif
  // Clear cache by marking all lines as containing invalid tracks
  for(int i=0; i<disk_max; i++) {
    cache_index[i].track = 255;
  }
}

#endif /* _DISK_C_ */
