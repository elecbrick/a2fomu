//
// fatfs.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// Access routines for FAT filesystem memory mapped in linear address space.

#ifdef DEBUG
#undef DEBUG
#endif
#ifdef DEBUG
#define debug printf
#else
#define debug(x, ...)
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <flash.h>
#include <fsfat.h>
#include <a2fomu.h>
//#include <dirent.h>  // Use the raw DOS file entry not a POSIX compliant one.

static_assert(sizeof(struct dirent)==32, "Struct dirent not packed correctly");

//-------------------------------------------
// Integer type definitions for FatFs module
//-------------------------------------------

// Results of Disk Functions
typedef enum {
  RES_OK = 0, // 0: Successful
  RES_ERROR, // 1: R/W Error
  RES_WRPRT, // 2: Write Protected
  RES_NOTRDY, // 3: Not Ready
  RES_PARERR // 4: Invalid Parameter
} DRESULT;

//---------------------------------------
// Prototypes for disk control functions
//---------------------------------------
DRESULT disk_read (uint8_t pdrv, uint8_t* buff, uint32_t sector, unsigned count);

//--------------------------------------------------------------------------
// Module Private Work Area
//--------------------------------------------------------------------------
FATFS g_filesystem;


//╔═══════════════════════════════════════════════════════════════════════════╗
//║                                                                           ║
//║        Helper Functions for byte alignment and address translation        ║
//║                                                                           ║
//╚═══════════════════════════════════════════════════════════════════════════╝

//-----------------------------------------------------------------------
// Fetch unaligned multi-byte words
// Many multi-byte numbers in the boot sector are not aligned and would cause a
// trap if an attempt is made to read them directly by low-end processors. This
// is not and endien issue even though the corrective measue is one commonly
// used to ensure the bytes are assembled in the correct order.
//-----------------------------------------------------------------------

// Load a 2-byte little-endian word from unaligned memory
uint16_t align16 (const uint8_t* ptr) {
  return (ptr[1]<<8)|ptr[0];
}

// Load a 4-byte little-endian word from unaligned memory
uint32_t align32 (const uint8_t* ptr) {
  return (((ptr[3]<<8)|(ptr[2]<< 8))|(ptr[1]<<8))|ptr[0];
}

//-----------------------------------------------------------------------
// Retrieve cluster number of a pointer into the memory mapped filesystem
// Return: 2-0xFF5: Sector number in data region
//        -1: Address is not within the data area
//-----------------------------------------------------------------------
int cluster_number(const void *addr) {
  int cluster = ((uint8_t*)addr-g_filesystem.p_ino)/FLASHFS_SECTOR_SIZE;
  if(cluster<2 || cluster>=(int)g_filesystem.n_fatent) {
    cluster = -1;
  }
  return cluster;
}

//-----------------------------------------------------------------------
// Retrieve byte offset into cluster from a pointer into the filesystem
// Return: 2-0xFF5: Sector number in data region
//        -1: Address is not within the data area
//-----------------------------------------------------------------------
int cluster_offset(void *addr) {
  return ((uint8_t *)addr-g_filesystem.p_ino)&(FLASHFS_SECTOR_SIZE-1);
}

//-----------------------------------------------------------------------
// Get pointer to requested sector(cluster) that is resident in memory.
// Check range to ensure cluster is in the valid range of the filesystem then
// return the address of the sector.
// Return NULL if invalid cluster number.
//-----------------------------------------------------------------------
static void *lookup_fat(int cluster) {
  if(cluster<2 || cluster>=(int)g_filesystem.n_fatent) {
    return NULL;
  }
  return g_filesystem.p_ino+cluster*FLASHFS_SECTOR_SIZE;
}

