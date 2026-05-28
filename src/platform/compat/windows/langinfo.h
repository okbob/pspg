/* Windows compatibility for langinfo.h */
#ifndef LANGINFO_H_COMPAT
#define LANGINFO_H_COMPAT

/* Character encoding query (always UTF-8 on Windows) */
#define CODESET 0

static inline char *nl_langinfo(int item) {
  (void)item;
  return "UTF-8";
}

#endif
