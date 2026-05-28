/* Windows compatibility for poll.h */
#ifndef POLL_H_COMPAT
#define POLL_H_COMPAT

#include <winsock2.h>

#ifndef WSAPoll
  #define pollfd pollfd_custom
  
  struct pollfd_custom {
    int fd;
    short events;
    short revents;
  };

  /* Poll event flags from POSIX.1-2008 (bitmask for events/revents fields) */
  #ifndef POLLIN
    #define POLLIN  0x0001    /* Bit 0: Data available for reading */
    #define POLLOUT 0x0004    /* Bit 2: Ready for writing */
    #define POLLERR 0x0008    /* Bit 3: Error condition */
  #endif

  int poll(struct pollfd_custom *fds, unsigned int nfds, int timeout);
#else
  #define poll WSAPoll
#endif

#endif
