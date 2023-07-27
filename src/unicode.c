/*-------------------------------------------------------------------------
 *
 * unicode.c
 *	  unicode and wide chars routines
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/unicode.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "pspg.h"
#include "unicode.h"

inline static wchar_t utf8_to_unicode(const unsigned char *c);

/*
 * Returns length of utf8 string in chars.
 */
inline int
utf8len(const char *s)
{
	int			len = 0;

	for (; *s; ++s)
		if ((*s & 0xC0) != 0x80)
			++len;

	return len;
}

/*
 * Returns length of utf8 string in chars.
 */
size_t
utf8len_start_stop(const char *start, const char *stop)
{
	size_t		len = 0;

	for (; *start && start < stop ; ++start)
		if ((*start & 0xC0) != 0x80)
			++len;

	return len;
}

/*
 * Returns length of utf8 char in bytes
 */
inline int
utf8charlen(const char ch)
{
	if ((ch & 0x80) == 0)
		return 1;

	if ((ch & 0xF0) == 0xF0)
		return 4;

	if ((ch & 0xE0) == 0xE0)
		return 3;

	if ((ch & 0xC0) == 0xC0)
		return 2;

	return 1;
}

/*
 * This is an implementation of wcwidth() and wcswidth() as defined in
 * "The Single UNIX Specification, Version 2, The Open Group, 1997"
 * <http://www.UNIX-systems.org/online.html>
 *
 * Markus Kuhn -- 2001-09-08 -- public domain
 *
 * customised for PostgreSQL
 *
 * original available at : http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

struct mbinterval
{
	int		first;
	int		last;
};

/* auxiliary function for binary search in interval table */
inline static int
mbbisearch(wchar_t ucs, const struct mbinterval *table, int max)
{
	int			min = 0;

	if (ucs < table[0].first || ucs > table[max].last)
		return 0;

	while (max >= min)
	{
		int			mid;

		mid = (min + max) / 2;
		if (ucs > table[mid].last)
			min = mid + 1;
		else if (ucs < table[mid].first)
			max = mid - 1;
		else
			return 1;
	}

	return 0;
}

/* The following functions define the column width of an ISO 10646
 * character as follows:
 *
 *	  - The null character (U+0000) has a column width of 0.
 *
 *	  - Other C0/C1 control characters and DEL will lead to a return
 *		value of -1.
 *
 *	  - Non-spacing and enclosing combining characters (general
 *		category code Mn or Me in the Unicode database) have a
 *		column width of 0.
 *
 *	  - Other format characters (general category code Cf in the Unicode
 *		database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *	  - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *		have a column width of 0.
 *
 *	  - Spacing characters in the East Asian Wide (W) or East Asian
 *		FullWidth (F) category as defined in Unicode Technical
 *		Report #11 have a column width of 2.
 *
 *	  - All remaining characters (including all printable
 *		ISO 8859-1 and WGL4 characters, Unicode control characters,
 *		etc.) have a column width of 1.
 *
 * This implementation assumes that wchar_t characters are encoded
 * in ISO 10646.
 */
static int
ucs_wcwidth(wchar_t ucs)
{
#include "unicode_combining_table.h"
#include "unicode_east_asian_fw_table.h"

	/* test for 8-bit control characters */
	if (ucs == 0)
		return 0;

	if (ucs < 0x20 || (ucs >= 0x7f && ucs < 0xa0) || ucs > 0x0010ffff)
		return -1;

	/*
	 * binary search in table of non-spacing characters
	 *
	 * XXX: In the official Unicode sources, it is possible for a character to
	 * be described as both non-spacing and wide at the same time. As of
	 * Unicode 13.0, treating the non-spacing property as the determining
	 * factor for display width leads to the correct behavior, so do that
	 * search first.
	 */
	if (mbbisearch(ucs, combining,
				   sizeof(combining) / sizeof(struct mbinterval) - 1))
		return 0;

	/* binary search in table of wide characters */
	if (mbbisearch(ucs, east_asian_fw,
				   sizeof(east_asian_fw) / sizeof(struct mbinterval) - 1))
		return 2;

	return 1;
}

/*
 * Map a Unicode code point to UTF-8.  utf8string must have 4 bytes of
 * space allocated.
 */