//-----------------------------------------------------------------------
// FAT access - Read value of a FAT entry
// Return:  0..0xFFF: Value in FAT for given cluster
//         -1:        Error, cluster out of range for this filesystem
//-----------------------------------------------------------------------
int next_cluster(uint32_t cluster) {
  unsigned byte_pair, cluster_pair;

  // Verify cluster is valid for this filesystem. 
  if(cluster<2 || cluster>=g_filesystem.n_fatent) {
    errno = EBADF;
    return -1;
  }
  // Each FAT entry is 12 bits. Read the two bytes containing the entry.
  // Two entries are in three bytes so multiply by 1.5 to get a 24-bit pair
  cluster_pair = cluster+cluster/2;
  // The bytes may be crossing a word boundary so read independently.
  // They may also cross a sector boundary but this assumes the entire FAT is
  // resident in memory.
  byte_pair = g_filesystem.p_fat[cluster_pair++];
  byte_pair |= g_filesystem.p_fat[cluster_pair] << 8;
  // Finally, get the bits into the right position and mask off bits from the
  // other sector in the trio of bytes. Even numbered clusters have extra bits
  // above the MSB that need to be masked off while odd numbered clusters have
  // extra bits below the LSB that will be removed by shifting the LSB into
  // position.
  debug("next of %03x->%04x\n", (unsigned)cluster, (cluster&1) ? (byte_pair>>4) : (byte_pair&0xFFF));
  return (cluster&1) ? (byte_pair>>4) : (byte_pair&0xFFF);
}


//╔═══════════════════════════════════════════════════════════════════════════╗
//║                                                                           ║
//║   Directory handling - POSIX standard API calls for walking a filesystem  ║
//║                                                                           ║
//╚═══════════════════════════════════════════════════════════════════════════╝

//-----------------------------------------------------------------------
// Open a directory stream. Everything is memory mapped so no buffer
// management is necessary or useful. Keep a pointer to the entry of the
// directory being scanned and a pointer to the current item within the
// directory.
//-----------------------------------------------------------------------
void _opendir(DIR *dir_p, struct dirent *ent_p) {
  // The root directory has this_d set to the volume in order to differentiate
  // from the first entry in the root directory. We need to handle this.
  if((void*)ent_p==(void*)g_filesystem.p_volume) {
    dir_p->this_d = (void*)g_filesystem.p_volume;
    dir_p->next_d = g_filesystem.p_rootdir;
  } else {
    dir_p->this_d = ent_p;
    dir_p->next_d = lookup_fat(dir_p->this_d->first_cluster);
  }
  debug("_opendir this %08x next %08x\n", (unsigned)dir_p->this_d, (unsigned)dir_p->next_d);
}

//-----------------------------------------------------------------------
// Close a directory stream. Easy enough. We could NULLify the pointers but
// nothing needs to be done so why bother. It will be no different from
// unitinialized.
//-----------------------------------------------------------------------
int closedir(DIR *dirp) {
  (void)dirp;
  return 0;
}

//-----------------------------------------------------------------------
// Save the current position within a directory. Since the entire filesystem is
// memory mapped, the location is simply the pointer to the current dirent.
//-----------------------------------------------------------------------
long telldir(DIR *dirp) {
  // Return current offset
  return (long)dirp->next_d;
}

//-----------------------------------------------------------------------
// Directory handling - Returning to the beginning of a subdirectory. The
// first cluster of the directory is part of the DIR structure to enable fast
// rewind. Without this, it would be necessary to walk the filesystem again to
// find the parent of an inode.
//-----------------------------------------------------------------------
void rewinddir(DIR *dirp) {
  // The root directory has this_d set to the volume in order to differentiate
  // from the first entry in the root directory. We need to handle this.
  if((void*)dirp->this_d==(void*)g_filesystem.p_volume) {
    dirp->next_d = g_filesystem.p_rootdir;
  } else {
    dirp->next_d = lookup_fat(dirp->this_d->first_cluster);
  }
}

