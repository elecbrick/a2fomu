//
// flash.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// Erase and program one SPI Flash sector (one A2Fomu track) at a time.
// An Apple II disk is 35 tracks each of which is 4kB containing 16 sectors
// of 256 bytes.  Coincidentally, the SPI Flash on FOMU is programmed one 256
// byte page at a time and erased one 4kB sector at a time.

#include <generated/csr.h>
#include <generated/mem.h>
#include <spi.h>
#include <crc.h>
#include <rtc.h>
#include <disk.h>
#include <flash.h>
#include <string.h>
#include <stdio.h>
#include <a2fomu.h>

#ifndef DEBUG
#define DEBUG
#endif
#ifdef DEBUG
#define debug fprintf
#define NOT_WRITTEN_RC size
#else
#define debug(x, ...)
#define NOT_WRITTEN_RC (-1)
#endif

static volatile enum flash_state flash_state;
static volatile int allow_unsafe;
int flash_page_mask;

int flash_dst_addr;
int flash_update_size;
int flash_bytes_remaining;
uint8_t *flash_src_ptr;
uint16_t pages_to_program;   // bitmask of 16 pages per sector
uint16_t flash_next_page;           // page number 0-15

static void error(const char *msg) {
  fprintf(persistence, "Ef:%s", msg);
}

// Wrapper for litex SPI flash module that turns int into named parameter
static void flash_mode(enum spi_flash_mode mode) {
  lxspi_bitbang_en_write(mode);
}

// Flashing a valid Booster allows auto-start of A2Fomu from standard FOMU.
// Keeping this here only to acknowledge the sector Booster resides in.
void replace_booster(void) {
  flash_dst_addr = 0x5a000;
}


/*============================================================================
 * flash_busy - User API
 * Return state of flash write controller.
 * TODO Replace with read lock and write lock.
 *===========================================================================*/
int flash_busy(void) {
return flash_state!=FLASH_USER_MODE;
}


/*============================================================================
 * read_flash - User API
 * Like memcpy but the source is an offset within in flash, not the
 * global address space. Copy size bytes from src address to dst offset.
 *===========================================================================*/
int read_flash(void *dst, int src, int size) {
  if(flash_state!=FLASH_USER_MODE) {
    error("active");
    return 0;
  }
  // Flash is memory mapped so we can just read directly
  memcpy(dst, (const char*)(SPIFLASH_BASE+src), size);
  return size;
}


/*============================================================================
 * write_flash - User API
 * Like memcpy but the destination is an offset within in flash, not the
 * global address space. Copy size bytes from src address to dst offset.
 *===========================================================================*/