unsigned char *
unicode_to_utf8(wchar_t c, unsigned char *utf8string, int *size)
{
	int		_size;

	if (c <= 0x7F)
	{
		utf8string[0] = c;
		_size = 1;
	}
	else if (c <= 0x7FF)
	{
		utf8string[0] = 0xC0 | ((c >> 6) & 0x1F);
		utf8string[1] = 0x80 | (c & 0x3F);
		_size = 2;
	}
	else if (c <= 0xFFFF)
	{
		utf8string[0] = 0xE0 | ((c >> 12) & 0x0F);
		utf8string[1] = 0x80 | ((c >> 6) & 0x3F);
		utf8string[2] = 0x80 | (c & 0x3F);
		_size = 3;
	}
	else
	{
		utf8string[0] = 0xF0 | ((c >> 18) & 0x07);
		utf8string[1] = 0x80 | ((c >> 12) & 0x3F);
		utf8string[2] = 0x80 | ((c >> 6) & 0x3F);
		utf8string[3] = 0x80 | (c & 0x3F);
		_size = 4;
	}

	if (size != NULL)
		*size = _size;

	return utf8string;
}



/*
 * Convert a UTF-8 character to a Unicode code point.
 * This is a one-character version of pg_utf2wchar_with_len.
 *
 * No error checks here, c must point to a long-enough string.
 */
inline static wchar_t
utf8_to_unicode(const unsigned char *c)
{
	if ((*c & 0x80) == 0)
		return (wchar_t) c[0];
	else if ((*c & 0xe0) == 0xc0)
		return (wchar_t) (((c[0] & 0x1f) << 6) |
						   (c[1] & 0x3f));
	else if ((*c & 0xf0) == 0xe0)
		return (wchar_t) (((c[0] & 0x0f) << 12) |
						   ((c[1] & 0x3f) << 6) |
						   (c[2] & 0x3f));
	else if ((*c & 0xf8) == 0xf0)
		return (wchar_t) (((c[0] & 0x07) << 18) |
						   ((c[1] & 0x3f) << 12) |
						   ((c[2] & 0x3f) << 6) |
						   (c[3] & 0x3f));
	else

#if __WCHAR_MAX__ > 0x10000

		/* that is an invalid code on purpose */
		return 0xffffffff;

#else

		/* that is an invalid code on purpose */
		return 0xffff;

#endif

}

inline int
utf_dsplen(const char *s)
{
	if (*s >= 0x20 && *s < 0x7f)
		return 1;

	return ucs_wcwidth(utf8_to_unicode((const unsigned char *) s));
}

inline static int
_utf_dsplen(const char *s)
{
	return ucs_wcwidth(utf8_to_unicode((const unsigned char *) s));
}

/*
 * Returns display length of \0 ended multibyte string.
 * The string is limited by max_bytes too.
 */
int
utf_string_dsplen(const char *s, int bytes)
{
	int			result = 0;

	while (bytes > 0)
	{
		const char c = *s;

		if (c == '\t')
		{
			do
			{
				result++;
			} while (result % 8 != 0);

			s += 1;
			bytes -= 1;
		}
		else if (c >= 0x20 && c < 0x7f)
		{
			s += 1;
			result += 1;
			bytes -= 1;
		}
		else if (c)
		{
			int clen  = utf8charlen(c);

			result += _utf_dsplen(s);

			s += clen;
			bytes -= clen;
		}
		else
			break;
	}

	return result;
}

int
utf_string_dsplen_multiline(const char *s,
							size_t max_bytes,
							bool *multiline,
							bool first_only,
							long int *digits,
							long int *others,
							int trim_rows)
{
	int			result = -1;
	int			rowlen = 0;
	int			nrows = 0;
	const char *ptr = s;

	*multiline = false;

	while (*ptr != '\0' && max_bytes > 0)
	{
		int		clen;

		if (!first_only)
		{
			if (*ptr >= '0' && *ptr <= '9')
				(*digits)++;
			else if (*ptr != '-' && *ptr != ' ' && *ptr != ':')
				(*others)++;
		}

		if (*ptr == '\n')
		{
			*multiline = true;

			result = rowlen > result ? rowlen : result;
			max_bytes -= 1;

			rowlen = 0;
			ptr += 1;

			if (first_only)
				break;

			if (trim_rows > 0 && ++nrows == trim_rows)
				break;

			continue;
		}

		clen = utf8charlen(*ptr);
		if (clen == 1 && *ptr == '\t')
		{
			/* this code is designed like pg_wcssize */
			do
			{
				rowlen++;
			} while (rowlen % 8 != 0);
		}
		else
			rowlen += utf_dsplen(ptr);

		ptr += clen;
		max_bytes -= clen;
	}

	result = rowlen > result ? rowlen : result;

	return result;
}


