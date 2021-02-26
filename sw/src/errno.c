//
// errno.c - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#include <errno.h>

// Provide the strings for perror() that are actually produced by a2fomu.
// Other standard errors that could be produced in the future are kept in
// the last section of this file should the program be updated with features
// that will actually produce these errors.

int errno;
const char* const _sys_errlist[_MAX_ERROR] = {
  [EAGAIN]        = "Resource unavailable",
  [EBADF]         = "Bad file",
  [EBUSY]         = "Device busy",
  [EINVAL]        = "Invalid argument",
  [EIO]           = "I/O error",
  [EISDIR]        = "Is a directory",
  [EMFILE]        = "Too many open files",
  [ENAMETOOLONG]  = "Filename indeterminate",
  [ENODEV]        = "No such device",
  [ENOENT]        = "No such file or directory",
  [ENOEXEC]       = "Executable file format error",
  [ENOLCK]        = "No locks available",
  [ENOTDIR]       = "Not a directory",
  [EROFS]         = "Read-only file system",
  [ERANGE]        = "Result too large",         // Required by STD C.
  [EDOM]          = "Argument out of domain",   // Required by STD C.
  [EILSEQ]        = "Illegal byte sequence",    // Required by STD C.
};

#if 0
const char* const __attribute__((section (".a2folib0"))) __sys_errlist_extension[__MAX_ERROR] = {
  [EACCES]        = "Permission denied",
  [EDEADLK]       = "Deadlock would occur",
  [EEXIST]        = "File exists",
  [EFBIG]         = "File too large",
  [EINPROGRESS]   = "Operation in progress",
  [EINTR]         = "Interrupted call",
  [ENOBUFS]       = "No buffer space available",
  [ENOMEM]        = "Not enough space",
  [ENOSPC]        = "No space left on device",
  [ENOSYS]        = "Function not supported",
  [ENOTEMPTY]     = "Directory not empty",
  [ENOTSUP]       = "Not supported",
  [ENOTTY]        = "Inappropriate I/O control operation",
  [ENXIO]         = "No such device or address",
  [EOVERFLOW]     = "Value too large for data type",
  [EPERM]         = "Operation not permitted",
};
#endif