int write_flash(int dst, const void *src, int size) {
  static int ny, next_ny=1;
  // This will be called repeatedly until entire write has completed. It will
  // not be completed initially as several miliseconds are required in the best
  // case and will be many seconds for large transfers.
  if(flash_src_ptr==src && flash_dst_addr==dst && flash_update_size==size) {
    if(flash_state!=FLASH_USER_MODE) {
      // Same transfer checking if completed yet. Check if status can be updated
      if(++ny==next_ny) {
        debug(persistence, "@N%d", ny);
        // Use exponential decay in logging frequency
        next_ny*=2;
      }
      // TinyUSB assumes an OS will force a yield and schedule other tasks. This
      // application does not have multiple threads so control is explicitly
      // given back to the scheduler now and then returned to the USB process
      // to handle further communication and keep the host link alive.
      yield();
      return 0;
    } else {
      // Transfer complete
      debug(persistence, "@K%d", ny);
      ny = 0;
      next_ny = 1;
      return size;
    }
  }
  if(flash_state!=FLASH_USER_MODE) {
    // A new request has arrived while another operation is still in progress.
    // This request must be postponed. Respond with 0 so application repeats
    // request at a later time.
    error("active");
    return 0;
  }
  debug(persistence, "@Q%X:%X", (unsigned)dst, (unsigned)size);
  // Verify then store parameters for task. The task lets this be a reentrant
  // routine allowing other tasks to function while the flash takes several
  // miliseconds to erase or program itself.
  if(dst<FIRST_SAFE_ADDRESS) {
    // Unsafe address - user could be disabling or even bricking the device
    error("unsafe");
    //printf("Parameters: dst %X, size %X\n", (unsigned)dst, (unsigned)size);
    return NOT_WRITTEN_RC;
  }
  if(size>ERASE_SECTOR_SIZE || dst+size > SPIFLASH_SIZE) {
    // Too big - single sector is only size supported due to cache size
    error("toobig");
    return NOT_WRITTEN_RC;
  }
  if((dst&(ERASE_SECTOR_SIZE-1))+size>ERASE_SECTOR_SIZE) {
    // Sector boundary crossing - all changes must be within a single sector
    error("sector");
    //printf("Parameters: dst %X, size %X\n", (unsigned)dst, (unsigned)size);
    return NOT_WRITTEN_RC;
  }

  // Determine whether erase is necessary and which pages need to be updated.
  // Avoiding unnecessary erasing and programming may extend device lifespan.
  // First scan entire sector to determine whether or not erase is needed.
  // This must be done first as the programming check needs to know whether to
  // compare with existing value or erased value.
  int needs_erase = 0;
  const uint8_t *from = src;
  uint8_t *to = (uint8_t*)(SPIFLASH_BASE+dst);
  int byte, page;
  for(byte=0; byte<size; byte++) {
    // Erase is needed to convert bits from 0 to 1 but not from 1 to 0 so
    // existing data can have unneccesary bit set but if it has any bits that
    // need to be flipped to 1, the entire sector needs to be erased and set 
    // to all 1s.
    if((*from&*to)!=*from) {
      needs_erase = 1;
      break;
    }
    from++;
    to++;
  }
  if(size!=ERASE_SECTOR_SIZE) {
    // Check for exceptions that are needed when less than a full sector is
    // being presented for programming.
    if((dst&(ERASE_SECTOR_SIZE-1))==0 && size==FATFS_SECTOR_SIZE) {
      // Assume a file system will send an entire cluster (flash sector)
      // immediately following the first sector (two flash pages). Allow the
      // erase of a sector when 512 a byte write request comes for the first
      // page of a sector. Verify the rest of the sector is already erased.
      for(byte=size; byte<ERASE_SECTOR_SIZE; byte++) {
        if(*to!=0xFF) {
          needs_erase = 1;
          break;
        }
        to++;
      }
    } else if(needs_erase) {
      if((dst&(ERASE_SECTOR_SIZE-1))!=0) {
        // Erase is needed but destination is not on a sector boundary
        error("neederase");
        //printf("Parameters: dst %X, size %X\n", (unsigned)dst, (unsigned)size);
        return NOT_WRITTEN_RC;
      } else {
        //printf("Warning: Erasing sector %X but only programming %X bytes\n",
            //(unsigned)dst, (unsigned)size);
      }
    } else {
      // Erase not needed. Continue with programming.
    }
  }
  // Now determine which pages need to be programmed.
  pages_to_program = 0;
  from = src;
  to = (uint8_t*)(SPIFLASH_BASE+dst);
  if(((dst&(PROGRAM_PAGE_SIZE-1))!=0) || ((size&(PROGRAM_PAGE_SIZE-1))!=0)) {
    // Assume all data is to be programmed if starting or ending address is not
    // on a page boundary.
    pages_to_program = (1<<ERASE_SECTOR_SIZE/PROGRAM_PAGE_SIZE)-1;
  } else {
    // See which pages already have the required information and thus do not
    // need to be programmed. This saves time and lengthens the flash lifespan.
    for(page=0; page<size/PROGRAM_PAGE_SIZE; page++) {
      for(byte=0; byte<PROGRAM_PAGE_SIZE; byte++) {
        if(*from!=(needs_erase?0xFF:*to)) {
          pages_to_program |= 1<<page;
          break;
        }
      }
    }
  }

  // All safety checks pass. Store parameters and start the long update cycle
  flash_src_ptr = (uint8_t *)src;
  flash_dst_addr = dst;
  flash_update_size = size;
  flash_bytes_remaining = size;
  flash_next_page = 0;
  //printf("Flashing src %X, page %X, size %X, mask %02x\n", (unsigned)src,
      //(unsigned)((dst-FIRST_SAFE_ADDRESS)/PROGRAM_PAGE_SIZE), (unsigned)size,
      //(unsigned)pages_to_program);
  if(needs_erase) {
    flash_state = FLASH_ERASE_TRACK;
    flash_mode(FLASH_WRITE_ENABLED);
  } else if(pages_to_program) {
    flash_state = FLASH_WRITE_SECTOR;
    flash_mode(FLASH_WRITE_ENABLED);
  } else {
    // Signal programming of this sector is complete (as it was unnecessary).
    //printf("Sector at 0x%X already has requested content\n", dst);
    return size;
  }
  // Return bytes written so far (none).
  return 0;
}

