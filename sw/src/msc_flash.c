//
// msc_flash.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <generated/mem.h>
#include "a2fomu.h"
#include "flash.h"
#include "tusb.h"

#define FATFS_NUM_SECTORS  (0x17E)
#if FATFS_NUM_SECTORS != (FLASHFS_NUM_SECTORS-2)
#error "Block count mismatch"
#endif

int flash_drive = FIRST_SAFE_ADDRESS;

// The filesystem initialization shown here is not part of the a2fomu
// operating system. Instead, the filesystem is loaded into flash directly as
// a byproduct of programming the a2fomu gateware and firmware into flash.

// Block0: Boot Sector
uint8_t boot_sector[] = {
  0xEB, 0x3C, 0x90,                // x86 JMP opcode              xxx (expected)
  'f','o','m','u','l','o','a','d', // Disk Format Program          "" 003-00a
  0x00, 0x10, //FLASHFS_SECTOR_SIZE// Bytes per logical sector:  4096 (exp 512)
  0x01,                            // Logical sectors per cluster:  8 (4k erase)
  0x01, 0x00,                      // Reserved logical sectors:     1 00e-00f
  0x01,                            // Num. File Allocation Tables:  1     010
  0x00, 0x01,                      // Max. root directory entries:256 011-012
  0x80, 0x01, //FLASHFS_NUM_SECTORS// Total logical sectors:      382 013-014
  0xF8,                            // Media descriptor (in BPB)   xF8     015
  0x01, 0x00,                      // Logical sectors per FAT:      1 016-017
  0x20, 0x00,                      // Physical sectors per track:   1 018-019
  0x01, 0x00,                      // Number of heads:              1 01a-01b
  0x00, 0x00, 0x00, 0x00,          // Hidden sectors (not partnd):  0 (required)
  0x00, 0x00, 0x00, 0x00,          // Sectors if >65536:            0 020-023
  0x80,                            // Physical drive number:      127 (fixed)
  0x00,                            // Reserved (dirty bit)          0     025
  0x29,                            // Extended boot signature:    x29 (required)
  0x21, 0x20, 0x31, 0x01,          // Volume ID (BCD)             nnn 027-02a
  'A','2','F','o','m','u',' ',' ',' ',' ',' ',  // Volume Label:   "" 02b-035
  'F','A','T','1','2',' ',' ',' ', // File system type:            "" 036-03d
};
uint8_t boot_sector_signature[] = {
  0x55, 0xAA                       // Boot sector signature           1fe-1ff
};

// Block1: FAT12 Table
uint8_t fat_table_init[] = {
  0xF8,0xFF,0xFF,   // FAT ID / Media Descriptor + End Of Chain
  //0xFF,0x0F,0x00,   // Cluster end of readme file
};

// Block2,3: Root Directory
uint8_t root_directory_init[] = {
  'A','2','F','o','m','u',' ',' ',' ',' ',' ', // Volume Label
  0x08,                            // Attributes: Volume Label
  0x00,                            // Lowercase flags
  0x00, 0x00, 0x00,                // Creation time
  0x00, 0x00,                      // Creation date (1980)
  0x00, 0x00,                      // Last access date (1980)
  0x00, 0x00,                      // Access rights (unrestricted)
  0x4F, 0x6D,                      // Time of last change
  0x65, 0x43,                      // Date of last change
  0x00, 0x00,                      // First cluster
  0x00, 0x00, 0x00, 0x00,          // File size
  // second entry is readme file
  //'R','E','A','D','M','E',' ',' ','T','X','T', 0x20, 0x00, 0xC6, 0x52, 0x6D,
  //0x65, 0x43, 0x65, 0x43, 0x00, 0x00, 0x88, 0x6D, 0x65, 0x43, 0x02, 0x00,
  //sizeof(README_CONTENTS)-1, 0x00, 0x00, 0x00 // file size (4 Bytes)
};


// Callback from TinyUSB upon reception of a USB Mass Storage command.

// SCSI_CMD_INQUIRY received:
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void)lun;
  const char vid[] = "A2Fomu";
  const char pid[] = "Mass Storage";
  const char rev[] = "0.5";
  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Test Unit Ready command received.
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void)lun;
  return true; // Flash drive is always available
}

// SCSI_CMD_READ_CAPACITY_10, SCSI_CMD_READ_FORMAT_CAPACITY
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count,
    uint16_t* block_size) {
  (void)lun;
  // Drive size is requested.
  *block_count = FLASHFS_NUM_SECTORS;
  *block_size  = FLASHFS_SECTOR_SIZE;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
    bool load_eject) {
  (void)lun;
  (void) power_condition;
  if(load_eject) {
    if(start) {
      // load disk storage
    } else {
      // unload disk storage
    }
  }
  return true;
}

// READ10 command is received.
// Translate logical block address to flash memory address and copy the
// requrested number of bytes to the buffer.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  (void)lun;
  int src = flash_drive+lba*FLASHFS_SECTOR_SIZE+offset;
  return read_flash(buffer, src, bufsize);
}

// WRITE10 command is received.
// Translate logical block address to flash memory address and request the
// flash controller to write number of bytes from the buffer.
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void)lun;
  return write_flash(flash_drive+lba*FLASHFS_SECTOR_SIZE+offset,
      buffer, bufsize);
}

// Callback invoked for all SCSI commands that do not have their unique
// callbacks defined.
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer,
    uint16_t bufsize) {
  void const* response = NULL;
  uint16_t resplen = 0;
  // most scsi handled is input
  bool in_xfer = true;

  switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      resplen = 0;
      break;
    default:
      // Respond indicating an unrecognized command was received.
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      resplen = -1;
      break;
  }
  // Return value must fit within buffer.
  if(resplen>bufsize) {
    resplen = bufsize;
  }
  if(response&&(resplen>0)&&in_xfer) {
    memcpy(buffer, response, resplen);
  }
  return resplen;
}
