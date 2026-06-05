/* Windows compatibility for unistd.h */
#ifndef UNISTD_H_COMPAT
#define UNISTD_H_COMPAT

#include <io.h>
#include <process.h>
#include <stdio.h>
#include <BaseTsd.h>

#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/* Standard file descriptor numbers (POSIX.1-2008) */
#ifndef STDIN_FILENO
  #define STDIN_FILENO  0    /* Standard input - fd 0 */
  #define STDOUT_FILENO 1    /* Standard output - fd 1 */
  #define STDERR_FILENO 2    /* Standard error - fd 2 */
#endif

/* Platform-specific implementations */
#define strndup platform_strndup
#define usleep platform_usleep

/* POSIX to Windows CRT mappings */
#define access _access
#define close _close
#define dup2 _dup2
#define fileno _fileno
#define isatty _isatty
#define lseek _lseek
#define write _write

/* Process pipe functions */
#define popen _popen
#define pclose _pclose

#ifndef setlinebuf
  #define setlinebuf(stream) setvbuf(stream, NULL, _IOLBF, BUFSIZ)
#endif

int pipe(int pipefd[2]);
int fork(void);

#endif