//-----------------------------------------------------------------------
// Restore the current position within a directory. Since the entire filesystem
// is memory mapped, the location is simply the pointer to the current dirent.
//-----------------------------------------------------------------------
void seekdir(DIR *dirp, long loc) {
#ifdef SIMPLE_VERIFICATION
  // The value of loc was supposedly returned by telldir with the current dirp.
  // Full validation requires walking the sector chain to verify the target loc
  // really is in the chain. Simple validation just checks that the target is
  // within the range of the filesystem and is properly aligned for word
  // access.
  // Verify validity of recall location. It must be on a 32-byte boundary 
  // within the volume. The actual minimum is a 32-bit boundary but making the
  // check slightly more inclusive is simple enough.
  if(loc<g_filesystem.p_rootdir ||
      (loc-g_filesystem.p_rootdir)%sizeof(struct dirent) ||
      loc>g_filesystem.p_ino+g_filesystem.n_fatent*FLASHFS_SECTOR_SIZE) {
    dirp->dir = NULL;
    errno = EBADF;
    return;
  }
#endif
  // Set current offset to what was supposedly returned by telldir
  dirp->next_d = (struct dirent *)loc;
#ifdef PEDANTIC_VERIFICATION
  // This code should never be run unless there is suspicion that a recall
  // position could become corrupted. Walk the directory and verify that the
  // recall location is within the directory. One huge assumption is that the
  // first_cluster field of dirp is not corrupted itself.
  // Root directory starts at cluster 0
  cluster = dirp->first_cluster;
  if(cluster==0) {
    // Root directory is guaranteed contiguous.
    if(loc>=g_filesystem.n_dirent*sizeof(struct direct)) {
      // Requested entry is not in root directory
      dirp->dir = NULL;
      errno = EBADF;
      return;
    }
  } else {
    // Subdirectory can be disjoint so need to follow cluster chain
    while(loc>=FLASHFS_SECTOR_SIZE) {
      // Advance to next cluster in chain
      cluster = lookup_fat(cluster);
      if(cluster<0) {
        dirp->dir = NULL;
        errno = EBADF;
        return;
      }
      if(cluster<2 || cluster>=g_filesystem.n_fatent) {
        // Reached end of the chain before reaching the requested offset
        dirp->dir = NULL;
        errno = EBADF;
        return;
      }
      loc -= FLASHFS_SECTOR_SIZE;
    }
    if(cluster_number(loc)!=cluster) {
      dirp->dir = NULL;
      errno = EBADF;
      return;
    }
  }
#endif
  return;
}

//-----------------------------------------------------------------------
// Return next entry in directory.
// Returns NULL after returning the last entry.
//-----------------------------------------------------------------------
struct dirent *readdir(DIR *dirp) {
  struct dirent *entp;
  uint32_t clust;

  // The result is already being pointed to because the prior call to readdir
  // or opendir performed the initialization as part of its function.
  entp = dirp->next_d;
  // Advance pointer so next call returns next entry.
  if(entp) {
    dirp->next_d++;
    if(cluster_offset(dirp->next_d)==0) {
      // Cluster advancing
      if((void*)dirp->this_d==(void*)g_filesystem.p_volume) {
        // Root directory: fixed size, fixed location, contiguous.
        if((uint8_t*)dirp->next_d>=g_filesystem.p_ino+2*FLASHFS_SECTOR_SIZE) {
          // Normal end of directory. Set state so NULL is returned next time.
          dirp->next_d = NULL;
        }
      } else {
        // Subdirectory that needs to follow cluster chain.
        clust = next_cluster(cluster_number(dirp->next_d));
        if(clust<2 || clust >= g_filesystem.n_fatent) {
          // Normal end of directory. Set state so NULL is returned next time.
          // TODO Should be able to signal error on subsequent read as well in
          // the event that a bad pointer exists instead of a normal EOC mark.
          dirp->next_d = NULL;
        } else {
          // Get next cluster
          dirp->next_d = lookup_fat(clust);
        }
      }
    }
  }
  if(entp->filename[0]=='\0') {
    // A filename starting with '\0' indicates no further entries exist and
    // that this is the end of the directory.
    entp = NULL;
  }
  debug("readdir %08x\n", (unsigned)entp);
  return entp;
}

//-----------------------------------------------------------------------
// Directory handling - Look for a named object in the directory.
// Return number of matches found.
//-----------------------------------------------------------------------
struct dirent *scandir(DIR* dirp, char *pattern) {
  struct dirent *entp;
  struct dirent *foundp=NULL;
  int i, found=0;

  do {
    entp = readdir(dirp);
    if(!entp) {
      // No more files in this directory
      break;
    }
    // This is likely a long filename.  Such entries are not handled by this
    // applicaiton.  It could also be a volume with the same name as the file
    // being searched for.
    if(entp->attributes&e_volume) {
      // Ignore volume labels.
      continue;
    }
    // Check if filename matches
    for(i=0; i<11; i++) {
      if(pattern[i]!=entp->filename[i]&&pattern[i]!='*') {
        // This is not the file you are looking for
        break;
      }
    }
    if(i<11) {
      // The name did not match. Check the next file.
      debug("no match i:%d, 11:%d, ==:%d, <:%d\n", i, 11, i==11, i<11);
      continue;
    }
    // Match! If pattern has wildcards, there could be more matches.
    foundp = entp;
    found++;
    debug("Found %d\n", found);
  } while (entp);
  if(found>1) {
    // Multiple matches of filename
    errno = ENAMETOOLONG;
    foundp=NULL;
  } else if(!found) {
    // No matches of filename
    errno = ENOENT;
  }
  debug("scandir entry:%08x error:%d pattern:%s\n", (unsigned)foundp, errno, pattern);
  return foundp;
}

