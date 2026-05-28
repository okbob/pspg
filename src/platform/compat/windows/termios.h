/* Windows compatibility for termios.h */
#ifndef TERMIOS_H_COMPAT
#define TERMIOS_H_COMPAT

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;

struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_cc[32];
};

/* Terminal control values from POSIX.1-2008 (octal values from 4.3BSD) */
#define TCSANOW 0          /* Change attributes immediately */
#define TCSAFLUSH 2        /* Change after flushing output and draining input */
#define ECHO 0000010       /* Octal 010 (bit 3): Echo input characters */
#define ICANON 0000002     /* Octal 002 (bit 1): Canonical mode - line-based editing */

static inline int tcgetattr(int fd, struct termios *termios_p) {
  return 0;
}

static inline int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  return 0;
}

#endif