/*
 * This version of previous function uses similar calculation like
 * readline libarry - and supports terminal tabs.
 *
 * This code is based on demo ulfalizer/readline-and-ncurses
 *
 */
int
readline_utf_string_dsplen(const char *s, size_t max_bytes, size_t offset)
{
	int		result = 0;
	const char *ptr = s;

	while (*ptr != '\0' && max_bytes > 0)
	{
		int		clen = utf8charlen(*ptr);
		int		width = utf_dsplen(ptr);

		if (width > 0)
			result += width;
		else if (*ptr == '\t')
			result = ((result + offset + 8) & ~7) - offset;

		ptr += clen;
		max_bytes -= clen;
	}

	return result;
}

/*
 * following code is taken from starwing/luautf8 library.
 *
 */
typedef struct conv_table
{
	wchar_t		first;
	wchar_t		last;
	int			step;
	int			offset;
} conv_table;

typedef struct range_table
{
	wchar_t first;
	wchar_t last;
	int step;
} range_table;

static int
convert_char(conv_table *t, size_t size, wchar_t ucs)
{
	size_t begin, end;

	begin = 0;
	end = size;

	while (begin < end)
	{
		int mid = (begin + end) / 2;
		if (t[mid].last < ucs)
			begin = mid + 1;
		else if (t[mid].first > ucs)
			end = mid;
		else if ((ucs - t[mid].first) % t[mid].step == 0)
			return ucs + t[mid].offset;
		else
			return ucs;
	}

	return ucs;
}

static int
find_in_range(range_table *t, size_t size, wchar_t ucs)
{
	size_t begin, end;

	begin = 0;
	end = size;

	while (begin < end)
	{
		int mid = (begin + end) / 2;

		if (t[mid].last < ucs)
			begin = mid + 1;
		else if (t[mid].first > ucs)
			end = mid;
		else
			return (ucs - t[mid].first) % t[mid].step == 0;
	}

	return 0;
}

#define table_size(t) (sizeof(t)/sizeof((t)[0]))

