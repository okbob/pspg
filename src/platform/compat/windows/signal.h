/* Windows compatibility for signal.h */
#ifndef SIGNAL_H_COMPAT
#define SIGNAL_H_COMPAT

#if defined(__GNUC__) || defined(__clang__)
# include_next <signal.h>
#else
# include <signal.h>
#endif

/* Signal handler function pointers (POSIX.1-2008, values from AT&T Unix) */
#ifndef SIG_IGN
  #define SIG_IGN ((void (__cdecl *)(int)) 1)    /* Ignore signal - pointer value 1 */
#endif
#ifndef SIG_DFL
  #define SIG_DFL ((void (__cdecl *)(int)) 0)    /* Default handler - pointer value 0 (NULL) */
#endif

/* Unix signal numbers from POSIX.1-2008 (values from 4.3BSD) */
#ifndef SIGPIPE
  #define SIGPIPE 13       /* Broken pipe - write to pipe with no reader */
#endif

/* Common signals (already in Windows signal.h) */
#ifndef SIGINT
  #define SIGINT 2         /* Interrupt from keyboard (Ctrl+C) */
#endif
#ifndef SIGTERM
  #define SIGTERM 15       /* Termination signal - graceful shutdown request */
#endif
#ifndef SIGSEGV
  #define SIGSEGV 11       /* Invalid memory reference - segmentation violation */
#endif

#ifndef SIGWINCH
  #define SIGWINCH 28      /* Window size change - terminal resized */
#endif

struct sigaction {
  void (*sa_handler)(int);
  int sa_flags;
  int sa_mask;
};

/* sigaction flags from POSIX.1-2008 (Linux sa_flags bits) */
#define SA_RESETHAND 0x00000004    /* Bit 2: Reset handler to SIG_DFL after invocation */

static inline int sigemptyset(int *set) {
  *set = 0;
  return 0;
}

#ifndef __GNUC__ /* MSVC only and not ClangCL */
typedef void (__CRTDECL *_crt_signal_t)(int);
    _ACRTIMP _crt_signal_t __cdecl signal(_In_ int _Signal, _In_opt_ _crt_signal_t _Function);
#endif

/*
 * Windows doesn't have sigaction, emulate with signal().
 */
static inline int sigaction(int signum, const struct sigaction *act,
                            struct sigaction *oldact) {
  if (act) {
    void (*old_handler)(int) = signal(signum, act->sa_handler);
    if (oldact) {
      oldact->sa_handler = old_handler;
      oldact->sa_flags = 0;
      oldact->sa_mask = 0;
    }
    return 0;
  }
  return -1;
}

#endif
