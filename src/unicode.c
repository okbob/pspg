/*-------------------------------------------------------------------------
 *
 * unicode.c
 *	  unicode and wide chars routines
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/unicode.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <stdbool.h>

#include "unicode.h"
#include "string.h"

/*
 * Returns length of utf8 string in chars.
 */
size_t
utf8len(char *s)
{
	size_t len = 0;

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
	size_t len = 0;

	for (; *start && start < stop ; ++start)
		if ((*start & 0xC0) != 0x80)
			++len;
	return len;
}

/*
 * Returns length of utf8 char in bytes
 */
inline int
utf8charlen(char ch)
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
	unsigned short first;
	unsigned short last;
};

/* auxiliary function for binary search in interval table */
static int
mbbisearch(wchar_t ucs, const struct mbinterval *table, int max)
{
	int			min = 0;
	int			mid;

	if (ucs < table[0].first || ucs > table[max].last)
		return 0;
	while (max >= min)
	{
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
	/* sorted list of non-overlapping intervals of non-spacing characters */
	static const struct mbinterval combining[] = {
		{0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD},
		{0x05BF, 0x05BF}, {0x05C1, 0x05C2}, {0x05C4, 0x05C5},
		{0x05C7, 0x05C7}, {0x0610, 0x061A}, {0x064B, 0x065F},
		{0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x06DF, 0x06E4},
		{0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0711, 0x0711},
		{0x0730, 0x074A}, {0x07A6, 0x07B0}, {0x07EB, 0x07F3},
		{0x07FD, 0x07FD}, {0x0816, 0x0819}, {0x081B, 0x0823},
		{0x0825, 0x0827}, {0x0829, 0x082D}, {0x0859, 0x085B},
		{0x08D3, 0x08E1}, {0x08E3, 0x0902}, {0x093A, 0x093A},
		{0x093C, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D},
		{0x0951, 0x0957}, {0x0962, 0x0963}, {0x0981, 0x0981},
		{0x09BC, 0x09BC}, {0x09C1, 0x09C4}, {0x09CD, 0x09CD},
		{0x09E2, 0x09E3}, {0x09FE, 0x0A02}, {0x0A3C, 0x0A3C},
		{0x0A41, 0x0A51}, {0x0A70, 0x0A71}, {0x0A75, 0x0A75},
		{0x0A81, 0x0A82}, {0x0ABC, 0x0ABC}, {0x0AC1, 0x0AC8},
		{0x0ACD, 0x0ACD}, {0x0AE2, 0x0AE3}, {0x0AFA, 0x0B01},
		{0x0B3C, 0x0B3C}, {0x0B3F, 0x0B3F}, {0x0B41, 0x0B44},
		{0x0B4D, 0x0B56}, {0x0B62, 0x0B63}, {0x0B82, 0x0B82},
		{0x0BC0, 0x0BC0}, {0x0BCD, 0x0BCD}, {0x0C00, 0x0C00},
		{0x0C04, 0x0C04}, {0x0C3E, 0x0C40}, {0x0C46, 0x0C56},
		{0x0C62, 0x0C63}, {0x0C81, 0x0C81}, {0x0CBC, 0x0CBC},
		{0x0CBF, 0x0CBF}, {0x0CC6, 0x0CC6}, {0x0CCC, 0x0CCD},
		{0x0CE2, 0x0CE3}, {0x0D00, 0x0D01}, {0x0D3B, 0x0D3C},
		{0x0D41, 0x0D44}, {0x0D4D, 0x0D4D}, {0x0D62, 0x0D63},
		{0x0DCA, 0x0DCA}, {0x0DD2, 0x0DD6}, {0x0E31, 0x0E31},
		{0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1},
		{0x0EB4, 0x0EBC}, {0x0EC8, 0x0ECD}, {0x0F18, 0x0F19},
		{0x0F35, 0x0F35}, {0x0F37, 0x0F37}, {0x0F39, 0x0F39},
		{0x0F71, 0x0F7E}, {0x0F80, 0x0F84}, {0x0F86, 0x0F87},
		{0x0F8D, 0x0FBC}, {0x0FC6, 0x0FC6}, {0x102D, 0x1030},
		{0x1032, 0x1037}, {0x1039, 0x103A}, {0x103D, 0x103E},
		{0x1058, 0x1059}, {0x105E, 0x1060}, {0x1071, 0x1074},
		{0x1082, 0x1082}, {0x1085, 0x1086}, {0x108D, 0x108D},
		{0x109D, 0x109D}, {0x135D, 0x135F}, {0x1712, 0x1714},
		{0x1732, 0x1734}, {0x1752, 0x1753}, {0x1772, 0x1773},
		{0x17B4, 0x17B5}, {0x17B7, 0x17BD}, {0x17C6, 0x17C6},
		{0x17C9, 0x17D3}, {0x17DD, 0x17DD}, {0x180B, 0x180D},
		{0x1885, 0x1886}, {0x18A9, 0x18A9}, {0x1920, 0x1922},
		{0x1927, 0x1928}, {0x1932, 0x1932}, {0x1939, 0x193B},
		{0x1A17, 0x1A18}, {0x1A1B, 0x1A1B}, {0x1A56, 0x1A56},
		{0x1A58, 0x1A60}, {0x1A62, 0x1A62}, {0x1A65, 0x1A6C},
		{0x1A73, 0x1A7F}, {0x1AB0, 0x1B03}, {0x1B34, 0x1B34},
		{0x1B36, 0x1B3A}, {0x1B3C, 0x1B3C}, {0x1B42, 0x1B42},
		{0x1B6B, 0x1B73}, {0x1B80, 0x1B81}, {0x1BA2, 0x1BA5},
		{0x1BA8, 0x1BA9}, {0x1BAB, 0x1BAD}, {0x1BE6, 0x1BE6},
		{0x1BE8, 0x1BE9}, {0x1BED, 0x1BED}, {0x1BEF, 0x1BF1},
		{0x1C2C, 0x1C33}, {0x1C36, 0x1C37}, {0x1CD0, 0x1CD2},
		{0x1CD4, 0x1CE0}, {0x1CE2, 0x1CE8}, {0x1CED, 0x1CED},
		{0x1CF4, 0x1CF4}, {0x1CF8, 0x1CF9}, {0x1DC0, 0x1DFF},
		{0x20D0, 0x20F0}, {0x2CEF, 0x2CF1}, {0x2D7F, 0x2D7F},
		{0x2DE0, 0x2DFF}, {0x302A, 0x302D}, {0x3099, 0x309A},
		{0xA66F, 0xA672}, {0xA674, 0xA67D}, {0xA69E, 0xA69F},
		{0xA6F0, 0xA6F1}, {0xA802, 0xA802}, {0xA806, 0xA806},
		{0xA80B, 0xA80B}, {0xA825, 0xA826}, {0xA8C4, 0xA8C5},
		{0xA8E0, 0xA8F1}, {0xA8FF, 0xA8FF}, {0xA926, 0xA92D},
		{0xA947, 0xA951}, {0xA980, 0xA982}, {0xA9B3, 0xA9B3},
		{0xA9B6, 0xA9B9}, {0xA9BC, 0xA9BD}, {0xA9E5, 0xA9E5},
		{0xAA29, 0xAA2E}, {0xAA31, 0xAA32}, {0xAA35, 0xAA36},
		{0xAA43, 0xAA43}, {0xAA4C, 0xAA4C}, {0xAA7C, 0xAA7C},
		{0xAAB0, 0xAAB0}, {0xAAB2, 0xAAB4}, {0xAAB7, 0xAAB8},
		{0xAABE, 0xAABF}, {0xAAC1, 0xAAC1}, {0xAAEC, 0xAAED},
		{0xAAF6, 0xAAF6}, {0xABE5, 0xABE5}, {0xABE8, 0xABE8},
		{0xABED, 0xABED}, {0xFB1E, 0xFB1E}, {0xFE00, 0xFE0F},
		{0xFE20, 0xFE2F},
	};

	/* test for 8-bit control characters */
	if (ucs == 0)
		return 0;

	if (ucs < 0x20 || (ucs >= 0x7f && ucs < 0xa0) || ucs > 0x0010ffff)
		return -1;

	/* binary search in table of non-spacing characters */
	if (mbbisearch(ucs, combining,
				   sizeof(combining) / sizeof(struct mbinterval) - 1))
		return 0;

	/*
	 * if we arrive here, ucs is not a combining or C0/C1 control character
	 */

	return 1 +
		(ucs >= 0x1100 &&
		 (ucs <= 0x115f ||		/* Hangul Jamo init. consonants */
		  (ucs >= 0x2e80 && ucs <= 0xa4cf && (ucs & ~0x0011) != 0x300a &&
		   ucs != 0x303f) ||	/* CJK ... Yi */
		  (ucs >= 0xac00 && ucs <= 0xd7a3) ||	/* Hangul Syllables */
		  (ucs >= 0xf900 && ucs <= 0xfaff) ||	/* CJK Compatibility
												 * Ideographs */
		  (ucs >= 0xfe30 && ucs <= 0xfe6f) ||	/* CJK Compatibility Forms */
		  (ucs >= 0xff00 && ucs <= 0xff5f) ||	/* Fullwidth Forms */
		  (ucs >= 0xffe0 && ucs <= 0xffe6) ||
		  (ucs >= 0x20000 && ucs <= 0x2ffff)));
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
wchar_t
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
	if (*s == ' ')
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
utf_string_dsplen(const char *s, size_t max_bytes)
{
	int result = 0;
	const char *ptr = s;

	while (*ptr != '\0' && max_bytes > 0)
	{
		if (*ptr == ' ')
		{
			ptr += 1;
			result += 1;
			max_bytes -= 1;
		} 
		else
		{
			int		clen;

			clen  = utf8charlen(*ptr);
			result += _utf_dsplen(ptr);
			ptr += clen;
			max_bytes -= clen;
		}
	}

	return result;
}

int
utf_string_dsplen_multiline(const char *s, size_t max_bytes, bool *multiline, bool first_only, long int *digits, long int *others)
{
	int result = -1;
	int		rowlen = 0;
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

			continue;
		}

		clen = utf8charlen(*ptr);
		rowlen += utf_dsplen(ptr);
		ptr += clen;
		max_bytes -= clen;
	}

	return result != -1 ? result : rowlen;
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
	int result = 0;
	const char *ptr = s;

	while (*ptr != '\0' && max_bytes > 0)
	{
		int		clen = utf8charlen(*ptr);
		int		dsplen = utf_dsplen(ptr);

		if (dsplen > 0)
			result += dsplen;
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
    wchar_t first;
    wchar_t last;
    int step;
    int offset;
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
	int		f1 = 0, f2 = 0;
	int		needle_char_len = 0; /* be compiler quiet */

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


const char *
utf8_nstrstr(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0, f2 = 0;
	int		needle_char_len = 0; /* be compiler quiet */

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
	int		f1 = 0, f2 = 0;
	bool	eq;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		int		haystack_char_len;
		int		needle_char_len = 0;
		bool	needle_char_is_upper =  false;

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