int
utf8_tofold(const char *s)
{
	static struct conv_table tofold_table[] = {
		{ 0x41, 0x5A, 1, 32 }, { 0xB5, 0xB5, 1, 775 }, { 0xC0, 0xD6, 1, 32 },
		{ 0xD8, 0xDE, 1, 32 }, { 0x100, 0x12E, 2, 1 }, { 0x132, 0x136, 2, 1 },
		{ 0x139, 0x147, 2, 1 }, { 0x14A, 0x176, 2, 1 }, { 0x178, 0x178, 1, -121 },
		{ 0x179, 0x17D, 2, 1 }, { 0x17F, 0x17F, 1, -268 }, { 0x181, 0x181, 1, 210 },
		{ 0x182, 0x184, 2, 1 }, { 0x186, 0x186, 1, 206 }, { 0x187, 0x187, 1, 1 },
		{ 0x189, 0x18A, 1, 205 }, { 0x18B, 0x18B, 1, 1 }, { 0x18E, 0x18E, 1, 79 },
		{ 0x18F, 0x18F, 1, 202 }, { 0x190, 0x190, 1, 203 }, { 0x191, 0x191, 1, 1 },
		{ 0x193, 0x193, 1, 205 }, { 0x194, 0x194, 1, 207 }, { 0x196, 0x196, 1, 211 },
		{ 0x197, 0x197, 1, 209 }, { 0x198, 0x198, 1, 1 }, { 0x19C, 0x19C, 1, 211 },
		{ 0x19D, 0x19D, 1, 213 }, { 0x19F, 0x19F, 1, 214 }, { 0x1A0, 0x1A4, 2, 1 },
		{ 0x1A6, 0x1A6, 1, 218 }, { 0x1A7, 0x1A7, 1, 1 }, { 0x1A9, 0x1A9, 1, 218 },
		{ 0x1AC, 0x1AC, 1, 1 }, { 0x1AE, 0x1AE, 1, 218 }, { 0x1AF, 0x1AF, 1, 1 },
		{ 0x1B1, 0x1B2, 1, 217 }, { 0x1B3, 0x1B5, 2, 1 }, { 0x1B7, 0x1B7, 1, 219 },
		{ 0x1B8, 0x1BC, 4, 1 }, { 0x1C4, 0x1C4, 1, 2 }, { 0x1C5, 0x1C5, 1, 1 },
		{ 0x1C7, 0x1C7, 1, 2 }, { 0x1C8, 0x1C8, 1, 1 }, { 0x1CA, 0x1CA, 1, 2 },
		{ 0x1CB, 0x1DB, 2, 1 }, { 0x1DE, 0x1EE, 2, 1 }, { 0x1F1, 0x1F1, 1, 2 },
		{ 0x1F2, 0x1F4, 2, 1 }, { 0x1F6, 0x1F6, 1, -97 }, { 0x1F7, 0x1F7, 1, -56 },
		{ 0x1F8, 0x21E, 2, 1 }, { 0x220, 0x220, 1, -130 }, { 0x222, 0x232, 2, 1 },
		{ 0x23A, 0x23A, 1, 10795 }, { 0x23B, 0x23B, 1, 1 }, { 0x23D, 0x23D, 1, -163 },
		{ 0x23E, 0x23E, 1, 10792 }, { 0x241, 0x241, 1, 1 }, { 0x243, 0x243, 1, -195 },
		{ 0x244, 0x244, 1, 69 }, { 0x245, 0x245, 1, 71 }, { 0x246, 0x24E, 2, 1 },
		{ 0x345, 0x345, 1, 116 }, { 0x370, 0x372, 2, 1 }, { 0x376, 0x376, 1, 1 },
		{ 0x37F, 0x37F, 1, 116 }, { 0x386, 0x386, 1, 38 }, { 0x388, 0x38A, 1, 37 },
		{ 0x38C, 0x38C, 1, 64 }, { 0x38E, 0x38F, 1, 63 }, { 0x391, 0x3A1, 1, 32 },
		{ 0x3A3, 0x3AB, 1, 32 }, { 0x3C2, 0x3C2, 1, 1 }, { 0x3CF, 0x3CF, 1, 8 },
		{ 0x3D0, 0x3D0, 1, -30 }, { 0x3D1, 0x3D1, 1, -25 }, { 0x3D5, 0x3D5, 1, -15 },
		{ 0x3D6, 0x3D6, 1, -22 }, { 0x3D8, 0x3EE, 2, 1 }, { 0x3F0, 0x3F0, 1, -54 },
		{ 0x3F1, 0x3F1, 1, -48 }, { 0x3F4, 0x3F4, 1, -60 }, { 0x3F5, 0x3F5, 1, -64 },
		{ 0x3F7, 0x3F7, 1, 1 }, { 0x3F9, 0x3F9, 1, -7 }, { 0x3FA, 0x3FA, 1, 1 },
		{ 0x3FD, 0x3FF, 1, -130 }, { 0x400, 0x40F, 1, 80 }, { 0x410, 0x42F, 1, 32 },
		{ 0x460, 0x480, 2, 1 }, { 0x48A, 0x4BE, 2, 1 }, { 0x4C0, 0x4C0, 1, 15 },
		{ 0x4C1, 0x4CD, 2, 1 }, { 0x4D0, 0x52E, 2, 1 }, { 0x531, 0x556, 1, 48 },
		{ 0x10A0, 0x10C5, 1, 7264 }, { 0x10C7, 0x10CD, 6, 7264 }, { 0x13F8, 0x13FD, 1, -8 },
		{ 0x1E00, 0x1E94, 2, 1 }, { 0x1E9B, 0x1E9B, 1, -58 }, { 0x1E9E, 0x1E9E, 1, -7615 },
		{ 0x1EA0, 0x1EFE, 2, 1 }, { 0x1F08, 0x1F0F, 1, -8 }, { 0x1F18, 0x1F1D, 1, -8 },
		{ 0x1F28, 0x1F2F, 1, -8 }, { 0x1F38, 0x1F3F, 1, -8 }, { 0x1F48, 0x1F4D, 1, -8 },
		{ 0x1F59, 0x1F5F, 2, -8 }, { 0x1F68, 0x1F6F, 1, -8 }, { 0x1F88, 0x1F8F, 1, -8 },
		{ 0x1F98, 0x1F9F, 1, -8 }, { 0x1FA8, 0x1FAF, 1, -8 }, { 0x1FB8, 0x1FB9, 1, -8 },
		{ 0x1FBA, 0x1FBB, 1, -74 }, { 0x1FBC, 0x1FBC, 1, -9 }, { 0x1FBE, 0x1FBE, 1, -7173 },
		{ 0x1FC8, 0x1FCB, 1, -86 }, { 0x1FCC, 0x1FCC, 1, -9 }, { 0x1FD8, 0x1FD9, 1, -8 },
		{ 0x1FDA, 0x1FDB, 1, -100 }, { 0x1FE8, 0x1FE9, 1, -8 }, { 0x1FEA, 0x1FEB, 1, -112 },
		{ 0x1FEC, 0x1FEC, 1, -7 }, { 0x1FF8, 0x1FF9, 1, -128 }, { 0x1FFA, 0x1FFB, 1, -126 },
		{ 0x1FFC, 0x1FFC, 1, -9 },{ 0x2126, 0x2126, 1, -7517 }, { 0x212A, 0x212A, 1, -8383 },
		{ 0x212B, 0x212B, 1, -8262 }, { 0x2132, 0x2132, 1, 28 }, { 0x2160, 0x216F, 1, 16 },
		{ 0x2183, 0x2183, 1, 1 }, { 0x24B6, 0x24CF, 1, 26 }, { 0x2C00, 0x2C2E, 1, 48 },
		{ 0x2C60, 0x2C60, 1, 1 }, { 0x2C62, 0x2C62, 1, -10743 }, { 0x2C63, 0x2C63, 1, -3814 },
		{ 0x2C64, 0x2C64, 1, -10727 }, { 0x2C67, 0x2C6B, 2, 1 }, { 0x2C6D, 0x2C6D, 1, -10780 },
		{ 0x2C6E, 0x2C6E, 1, -10749 }, { 0x2C6F, 0x2C6F, 1, -10783 }, { 0x2C70, 0x2C70, 1, -10782 },
		{ 0x2C72, 0x2C75, 3, 1 },{ 0x2C7E, 0x2C7F, 1, -10815 }, { 0x2C80, 0x2CE2, 2, 1 },
		{ 0x2CEB, 0x2CED, 2, 1 }, { 0x2CF2, 0xA640, 31054, 1 }, { 0xA642, 0xA66C, 2, 1 },
		{ 0xA680, 0xA69A, 2, 1 }, { 0xA722, 0xA72E, 2, 1 }, { 0xA732, 0xA76E, 2, 1 },
		{ 0xA779, 0xA77B, 2, 1 }, { 0xA77D, 0xA77D, 1, -35332 }, { 0xA77E, 0xA786, 2, 1 },
		{ 0xA78B, 0xA78B, 1, 1 }, { 0xA78D, 0xA78D, 1, -42280 }, { 0xA790, 0xA792, 2, 1 },
		{ 0xA796, 0xA7A8, 2, 1 }, { 0xA7AA, 0xA7AA, 1, -42308 }, { 0xA7AB, 0xA7AB, 1, -42319 },
		{ 0xA7AC, 0xA7AC, 1, -42315 },{ 0xA7AD, 0xA7AD, 1, -42305 }, { 0xA7B0, 0xA7B0, 1, -42258 },
		{ 0xA7B1, 0xA7B1, 1, -42282 }, { 0xA7B2, 0xA7B2, 1, -42261 }, { 0xA7B3, 0xA7B3, 1, 928 },
		{ 0xA7B4, 0xA7B6, 2, 1 }, { 0xAB70, 0xABBF, 1, -38864 }, { 0xFF21, 0xFF3A, 1, 32 }

#if __WCHAR_MAX__ > 0x10000

		, { 0x10400, 0x10427, 1, 40 }, { 0x10C80, 0x10CB2, 1, 64 }, { 0x118A0, 0x118BF, 1, 32 }

#endif

	};

	return convert_char(tofold_table, table_size(tofold_table),
						  utf8_to_unicode((const unsigned char *) s));
}

