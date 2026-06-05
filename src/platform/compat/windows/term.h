/* Windows compatibility for term.h */
#ifndef TERM_H_COMPAT
#define TERM_H_COMPAT

#ifndef NCURSES_CONST
  #define NCURSES_CONST const
#endif

static inline char* tigetstr(NCURSES_CONST char *capname) {
  return NULL;
}

#endif
