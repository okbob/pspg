/*-------------------------------------------------------------------------
 *
 * string.c
 *	  a routines for case insensitive string operations
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/string.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>

#include "pspg.h"

/*
 * Case insensitive string comparation.
 */
bool
nstreq(const char *str1, const char *str2)
{
	while (*str1 != '\0')
	{
		if (*str2 == '\0')
			return false;

		if (toupper(*str1++) != toupper(*str2++))
			return false;
	}

	return *str2 == '\0';
}

/*
 * special case insensitive searching routines
 */
const char *
nstrstr(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0, f2 = 0;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		if (*haystack_cur == '\0')
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			f1 = toupper(*needle_cur);
		}

		f2 = toupper(*haystack_cur);
		if (f1 == f2)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}

const char *
nstrstr_with_sizes(const char *haystack,
				   const int haystack_size,
				   const char *needle,
				   int needle_size)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	const char *haystack_end, *needle_end;
	int		f1 = 0, f2 = 0;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	haystack_end = haystack + haystack_size;
	needle_end = needle + needle_size;

	while (needle_cur < needle_end)
	{
		if (haystack_cur == haystack_end)
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			f1 = toupper(*needle_cur);
		}

		f2 = toupper(*haystack_cur);
		if (f1 == f2)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}

/*
 * Special string searching, lower chars are case insensitive,
 * upper chars are case sensitive.
 */
const char *
nstrstr_ignore_lower_case(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0;
	bool	eq;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		bool	needle_char_is_upper = false; /* be compiler quiet */

		if (*haystack_cur == '\0')
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			needle_char_is_upper = isupper(*needle_cur);
			f1 = toupper(*needle_cur);
		}

		if (needle_char_is_upper)
		{
			/* case sensitive */
			eq = *haystack_cur == *needle_cur;
		}
		else
		{
			/* case insensitive */
			eq = f1 == toupper(*haystack_cur);
		}

		if (eq)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}