const char *
utf8_nstrstr_with_sizes(const char *haystack,
						int haystack_size,
						const char *needle,
						int needle_size)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	const char *haystack_end, *needle_end;
	int			f1 = 0;
	int			needle_char_len = 0; /* be compiler quiet */

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	haystack_end = haystack + haystack_size;
	needle_end = needle + needle_size;

	while (needle_cur < needle_end)
	{
		int			f2;

		if (haystack_cur == haystack_end)
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			needle_char_len = utf8charlen(*needle_cur);
			f1 = utf8_tofold(needle_cur);
		}

		f2 = utf8_tofold(haystack_cur);
		if (f1 == f2)
		{
			needle_cur += needle_char_len;
			haystack_cur += utf8charlen(*haystack_cur);
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += utf8charlen(*haystack);
		}
	}

	return haystack;
}

bool
utf8_nstarts_with_with_sizes(const char *str,
							 int str_size,
							 const char *pattern,
							 int pattern_size)
{
	while (pattern_size > 0)
	{
		int			bytes_c;
		int			bytes_p;

		if (str_size <= 0)
			return false;

		bytes_c = utf8charlen(*str);
		bytes_p = utf8charlen(*pattern);

		if (bytes_c != bytes_p ||
			memcmp(str, pattern, bytes_c) != 0)
		{
			int		f1, f2;

			f1 = utf8_tofold(str);
			f2 = utf8_tofold(pattern);

			if (f1 != f2)
				return false;
		}

		str += bytes_c;
		str_size -= bytes_c;

		pattern += bytes_p;
		pattern_size -= bytes_p;
	}

	return true;
}

