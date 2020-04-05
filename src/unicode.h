/*-------------------------------------------------------------------------
 *
 * unicode.h
 *	  unicode and wide chars routines
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
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

extern size_t utf8len(char *s);
extern size_t utf8len_start_stop(const char *start, const char *stop);
extern int utf8charlen(char ch);
extern int utf_dsplen(const char *s);
extern int utf_string_dsplen(const char *s, size_t max_bytes);
extern int readline_utf_string_dsplen(const char *s, size_t max_bytes, size_t offset);
extern const char *utf8_nstrstr(const char *haystack, const char *needle);
extern const char *utf8_nstrstr_with_sizes(const char *haystack, int haystack_size, const char *needle, int needle_size);
extern const char *utf8_nstrstr_ignore_lower_case(const char *haystack, const char *needle);
extern bool utf8_isupper(const char *s);
extern unsigned char *unicode_to_utf8(wchar_t c, unsigned char *utf8string, int *size);
extern int utf8_tofold(const char *s);
extern int utf2wchar_with_len(const unsigned char *from, wchar_t *to, int len);
extern int utf_string_dsplen_multiline(const char *s, size_t max_bytes, bool *multiline, bool first_only, long int *digits, long int *others);

#endif