//-----------------------------------------------------------------------
// Find named file in hierarchy
    // EOK(0): successful, !=0: error code
    // Directory object to return last directory and found object
    // Full-path string to find a file or directory
//-----------------------------------------------------------------------
struct dirent *finddirent(const char* path) {
  char filename[12];
  DIR dir_s;
  int i, c, in_ext, long_filename;
  struct dirent *de_p = (struct dirent*)(void*)g_filesystem.p_volume;

  // This OS does not have a current directory so everyting starts at the root.
  // The first entry of the root directory could be a subdirectory so a special
  // value must be used to indicate the root. NULL is reserved for end of
  // directory or no match found so we chose the volume record as the token.
  // All paths are absolute so strip any leading slash as all files are relative
  // to the root directory. To add working directory functionality, open the
  // working directory above and open the root directory in this loop.
  while(*path=='/' || *path =='\\') {
    path++;
    if(*path=='\0') {
      return de_p;
    }
  }
  // Loop for each directory level 
  for (;;) {
    _opendir(&dir_s, de_p);
    // Munge filename into format that matches directory entry
    i = in_ext = long_filename = 0;
    // Loop for each character in a name
    for (;;) {
      c = *path++;
      if(c=='\0') {
        // End of input path
        break;
      }
      if(c=='/' || c == '\\') { // Break if a separator is found
        while (*path == '/' || *path == '\\')
          path++; // Skip duplicated separator if exist
        break;
      }
      if (c=='.') {
        // Fill remainder of basename with spaces and move to extenstion
        in_ext = 1;
        for( ; i<8; i++) {
          filename[i] = ' ';
        }
        // Force to beginning of extension so filenames.with.dots.txt
        // keeps the txt extenstion
        i = 8;
        continue;
      }
      if(c=='*') {
        // Handle wildcards by expanding to end of name.
        if(in_ext) {
          // in extension - wildcard slots not explicitly given yet.
          for( ; i<11; i++) {
            filename[i] = '*';
          }
        } else {
          // Wildcard rest of basename but keep looking for extension.
          for( ; i<8; i++) {
            filename[i] = '*';
          }
        }
        continue;
      }
      if(in_ext) {
        if(i>=11) {
          // Ignore extension character after the first three.
          continue;
        }
      } else {
        if(i>=8) {
          // Ignore characters after first 8 and set flag to convert to short
          // form of long file name (~N) format.
          long_filename = 1;
          continue;
        }
      }
      // All special characters handled. Anything remaining must be part of
      // the filename. Store it for comparison.
      // TODO Many special characters are not valid in filenames and should
      // generate an error. Since this is read-only, no harm in skipping check.
      // DOS FAT names must be stored in upper case. This also makes the
      // comparison routine slightly easier.
      if(c>='a' && c<='z') {            // TODO this should be toupper() but
        c = c+'A'-'a';                  // build environment throws errors.
      }
      filename[i++] = c;
    }
    // Pad short names with spaces
    for( ; i<11; i++) {
      filename[i] = ' ';
    }
    if(long_filename) {
      // reallylongfilename is proabably REALLY~1 but if there also happens to
      // be reallylongfilename2 then it might be REALLY~2 or another number.
      // Look for a ~ but wildcard the digit so as to not guess incorrectly.
      filename[6] = '~';
      filename[7] = '*';
    }
    filename[11] = '\0';
    // Find an object with the chosen name in the current directory
    de_p = scandir(&dir_s, filename);
    if(!de_p) {
      // Either no object found, multiple objects found, or some internal
      // error occurred. errno has been set appropriately so return NULL.
      break;
    }
    if(c=='\0') {
      // The last character of path has been read and all segments matched.
      // Return entry found.
      break;
    }
    // There is a slash following this name so it should be a sub-directory.
    // Verify this fact before trying to process it.
    if (!(de_p->attributes&e_directory)) {
      // It is not a sub-directory and cannot follow
      errno = ENOTDIR;
      de_p = NULL;
      break;
    }
    // Close current directory and open next level directory the entry of which
    // already happens to be in de_p.
    (void)closedir(&dir_s);
  }
  debug("finddirent %08x\n", (unsigned)de_p);
  return de_p;
}

