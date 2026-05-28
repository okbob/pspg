/* Windows compatibility for fcntl.h */
#ifndef FCNTL_H_COMPAT
#define FCNTL_H_COMPAT

#include <io.h>
#include <fcntl.h>

/* File control commands and flags from POSIX.1-2008 (missing in Windows fcntl.h) */
#define F_GETFL 3          /* fcntl command 3: Get file status flags (O_APPEND, O_NONBLOCK, etc.) */
#define F_SETFL 4          /* fcntl command 4: Set file status flags */
#define O_NONBLOCK 04000   /* Octal 04000 (0x800): Non-blocking I/O - from Linux fcntl-linux.h */

static inline int fcntl_win32(int fd, int cmd, ...)  {
  return 0;
}

#ifndef fcntl
  #define fcntl fcntl_win32
#endif

#endif
