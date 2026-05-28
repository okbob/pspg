/* Windows compatibility for sys/ioctl.h */
#ifndef SYS_IOCTL_H_COMPAT
#define SYS_IOCTL_H_COMPAT

/* Terminal ioctl requests from Linux asm-generic/ioctls.h */
#define TIOCGWINSZ 0x5413      /* Get window size - 'T'<<8 | 0x13 */

struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

static inline int ioctl(int fd, unsigned long request, ...) {
  return -1;
}

#endif
