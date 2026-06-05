/* Windows compatibility for sys/wait.h */
#ifndef SYS_WAIT_H_COMPAT
#define SYS_WAIT_H_COMPAT

/* waitpid options from POSIX.1-2008 (bits for 3rd parameter) */
#define WNOHANG 1          /* Bit 0: Return immediately if no child has exited */
#define WUNTRACED 2        /* Bit 1: Also return for stopped children (not just terminated) */

/* Process exit status macros (POSIX.1-2008) - Windows stubs always report normal exit */
#define WIFEXITED(status) 1              /* Always true: process exited normally */
#define WEXITSTATUS(status) (status)     /* Return full status as exit code */
#define WIFSIGNALED(status) 0            /* Always false: not terminated by signal */
#define WTERMSIG(status) 0               /* Always 0: no terminating signal */

#ifndef __GNUC__
typedef int pid_t;
#endif

static inline pid_t waitpid(pid_t pid, int *status, int options) {
  return -1;
}

#endif
