//
// fsfat.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _FSFAT_H_
#define _FSFAT_H_

// Access routines for FAT filesystem memory mapped in linear address space.
//
// TODO Also defined here are standard directory routines that return
// nonstandard results. The POSIX interface is used but struct dirent is a FAT
// entry that does not contain a NULL terminated filename. Also, the structure
// returned by readdir() will yield invalid results if read while the flash is
// being written to. As such, a copy from the internal form to standard format
// must be performed to ensure expected operation.

#include <stdint.h>
#include <sys/types.h>
// #include <stdio.h>
// #include <errno.h>
// #include <ctype.h>
// #include <flash.h>
// #include <a2fomu.h>

// Boot Sector incuding Bios Parameter Block (BPB). A requirement of FAT.
// There are many 16-bit and 32-bit numeric parameters in this structure.
// However, most are misaligned and will not load correctly on the minimal
// subset architecture used in A2Fomu. As such, everything here is defined
// as a byte array.
typedef struct {
  uint8_t jmp[3];                       // Jump Instruction
  uint8_t manufacturer[8];              // OEM Name
  uint8_t bytes_per_sector[2];          // Bytes per logical sector
  uint8_t sectors_per_cluster;          // Logical sectors per cluster
  uint8_t reserved_sectors[2];          // Reserved logical sectors
  uint8_t num_fats;                     // Number of File Allocation Tables
  uint8_t max_root_dir_ent[2];          // Maximum root directory entries
  uint8_t num_sectors[2];               // Total logical sectors
  uint8_t media_descriptor;             // Media descriptor
  uint8_t sectors_per_fat[2];           // Logical sectors per FAT
  uint8_t sectors_per_track[2];         // Physical sectors per track
  uint8_t num_heads[2];                 // Number of heads
  uint8_t hidden_sectors[4];            // Hidden sectors (0 if not partitioned)
  uint8_t num_sectors_32[4];            // Total Sectors if greater than 65536
  uint8_t drive_number;                 // Physical drive number
  uint8_t dirty;                        // Changed by OS when volume is mounted
  uint8_t extended_signature;           // Extended boot signature
  uint8_t volume_id[4];                 // Volume ID (BCD)
  uint8_t volume_label[11];             // Volume Label
  uint8_t fs_name[8];                   // File system type
  uint8_t boot_code[448];               // Bootstrap code or error message
  uint8_t boot_signature[2];            // Boot sector signature: 0x55, 0xAA
} boot_sector;

// Directory Entry
// These are 32 bytes long and the numeric fields within are all aligned
// correctly so fields may be accessed directly by the little-endian Risc-V
// processor.
typedef enum __attribute__((packed)) {   // Occupy one byte in structure
  e_readonly      = 0x01,
  e_hidden        = 0x02,
  e_system        = 0x04,
  e_volume        = 0x08,
  e_directory     = 0x10,
  e_archive       = 0x20,
  // The two high order bits of this byte are undefined and reserved and
  // may be used for internal purposes.
  e_contiguous    = 0x40,
} attribute_t;

typedef enum __attribute__((packed)) {   // Occupy one byte in structure
  e_lc_extension  = 0x08,
  e_lc_basename   = 0x10,
} lowercase_t;

typedef enum __attribute__((packed)) {   // Occupy two byte in structures
  e_owner_change  = 0x0001,
  e_owner_execute = 0x0002,
  e_owner_write   = 0x0004,
  e_owner_read    = 0x0008,
  e_group_change  = 0x0010,
  e_group_execute = 0x0020,
  e_group_write   = 0x0040,
  e_group_read    = 0x0080,
  e_world_change  = 0x0100,
  e_world_execute = 0x0200,
  e_world_write   = 0x0400,
  e_world_read    = 0x0800,
} permission_t;

// POSIX defines struct dirent used by all directory routines     *NONSTANDARD* 
// that is supposed to contain a NULL terminated filename.        *NONSTANDARD* 
// Rather than allocate memory for copies of objecs, we return    *NONSTANDARD* 
// pointers to the actual FAT directory entry that may be on      *NONSTANDARD* 
// read-only media.                                               *NONSTANDARD* 
struct dirent {
  uint8_t filename[11];                 // File name 8.3, padded with spaces
  //uint8_t extenstion[3];              // gcc workaround
  attribute_t attributes;               // File handling modifiers
  lowercase_t lowercase_flags;          // Lowercase basename and/or extension
  uint8_t creation_time[3];             // Time of file creation
  uint8_t creation_date[2];             // Date of file creation
  uint8_t access_date[2];               // Time of file creation
  permission_t permissions;             // Access rights (0: file unrestricted)
  uint8_t change_time[2];               // Time of last change
  uint8_t change_date[2];               // Date of last change
  uint16_t first_cluster;               // First cluster of file
  uint32_t file_size;                   // File size
};

// File system structure initialized from Boot Sector
typedef struct {
  boot_sector *p_volume;                // Pointer to boot sector
  uint16_t n_dirent;                    // Number of root directory entries
  uint32_t n_fatent;                    // Maximum valid pointer into FAT
  struct dirent *p_rootdir;             // Root directory
  uint8_t *p_fat;                       // Pointer to file allocation table
  uint8_t *p_ino;                       // Pointer to cluster 0
} FATFS;

// Directory object structure (DIR)
typedef struct {
  struct dirent *this_d;                // Pointer to directory being read
  struct dirent *next_d;                // Pointer to next item in directory
} DIR;

extern FATFS g_filesystem;

// <stdio.h> -- FAT filesystem Standard Input/Output API
FILE *fopen(const char* pathname, const char* mode);
int fclose(FILE* fp);

// <unistd.h> -- FAT filesystem unbuffered read/write access
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);

// <sys/mount.h>
int mount(void* filesystem, long opt);

// <dirent.h> -- FAT Directory Entry API
DIR           *opendir(const char *name);
int            closedir(DIR *dirp);
struct dirent *readdir(DIR *dirp);
void           rewinddir(DIR *dirp);
void           seekdir(DIR *dirp, long loc);
long           telldir(DIR *dirp);

// Nonstandard variant of scandir: Does not call malloc. Returns a unique
// directory entry if the pattern matches a single file. Returns ENOENT if
// pattern does not match and returns ENAMETOOLONG if multiple matches. This
// is intended for finding a file with a long filename by looking for a
// single file that ends with "~[0-9]"
struct dirent *scandir(DIR* dirp, char *pattern);

#endif /* _FSFAT_H_ */