//---------------------------------------------------------------------------
//  Public Functions
//---------------------------------------------------------------------------

//-----------------------------------------------------------------------
// Init routine to start using a logical drive that is in entirely mapped into
// memory at the location given by the volume parameter.
// Return: EOK (0): success
//         EINVAL:  failure
// Failure would be because a valid FAT12 filesystem was not detected at the
// given address.
//-----------------------------------------------------------------------
int mount(void* filesystem, long opt) {
  // No options supported currently but keep the placeholder.
  (void)opt;
  // A DOS FAT filesystem stats with a boot sector
  boot_sector *volume = filesystem;
  if((align16(volume->boot_signature)==0xAA55) &&
      ((volume->jmp[0]==0xEB && volume->jmp[2]==0x90) ||
      volume->jmp[0]==0xE9) && volume->sectors_per_cluster==1 &&
      (align16(volume->bytes_per_sector)==FLASHFS_SECTOR_SIZE)) {
    // Valid boot sector found and a FAT filesystem exists with a cluster size
    // of 4kB having one sector per cluster. This is the base requirement for
    // this simple file system.
  } else {
    // Filesystem not found
    printf("mount: Invalid boot sig %04X(AA55), jmp %02X(E9), spc %02X(01), bps %04X(4k)\n",
        align16(volume->boot_signature),
        volume->jmp[0],
        volume->sectors_per_cluster,
        align16(volume->bytes_per_sector));
    return EINVAL;
  }
  // Initialize filesystem pointers and ranges
  g_filesystem.p_volume = volume;
  // The FAT sectors come right after the reserved sectors. There is likely
  // exactly one reserved sector, the boot sector. However, there is not need
  // for this assumption as the configured number is provided.
  g_filesystem.p_fat = filesystem+
      align16(volume->reserved_sectors)*FLASHFS_SECTOR_SIZE;
  // The root directory comes right after the FAT tables. There may be one or
  // two FAT tables and each table may be multiple sectors. Simple
  // multiplication lets us know where the root directory starts. There is no
  // need for more than one FAT as DOS never uses anything but the first. Also,
  // a size of 4kB per cluster allows a filesystem up to 11GB with a single
  // cluster FAT. Any device with this large a filesystem can afford a more
  // extensive implementation than this one.
  g_filesystem.p_rootdir = filesystem+(1+volume->num_fats*
      align16(volume->sectors_per_fat))*FLASHFS_SECTOR_SIZE;
  // The size of the root directory is needed and can be calculated from the
  // number of entries since each entry is a fixed 32 bytes and the number of
  // entries is provided in the boot sector.
  g_filesystem.n_dirent = align16(volume->max_root_dir_ent);
  // The data region is right after the root directory. Clusters 0 and 1 are
  // reserved and do not exist. The first cluster after the root directory is
  // cluster 2. We make things easy by initializing our pointer to the
  // theoretical cluster 0 which will be either in the root directory or the
  // FAT itself. Terminology is the POSIX inode rather than the DOS cluster as
  // a reminder that the index starts at 0 not at the start of the data region.
  g_filesystem.p_ino = ((uint8_t*)g_filesystem.p_rootdir)+
      g_filesystem.n_dirent*sizeof(struct dirent)-2*FLASHFS_SECTOR_SIZE;
  // The last necessary piece of information is the maximum valid cluster
  // number for range checking. This will be the provided number of clusters on
  // the medium minus those between the boot sector and inode 0.
  g_filesystem.n_fatent = align16(volume->num_sectors)-
      ((void*)g_filesystem.p_ino-filesystem)/FLASHFS_SECTOR_SIZE;
  // Everything should work now as long as the above parameters were configured
  // correctly. Of course, if there are problems like that, they would show up
  // under Windows or Linux when the filesystem was created.
  debug("mount mounted\nvol %08x\nfat %08x\ndir %08x\nino %08x\ndirent %d, fatent %d\n", (unsigned)g_filesystem.p_volume, (unsigned)g_filesystem.p_fat, (unsigned)g_filesystem.p_rootdir, (unsigned)g_filesystem.p_ino, (int)g_filesystem.n_dirent, (int)g_filesystem.n_fatent);
  return 0;
}