const char *
utf8_nstrstr(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int			f1 = 0;
	int			needle_char_len = 0; /* be compiler quiet */

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		int			f2;

		if (*haystack_cur == '\0')
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			needle_char_len = utf8charlen(*needle_cur);
			f1 = utf8_tofold(needle_cur);
		}

		f2 = utf8_tofold(haystack_cur);
		if (f1 == f2)
		{
			needle_cur += needle_char_len;
			haystack_cur += utf8charlen(*haystack_cur);
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += utf8charlen(*haystack);
		}
	}

	return haystack;
}

/*
 * Special string searching, lower chars are case insensitive,
 * upper chars are case sensitive.
 */
const char *
utf8_nstrstr_ignore_lower_case(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int			f1 = 0;
	int			needle_char_len = 0;
	bool		needle_char_is_upper = false;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		int			haystack_char_len;
		bool		eq;

		if (*haystack_cur == '\0')
			return NULL;

		haystack_char_len = utf8charlen(*haystack_cur);

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			needle_char_len = utf8charlen(*needle_cur);
			needle_char_is_upper = utf8_isupper(needle_cur);
			f1 = utf8_tofold(needle_cur);
		}

		if (needle_char_is_upper)
		{
			/* case sensitive */
			if (needle_char_len == haystack_char_len)
				eq = memcmp(haystack_cur, needle_cur, needle_char_len) == 0;
			else
				eq = false;
		}
		else
		{
			int			f2;

			/* case insensitive */
			f2 = utf8_tofold(haystack_cur);
			eq = f1 == f2;
		}

		if (eq)
		{
			needle_cur += needle_char_len;
			haystack_cur += haystack_char_len;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += utf8charlen(*haystack);
		}
	}

	return haystack;
}

