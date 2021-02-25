//
// dirent.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _DIRENT_H_
#define _DIRENT_H_

// POSIX compatible directory routines.
// Currently unused as this OS returns FAT entries directly and does not
// translate to the standardized format. 
//
// TODO A copy must be performed so entries returned are valid while a write
// to flash is in progress at which time, this structure will become relevant.

#include <sys/types.h>

// typedef struct {
// } DIR;
//
// struct dirent {
//   ino_t  d_ino          // File serial number. inode defined in sys/types.h
//   char   d_name[12];    // Name of entry. Size up to NAME_MAX including NULL
// };

#if 0
int closedir(DIR *);
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
void rewinddir(DIR *);
void seekdir(DIR *, long);
long telldir(DIR *);
#endif

#endif /* _DIRENT_H_ */
