//
// errno.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

#ifndef _ERRNO_H_
#define _ERRNO_H_

extern int errno;

extern const char * const _sys_errlist[];
extern int _sys_nerr;

// Define only the errors that are actually produced by the operating system.
// This is done to prevent unneccessary textual error messages that will never
// be seen from consuming precious resources.

enum __errno {
  EAGAIN = 1,     // Resource unavailable, try again
  EBADF,          // Bad file descriptor
  EBUSY,          // Device or resource busy
  EINVAL,         // Invalid argument
  EIO,            // I/O error
  EISDIR,         // Is a directory
  EMFILE,         // Too many open files
  ENAMETOOLONG,   // Filename too long
  ENODEV,         // No such device
  ENOENT,         // No such file or directory
  ENOEXEC,        // Executable file format error
  ENOLCK,         // No locks available
  ENOTDIR,        // Not a directory
  EROFS,          // Read-only file system
  ERANGE,         // Result too large
  EDOM,           // Mathematics argument out of domain of function
  EILSEQ,         // Illegal byte sequence
  _MAX_ERROR,     // Size of _sys_errlist
};

// Treat similar errors the same way in this single user system
#define ENFILE EMFILE
#define EWOULDBLOCK EAGAIN 


// The following values for errno are defined by POSIX but are not currently
// used by A2Fomu.  These are not defined to keep _sys_errlist to the minimum.
// They are individually commented to ensure _sys_errlist is updated should any
// be used.
enum __errno_reserved {
  _MIN_EXTENDED_ERROR = _MAX_ERROR-1,
  // E2BIG,          // Argument list too long
  // EACCES,         // Permission denied  
  // EADDRINUSE,     // Address in use
  // EADDRNOTAVAIL,  // Address not available
  // EAFNOSUPPORT,   // Address family not supported
  // EALREADY,       // Connection already in progress
  // EBADMSG,        // Bad message
  // ECANCELED,      // Operation canceled
  // ECHILD,         // No child processes
  // ECONNABORTED,   // Connection aborted
  // ECONNREFUSED,   // Connection refused
  // ECONNRESET,     // Connection reset
  // EDEADLK,        // Resource deadlock would occur
  // EDESTADDRREQ,   // Destination address required
  // EEXIST,         // File exists
  // EFAULT,         // Bad address
  // EFBIG,          // File too large
  // EHOSTUNREACH,   // Host is unreachable
  // EIDRM,          // Identifier removed
  // EINPROGRESS,    // Operation in progress
  // EINTR,          // Interrupted function
  // EISCONN,        // Socket is connected
  // ELOOP,          // Too many levels of symbolic links
  // EMLINK,         // Too many links
  // EMSGSIZE,       // Message too large
  // ENETDOWN,       // Network is down
  // ENETRESET,      // Connection aborted by network
  // ENETUNREACH,    // Network unreachable
  // ENFILE,         // Too many files open in system
  // ENOBUFS,        // No buffer space available
  // ENODATA,        // No message is available on the STREAM head read queue
  // ENOMEM,         // Not enough space
  // ENOMSG,         // No message of the desired type
  // ENOPROTOOPT,    // Protocol not available
  // ENOSPC,         // No space left on device
  // ENOSR,          // No STREAM resources
  // ENOSTR,         // Not a STREAM
  // ENOSYS,         // Function not supported
  // ENOTCONN,       // The socket is not connected
  // ENOTEMPTY,      // Directory not empty
  // ENOTSOCK,       // Not a socket
  // ENOTSUP,        // Not supported
  // ENOTTY,         // Inappropriate I/O control operation
  // ENXIO,          // No such device or address
  // EOPNOTSUPP,     // Operation not supported on socket
  // EOVERFLOW,      // Value too large to be stored in data type
  // EPERM,          // Operation not permitted
  // EPIPE,          // Broken pipe
  // EPROTO,         // Protocol error
  // EPROTONOSUPPORT,// Protocol not supported
  // EPROTOTYPE,     // Protocol wrong type for socket
  // ESPIPE,         // Invalid seek
  // ESRCH,          // No such process
  // ETIME,          // Stream ioctl() timeout
  // ETIMEDOUT,      // Connection timed out
  // ETXTBSY,        // Text file busy
  // EXDEV,          // Cross-device link
  _MAX_EXTENDED_ERROR,
};

#endif /* _ERRNO_H_ */