bool
utf8_isupper(const char *s)
{
	static struct range_table upper_table[] = {
		{ 0x41, 0x5A, 1 }, { 0xC0, 0xD6, 1 }, { 0xD8, 0xDE, 1 },
		{ 0x100, 0x136, 2 }, { 0x139, 0x147, 2 }, { 0x14A, 0x178, 2 },
		{ 0x179, 0x17D, 2 }, { 0x181, 0x182, 1 }, { 0x184, 0x186, 2 },
		{ 0x187, 0x189, 2 }, { 0x18A, 0x18B, 1 }, { 0x18E, 0x191, 1 },
		{ 0x193, 0x194, 1 }, { 0x196, 0x198, 1 }, { 0x19C, 0x19D, 1 },
		{ 0x19F, 0x1A0, 1 }, { 0x1A2, 0x1A6, 2 }, { 0x1A7, 0x1A9, 2 },
		{ 0x1AC, 0x1AE, 2 }, { 0x1AF, 0x1B1, 2 }, { 0x1B2, 0x1B3, 1 },
		{ 0x1B5, 0x1B7, 2 }, { 0x1B8, 0x1BC, 4 }, { 0x1C4, 0x1CD, 3 },
		{ 0x1CF, 0x1DB, 2 }, { 0x1DE, 0x1EE, 2 }, { 0x1F1, 0x1F4, 3 },
		{ 0x1F6, 0x1F8, 1 }, { 0x1FA, 0x232, 2 }, { 0x23A, 0x23B, 1 },
		{ 0x23D, 0x23E, 1 }, { 0x241, 0x243, 2 }, { 0x244, 0x246, 1 },
		{ 0x248, 0x24E, 2 }, { 0x370, 0x372, 2 }, { 0x376, 0x37F, 9 },
		{ 0x386, 0x388, 2 }, { 0x389, 0x38A, 1 }, { 0x38C, 0x38E, 2 },
		{ 0x38F, 0x391, 2 }, { 0x392, 0x3A1, 1 }, { 0x3A3, 0x3AB, 1 },
		{ 0x3CF, 0x3D2, 3 }, { 0x3D3, 0x3D4, 1 }, { 0x3D8, 0x3EE, 2 },
		{ 0x3F4, 0x3F7, 3 }, { 0x3F9, 0x3FA, 1 }, { 0x3FD, 0x42F, 1 },
		{ 0x460, 0x480, 2 }, { 0x48A, 0x4C0, 2 }, { 0x4C1, 0x4CD, 2 },
		{ 0x4D0, 0x52E, 2 }, { 0x531, 0x556, 1 }, { 0x10A0, 0x10C5, 1 },
		{ 0x10C7, 0x10CD, 6 }, { 0x13A0, 0x13F5, 1 }, { 0x1E00, 0x1E94, 2 },
		{ 0x1E9E, 0x1EFE, 2 }, { 0x1F08, 0x1F0F, 1 }, { 0x1F18, 0x1F1D, 1 },
		{ 0x1F28, 0x1F2F, 1 }, { 0x1F38, 0x1F3F, 1 }, { 0x1F48, 0x1F4D, 1 },
		{ 0x1F59, 0x1F5F, 2 }, { 0x1F68, 0x1F6F, 1 }, { 0x1FB8, 0x1FBB, 1 },
		{ 0x1FC8, 0x1FCB, 1 }, { 0x1FD8, 0x1FDB, 1 }, { 0x1FE8, 0x1FEC, 1 },
		{ 0x1FF8, 0x1FFB, 1 }, { 0x2102, 0x2107, 5 }, { 0x210B, 0x210D, 1 },
		{ 0x2110, 0x2112, 1 }, { 0x2115, 0x2119, 4 }, { 0x211A, 0x211D, 1 },
		{ 0x2124, 0x212A, 2 }, { 0x212B, 0x212D, 1 }, { 0x2130, 0x2133, 1 },
		{ 0x213E, 0x213F, 1 }, { 0x2145, 0x2160, 27 }, { 0x2161, 0x216F, 1 },
		{ 0x2183, 0x24B6, 819 }, { 0x24B7, 0x24CF, 1 }, { 0x2C00, 0x2C2E, 1 },
		{ 0x2C60, 0x2C62, 2 }, { 0x2C63, 0x2C64, 1 }, { 0x2C67, 0x2C6D, 2 },
		{ 0x2C6E, 0x2C70, 1 }, { 0x2C72, 0x2C75, 3 }, { 0x2C7E, 0x2C80, 1 },
		{ 0x2C82, 0x2CE2, 2 }, { 0x2CEB, 0x2CED, 2 }, { 0x2CF2, 0xA640, 31054 },
		{ 0xA642, 0xA66C, 2 }, { 0xA680, 0xA69A, 2 }, { 0xA722, 0xA72E, 2 },
		{ 0xA732, 0xA76E, 2 }, { 0xA779, 0xA77D, 2 }, { 0xA77E, 0xA786, 2 },
		{ 0xA78B, 0xA78D, 2 }, { 0xA790, 0xA792, 2 }, { 0xA796, 0xA7AA, 2 },
		{ 0xA7AB, 0xA7AD, 1 }, { 0xA7B0, 0xA7B4, 1 }, { 0xA7B6, 0xFF21, 22379 },
		{ 0xFF22, 0xFF3A, 1 }

#if __WCHAR_MAX__ > 0x10000

							,    { 0x10400, 0x10427, 1 }, { 0x10C80, 0x10CB2, 1 },
		{ 0x118A0, 0x118BF, 1 }, { 0x1D400, 0x1D419, 1 }, { 0x1D434, 0x1D44D, 1 },
		{ 0x1D468, 0x1D481, 1 }, { 0x1D49C, 0x1D49E, 2 }, { 0x1D49F, 0x1D4A5, 3 },
		{ 0x1D4A6, 0x1D4A9, 3 }, { 0x1D4AA, 0x1D4AC, 1 }, { 0x1D4AE, 0x1D4B5, 1 },
		{ 0x1D4D0, 0x1D4E9, 1 }, { 0x1D504, 0x1D505, 1 }, { 0x1D507, 0x1D50A, 1 },
		{ 0x1D50D, 0x1D514, 1 }, { 0x1D516, 0x1D51C, 1 }, { 0x1D538, 0x1D539, 1 },
		{ 0x1D53B, 0x1D53E, 1 }, { 0x1D540, 0x1D544, 1 }, { 0x1D546, 0x1D54A, 4 },
		{ 0x1D54B, 0x1D550, 1 }, { 0x1D56C, 0x1D585, 1 }, { 0x1D5A0, 0x1D5B9, 1 },
		{ 0x1D5D4, 0x1D5ED, 1 }, { 0x1D608, 0x1D621, 1 }, { 0x1D63C, 0x1D655, 1 },
		{ 0x1D670, 0x1D689, 1 }, { 0x1D6A8, 0x1D6C0, 1 }, { 0x1D6E2, 0x1D6FA, 1 },
		{ 0x1D71C, 0x1D734, 1 }, { 0x1D756, 0x1D76E, 1 }, { 0x1D790, 0x1D7A8, 1 },
		{ 0x1D7CA, 0x1F130, 6502 }, { 0x1F131, 0x1F149, 1 }, { 0x1F150, 0x1F169, 1 },
		{ 0x1F170, 0x1F189, 1 },

#endif

	};

	return find_in_range(upper_table, table_size(upper_table),
						utf8_to_unicode((const unsigned char *) s)) != 0;
}

