/*-------------------------------------------------------------------------
 *
 * unicode.h
 *	  unicode and wide chars routines
 *
 * Portions Copyright (c) 2017-2017 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/unicode.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PSPG_UNICODE_H
#define PSPG_UNICODE_H

#include <stdlib.h>
#include <stdbool.h>

size_t utf8len(char *s);
size_t utf8len_start_stop(const char *start, const char *stop);
int utf8charlen(char ch);
int utf_dsplen(const char *s);
const char *utf8_nstrstr(const char *haystack, const char *needle);
bool utf8_isupper(const char *s);


#endif