/*============================================================================*
 * write_flash_unsafe - Internal function to be called by CLI
 * Replace reserved sections in flash with content that has been verified
 * by the CLI or other checking routines.
 *============================================================================*/
int write_flash_unsafe(int dst, const void *src, int size) {
  if(flash_state!=FLASH_USER_MODE) {
    error("active");
    return 0;
  }
  debug(persistence, "@Y%X:%X", (unsigned)dst, (unsigned)size);
  if(size>ERASE_SECTOR_SIZE || dst+size > SPIFLASH_SIZE) {
    error("toobig");
    return NOT_WRITTEN_RC;
  }
  if((dst&(ERASE_SECTOR_SIZE-1))+size>ERASE_SECTOR_SIZE) {
    error("sector");
    return NOT_WRITTEN_RC;
  }
  if(dst<0x1A000) {
    error("failsafe");
    return NOT_WRITTEN_RC;
  }

  int needs_erase = 0;
  const uint8_t *from = src;
  uint8_t *to = (uint8_t*)(SPIFLASH_BASE+dst);
  int byte, page;
  for(byte=0; byte<size; byte++) {
    if((*from&*to)!=*from) {
      needs_erase = 1;
      break;
    }
    from++;
    to++;
  }
  pages_to_program = 0;
  from = src;
  to = (uint8_t*)(SPIFLASH_BASE+dst);
  if(((dst&(PROGRAM_PAGE_SIZE-1))!=0) || ((size&(PROGRAM_PAGE_SIZE-1))!=0)) {
    pages_to_program = (1<<ERASE_SECTOR_SIZE/PROGRAM_PAGE_SIZE)-1;
  } else {
    for(page=0; page<size/PROGRAM_PAGE_SIZE; page++) {
      for(byte=0; byte<PROGRAM_PAGE_SIZE; byte++) {
        if(*from!=(needs_erase?0xFF:*to)) {
          pages_to_program |= 1<<page;
          break;
        }
      }
    }
  }
  printf("Flashing src %X, dst %X, size %X, mask %02x\n", (unsigned)src,
      (unsigned)dst, (unsigned)size, (unsigned)pages_to_program);

  // All safety checks pass. Store parameters and start the long update cycle
  allow_unsafe = 1;
  flash_src_ptr = (uint8_t *)src;
  flash_dst_addr = dst;
  flash_update_size = size;
  flash_bytes_remaining = size;
  flash_next_page = 0;
  if(needs_erase) {
    flash_state = FLASH_ERASE_TRACK;
    flash_mode(FLASH_WRITE_ENABLED);
  } else if(pages_to_program) {
    flash_state = FLASH_WRITE_SECTOR;
    flash_mode(FLASH_WRITE_ENABLED);
  } else {
    return size;
  }
  return 0;
}

/*============================================================================*
 * flash_task - Internal function called by operating system periodically.
 * Write to flash memory with content that has been verified by the write_flash
 * or write_flash_unsafe routines which has already verified the content is
 * valid for the given location and will not brick the device.
 *============================================================================*/
