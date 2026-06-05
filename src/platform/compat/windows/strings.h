/* Windows compatibility for strings.h */
#ifndef STRINGS_H_COMPAT
#define STRINGS_H_COMPAT

#include <string.h>

/* Case-insensitive string comparison */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#endif
