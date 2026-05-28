/*-------------------------------------------------------------------------
 *
 * platform.h
 *	  Cross-platform compatibility layer
 *
 *-------------------------------------------------------------------------
 */

#ifndef PSPG_PLATFORM_H
#define PSPG_PLATFORM_H

/* Standard includes that work everywhere */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _MSC_VER
  #define F_SETFL 4          /* fcntl command 4: Set file status flags */
  #define O_NONBLOCK 04000   /* Octal 04000 (0x800): Non-blocking I/O - from Linux fcntl-linux.h */
  #define fcntl(...) (0)

  #include <io.h>	/* chmod instead of sys/stat.h */
  #include <BaseTsd.h>
  typedef SSIZE_T ssize_t;
  #define PATH_SEPARATOR '\\'
  #define PATH_SEPARATOR_STR "\\"

  /* POSIX clock types from POSIX.1-2008 (clock_gettime parameter) */
  #ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC 1    /* Monotonic clock: cannot go backwards, unaffected by time adjustments */
  #endif

  #ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME 0     /* Real-time clock: wall-clock time, affected by NTP/manual changes */
  #endif
  struct timespec;
  int clock_gettime(int clk_id, struct timespec *tp);
#else
  #include <sys/types.h>
  #include <unistd.h>
  #define PATH_SEPARATOR '/'
  #define PATH_SEPARATOR_STR "/"
#endif

char *platform_strndup(const char *s, size_t n);
char *platform_basename(char *path);
char *platform_dirname(char *path);
int platform_usleep(unsigned int usec);

#ifdef _WIN32
char *getpass(const char *prompt);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#if defined(__has_attribute)
  #if __has_attribute(noreturn)
    #define PSPG_NORETURN __attribute__ ((noreturn))
  #else
    #define PSPG_NORETURN
  #endif
#elif defined(__GNUC__)
  #define PSPG_NORETURN __attribute__ ((noreturn))
#elif defined(_MSC_VER)
  #define PSPG_NORETURN __declspec(noreturn)
#else
  #define PSPG_NORETURN
#endif

#ifndef UNUSED
  #define UNUSED(expr) do { (void)(expr); } while (0)
#endif

#endif /* PSPG_PLATFORM_H */