//-----------------------------------------------------------------------
// Open File
//-----------------------------------------------------------------------
FILE *fopen(const char* pathname, const char* mode) {
  FILE *file_p;
  struct dirent *dirp;
  int fileno;

  // TODO Only mode supported is "r". Skipping check.
  (void)mode;
  // Follow the file path. This routine can also be used by cd and opendir.
  dirp = finddirent(pathname);
  if(!dirp) {
    // File not found. Nothing exists with that name.
    errno = ENOENT;
    return NULL;
  }
  if(dirp->attributes&(e_directory|e_volume)) { // It is a directory
    errno = EISDIR;
    return NULL;
  }
  // File found and validated.
  // Allocate an unused FILE and, by association a fileno, for this file.
  // Do not allocate stdin, stdout, or sderr here even if they are freed.
  for(fileno=3; fileno<FOPEN_MAX; fileno++) {
    if(_file[fileno].device==a2dev_none) {
      // Found one.
      break;
    }
  }
  debug("Found fileno %d\n", fileno);
  if(fileno>=FOPEN_MAX) {
    // Well, it almost worked. Too many files open in system.
    errno = ENFILE;
    return NULL;
  }
  // Fill out the FILE structure that is being returned. This will allow
  // stdio calls such as getc and scanf to read the file.
  file_p = &_file[fileno];
  file_p->device = a2dev_flash;         // This is the mass storage device
  file_p->_loc = dirp->first_cluster;   // Cluster currently in buffer
  file_p->buffer = lookup_fat(file_p->_loc);   // Last one if multiple
  file_p->_max = FLASHFS_SECTOR_SIZE;   // Buffer size
  file_p->head = 0;                     // Reset read pointer
  file_p->tail = file_p->_max;          // Indicate buffer is full
  file_p->minor = (long)dirp;           // Cookie is pointer to the dirent
  file_p->_flags = 0;                   // Clear error and end of file status
  debug("fopen fileno %d\n", fileno);
  return file_p;
}

//-----------------------------------------------------------------------
// Close File
//-----------------------------------------------------------------------
int fclose(FILE *file_p) {
  file_p->device = a2dev_none;
  return 0;
}

//-----------------------------------------------------------------------
// Open Directory
//-----------------------------------------------------------------------
DIR *opendir(const char* pathname) {
  FILE *file_p;
  DIR *dir_p;
  struct dirent *dirent_p;
  int fileno;

  // Follow the file path. This routine is also be used by open.
  dirent_p = finddirent(pathname);
  if(!dirent_p) {
    // File not found. Nothing exists with that name.
    errno = ENOENT;
    return NULL;
  }
  if(dirent_p!=(struct dirent*)(void*)g_filesystem.p_volume &&
      !(dirent_p->attributes&e_directory)) { // It must be a directory
    errno = ENOTDIR;
    return NULL;
  }
  // File found and validated.
  // Allocate an unused FILE and, by association a fileno, for this file.
  // Do not allocate stdin, stdout, or sderr here even if they are freed.
  for(fileno=3; fileno<FOPEN_MAX; fileno++) {
    if(_file[fileno].device) {
      // Found one.
      break;
    }
  }
  if(fileno>=FOPEN_MAX) {
    // Well, it almost worked. Too many files open in system.
    errno = ENFILE;
    return NULL;
  }

  // Directory stream uses the same preallocated FILE structure that are used
  // by stdio.
  file_p = &_file[fileno];
  file_p->device = a2dev_flash;         // This is the mass storage device
  file_p->_flags = 0;                   // Clear error and end of file status
  dir_p = (DIR*)file_p;
  _opendir(dir_p, dirent_p);
  file_p->minor = (long)dirent_p;           // Cookie is pointer to the dirent
  return dir_p;
}