int
utf2wchar_with_len(const unsigned char *from, wchar_t *to, int len)
{
	int			cnt = 0;
	unsigned int	c1,
					c2,
					c3,
					c4;

	while (len > 0 && *from)
	{
		if ((*from & 0x80) == 0)
		{
			*to = *from++;
			len--;
		}
		else if ((*from & 0xe0) == 0xc0)
		{
			if (len < 2)
				break;			/* drop trailing incomplete char */
			c1 = *from++ & 0x1f;
			c2 = *from++ & 0x3f;
			*to = (c1 << 6) | c2;
			len -= 2;
		}
		else if ((*from & 0xf0) == 0xe0)
		{
			if (len < 3)
				break;			/* drop trailing incomplete char */
			c1 = *from++ & 0x0f;
			c2 = *from++ & 0x3f;
			c3 = *from++ & 0x3f;
			*to = (c1 << 12) | (c2 << 6) | c3;
			len -= 3;
		}
		else if ((*from & 0xf8) == 0xf0)
		{
			if (len < 4)
				break;			/* drop trailing incomplete char */
			c1 = *from++ & 0x07;
			c2 = *from++ & 0x3f;
			c3 = *from++ & 0x3f;
			c4 = *from++ & 0x3f;
			*to = (c1 << 18) | (c2 << 12) | (c3 << 6) | c4;
			len -= 4;
		}
		else
		{
			/* treat a bogus char as length 1; not ours to raise error */
			*to = *from++;
			len--;
		}
		to++;
		cnt++;
	}
	*to = 0;
	return cnt;
}