void flash_task(void) {
  int dst, size;
  unsigned char* src;
  switch(flash_state) {
    case FLASH_USER_MODE:
      // Nothing to do. Device is memory mapped and read access is unresticted.
      break;

    case FLASH_ERASE_TRACK:
      // Erase one 4096-byte block
      if(spiIsBusy()) {          // verify transition to async success.
        // Previous operation not finished yet. Let OS run other tasks.
        break;
      }
      if(flash_dst_addr<FIRST_SAFE_ADDRESS && !allow_unsafe) {
        // Unsafe address - programming error that could brick the device
        // Security check that should never happen.
        error("unsafe");
        flash_state = FLASH_VERIFY_TRACK;
        return;
      }
      spiBeginErase4(flash_dst_addr);  // 30ms typical
      //printf("@ERASE-%X@", (unsigned)flash_dst_addr);
      debug(persistence, "@E");
      flash_state = FLASH_WRITE_SECTOR;
      break;

    case FLASH_WRITE_SECTOR:
      // Program next 256-byte page
      if(spiIsBusy()) {          // verify transition to async success.
        // Previous operation not finished yet. Let OS run other tasks.
        break;
      }
      if(flash_dst_addr<FIRST_SAFE_ADDRESS && !allow_unsafe) {
        // Unsafe address - programming error that could brick the device
        // Security check that should never happen.
        error("unsafe");
        flash_state = FLASH_VERIFY_TRACK;
        return;
      }
      while(!(pages_to_program&(1<<flash_next_page))) {
        if(!pages_to_program) {
          flash_state = FLASH_VERIFY_TRACK;
          return;
        }
        flash_next_page++;
      }
      pages_to_program&=~(1<<flash_next_page);
      dst = flash_dst_addr+flash_next_page*PROGRAM_PAGE_SIZE;
      src = flash_src_ptr+flash_next_page*PROGRAM_PAGE_SIZE;
      size = PROGRAM_PAGE_SIZE;
      if(dst&(PROGRAM_PAGE_SIZE-1)) {
        // Destination is not on a page boundary. Addresses need to be adjusted
        // as well as the size of the first and last pages.
        if(flash_next_page==0) {
          // Adjust size if first write is not on page boundary.
          size = PROGRAM_PAGE_SIZE-(dst&(PROGRAM_PAGE_SIZE-1));
        } else {
          // Adjust src and dst to the previous page boundary
          dst &= ~(PROGRAM_PAGE_SIZE-1);
          // Sigh - the syntax needed to align a pointer
          src = (unsigned char*)((int)src&~(PROGRAM_PAGE_SIZE-1)); 
        }
      }
      // The last page may not be full. Adjust size if less than a full page.
      if(size>flash_bytes_remaining) {
        size = flash_bytes_remaining;
      }
      //printf("@WRITE-%X@", (unsigned)dst);
      debug(persistence, "@W%d",
          (unsigned)((dst-FIRST_SAFE_ADDRESS)/PROGRAM_PAGE_SIZE));
      // All checks complete and address adjusted for partial pages. Write it!
      spiBeginWrite(dst, src, size);
      flash_bytes_remaining -= size;
      if(flash_bytes_remaining==0) {
        // All data written whether or not it was a complete sector.
        flash_state = FLASH_VERIFY_TRACK;
        // Reenable all safety checks
        allow_unsafe = 0;
      }
      break;

    case FLASH_VERIFY_TRACK:
      // Verify sector was written correctly
      // Wait until all write activity has finished.
      if(spiIsBusy()) {
        // Previous operation not finished yet. Let OS run other tasks.
        break;
      }
      debug(persistence, "@V");
      // The previous activity check requied write mode but the verify will be
      // much faster if memory is returned to memory map mode.
      flash_mode(FLASH_MEMORY_MAPPED);
      // Dummy reads help speed up synchronization when switching from async
      // bit-bang to sync memory mapped mode.
      // TODO This is 1000 reads via an expensive function but it is likely only
      // a few memory accesses are really needed.
      for (int i=0; i<ERASE_SECTOR_SIZE; i+=4) {
        uint32_t dummy;
        memcpy(&dummy, (void*)(SPIFLASH_BASE+i), 4);
      }
      if(memcmp((const void*)(SPIFLASH_BASE+flash_dst_addr), flash_src_ptr,
            flash_update_size)) {
        error("verify");
      }
      flash_state = FLASH_USER_MODE;
      // Signal programming of this sector is complete.
      //printf("Sector at 0x%X updated\n", flash_dst_addr);
      debug(persistence, "@U");
      break;

    default:
      debug(persistence, "@FS%d", flash_state);
      error("state");
  }
}

void flash_init(void) {
  // At power on, the flash is memory mapped and the state is user mode.
  // However, the flash will be left in bit-bang write mode if the application
  // crashes and restarts while a write was in progress. This seeminly
  // unnecessary initialization ensures a sane environment after crash recovery.
  flash_mode(FLASH_MEMORY_MAPPED);
  flash_state = FLASH_USER_MODE;
}