//-----------------------------------------------------------------------
// Read File
//-----------------------------------------------------------------------
ssize_t read(int fd, void *buf, size_t count) {
  FILE* file_p = &_file[fd];            // Get file object from file number
  ssize_t bytes_read = 0;               // Initialized the returned value
  int clust, available;

  if(fd<0 || fd>OPEN_MAX || file_p->device!=a2dev_flash) {
    errno = EBADF;
    return -1;
  }
  printf("read f=%d, a=%08x, s=%d\n", fd, (unsigned)buf, count);
  // TODO Assume file is opened for read so no need to check flags
  // Repeat until all data read
  while(count) {
    // Copy all available data in current buffer to destination
    available = file_p->tail-file_p->head;
    debug("Avail: %d\n", available);
    if(available) {
      if(available<0) {
        // TODO A write operation was performed as that is the only reason
        // the tail pointer should have moved.  Write is not supported.
        file_p->_flags |= __SERR;
        bytes_read = -1;
        errno = EINVAL;
        return -1;
      }
      if(available>(int)count) {
        available = count;
      }
      memcpy(buf, file_p->buffer+file_p->head, available);
      debug("Copied dst=%08x, src=%08x, size=%d\n", (unsigned)buf,
          (unsigned)(file_p->buffer+file_p->head), available);
      bytes_read += available;
      file_p->head += available;
      buf += available;
      count -= available;
    }
    // Nothing left in current sector. fill buffer with next sector.
    if(count) {
      // Advance to next sector
      clust = next_cluster(file_p->_loc);
      if(clust<2 || (unsigned)clust >= g_filesystem.n_fatent) {
        if((clust&0xFF8)==0xFF8) {
          // Normal end of file
          file_p->_flags |= __SEOF;
        } else {
          // File corruption
          errno = EBADF;
          file_p->_flags |= __SERR;
          return -1;
        }
        break;
      }
      file_p->_loc = clust;
      file_p->buffer = lookup_fat(clust);   // Last one if multiple
      file_p->_max = FLASHFS_SECTOR_SIZE;   // Buffer size
      file_p->head = 0;                     // Reset read pointer
      file_p->tail = file_p->_max;          // Indicate buffer is full
    }
  }
  return bytes_read;
}

//-----------------------------------------------------------------------
// Reposition read/write pointer to specific location within a File
// Note: Different from POSIX, this will not position beyond the end of
// file since TODO extending the file would require writing to the file.
//-----------------------------------------------------------------------
off_t lseek(int fd, off_t offset, int whence) {
  FILE* file_p = &_file[fd];            // Get file object from file number
  int clust, pos=0;
  struct dirent *dir_p=(struct dirent*)file_p->minor;

  printf("seek f=%d, a=%08x, s=%d\n", fd, (unsigned)offset, whence);
  if(fd<0 || fd>OPEN_MAX || file_p->device!=a2dev_flash) {
    errno = EBADF;
    return -1;
  }
  // First determine the current byte offset since this is not stored
  clust = dir_p->first_cluster;                 // Rewind to beginning
  while(clust!=file_p->_loc) {
    clust = next_cluster(clust);
    if(clust<2 || (unsigned)clust >= g_filesystem.n_fatent) {
      // File corruption
      errno = EBADF;
      file_p->_flags |= __SERR;
      return -1;
    }
    pos += FLASHFS_SECTOR_SIZE;
  }
  switch(whence) {
    case SEEK_SET: break;                       // Absolute posistion given
    case SEEK_CUR: offset += pos+file_p->head;  // Add current offset in file
                   break;
    case SEEK_END: offset += dir_p->file_size; // Relative to end of file
                   break;
    default:       errno=EINVAL;
                   return -1;
  }
  // Limit offset to file size
  if((unsigned)offset>dir_p->file_size ) {
    errno=EINVAL;
    return -1;
  }
  // Start from beginning if offset is negative
  if(offset<pos) {
    clust = dir_p->first_cluster;      // Rewind to beginning
    pos = 0;
  }
  // Advance until the correct sector is located
  while(offset>=pos+FLASHFS_SECTOR_SIZE) {
    // Advance to next sector
    clust = next_cluster(file_p->_loc);
    if(clust<2 || (unsigned)clust >= g_filesystem.n_fatent) {
      // File corruption
      errno = EBADF;
      file_p->_flags |= __SERR;
      return -1;
    }
  }
  // Fill buffer with current sector
  file_p->_loc = clust;                         // Point to the right location
  file_p->buffer = lookup_fat(clust);           // Make data available for stdio
  file_p->_max = FLASHFS_SECTOR_SIZE;           // Reset buffer size just incase
  file_p->head += offset%FLASHFS_SECTOR_SIZE;   // Update read pointer
  file_p->tail = file_p->_max;                  // Last valid byte in buf +1
  return offset;
}
