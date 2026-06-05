/* Windows compatibility for libgen.h */
#ifndef LIBGEN_H_COMPAT
#define LIBGEN_H_COMPAT

/* Path manipulation functions */
#define basename(x) platform_basename(x)
#define dirname(x) platform_dirname(x)

#endif
