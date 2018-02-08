/*-------------------------------------------------------------------------
 *
 * unicode.c
 *	  unicode and wide chars routines
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/unicode.c
 *
 *-------------------------------------------------------------------------
 */

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
int
utf8charlen(char ch)
{
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
		{0x0300, 0x034E}, {0x0360, 0x0362}, {0x0483, 0x0486},
		{0x0488, 0x0489}, {0x0591, 0x05A1}, {0x05A3, 0x05B9},
		{0x05BB, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
		{0x05C4, 0x05C4}, {0x064B, 0x0655}, {0x0670, 0x0670},
		{0x06D6, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED},
		{0x070F, 0x070F}, {0x0711, 0x0711}, {0x0730, 0x074A},
		{0x07A6, 0x07B0}, {0x0901, 0x0902}, {0x093C, 0x093C},
		{0x0941, 0x0948}, {0x094D, 0x094D}, {0x0951, 0x0954},
		{0x0962, 0x0963}, {0x0981, 0x0981}, {0x09BC, 0x09BC},
		{0x09C1, 0x09C4}, {0x09CD, 0x09CD}, {0x09E2, 0x09E3},
		{0x0A02, 0x0A02}, {0x0A3C, 0x0A3C}, {0x0A41, 0x0A42},
		{0x0A47, 0x0A48}, {0x0A4B, 0x0A4D}, {0x0A70, 0x0A71},
		{0x0A81, 0x0A82}, {0x0ABC, 0x0ABC}, {0x0AC1, 0x0AC5},
		{0x0AC7, 0x0AC8}, {0x0ACD, 0x0ACD}, {0x0B01, 0x0B01},
		{0x0B3C, 0x0B3C}, {0x0B3F, 0x0B3F}, {0x0B41, 0x0B43},
		{0x0B4D, 0x0B4D}, {0x0B56, 0x0B56}, {0x0B82, 0x0B82},
		{0x0BC0, 0x0BC0}, {0x0BCD, 0x0BCD}, {0x0C3E, 0x0C40},
		{0x0C46, 0x0C48}, {0x0C4A, 0x0C4D}, {0x0C55, 0x0C56},
		{0x0CBF, 0x0CBF}, {0x0CC6, 0x0CC6}, {0x0CCC, 0x0CCD},
		{0x0D41, 0x0D43}, {0x0D4D, 0x0D4D}, {0x0DCA, 0x0DCA},
		{0x0DD2, 0x0DD4}, {0x0DD6, 0x0DD6}, {0x0E31, 0x0E31},
		{0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1},
		{0x0EB4, 0x0EB9}, {0x0EBB, 0x0EBC}, {0x0EC8, 0x0ECD},
		{0x0F18, 0x0F19}, {0x0F35, 0x0F35}, {0x0F37, 0x0F37},
		{0x0F39, 0x0F39}, {0x0F71, 0x0F7E}, {0x0F80, 0x0F84},
		{0x0F86, 0x0F87}, {0x0F90, 0x0F97}, {0x0F99, 0x0FBC},
		{0x0FC6, 0x0FC6}, {0x102D, 0x1030}, {0x1032, 0x1032},
		{0x1036, 0x1037}, {0x1039, 0x1039}, {0x1058, 0x1059},
		{0x1160, 0x11FF}, {0x17B7, 0x17BD}, {0x17C6, 0x17C6},
		{0x17C9, 0x17D3}, {0x180B, 0x180E}, {0x18A9, 0x18A9},
		{0x200B, 0x200F}, {0x202A, 0x202E}, {0x206A, 0x206F},
		{0x20D0, 0x20E3}, {0x302A, 0x302F}, {0x3099, 0x309A},
		{0xFB1E, 0xFB1E}, {0xFE20, 0xFE23}, {0xFEFF, 0xFEFF},
		{0xFFF9, 0xFFFB}
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
		/* that is an invalid code on purpose */
		return 0xffffffff;
}

int
utf_dsplen(const char *s)
{
	return ucs_wcwidth(utf8_to_unicode((const unsigned char *) s));
}

/*
 * following code is taken from starwing/luautf8 library.
 *
 */
typedef struct conv_table
{
    unsigned int first;
    unsigned int last;
    int step;
    int offset;
} conv_table;

typedef struct range_table
{
    unsigned int first;
    unsigned int last;
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

static int
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
		{ 0xA7B4, 0xA7B6, 2, 1 }, { 0xAB70, 0xABBF, 1, -38864 }, { 0xFF21, 0xFF3A, 1, 32 },
		{ 0x10400, 0x10427, 1, 40 }, { 0x10C80, 0x10CB2, 1, 64 }, { 0x118A0, 0x118BF, 1, 32 }
	};

	return convert_char(tofold_table, table_size(tofold_table),
						  utf8_to_unicode((const unsigned char *) s));
}

const char *
utf8_nstrstr(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0, f2 = 0;
	int		needle_char_len;

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
		int		needle_char_len;
		int		haystack_char_len;
		bool	needle_char_is_upper;

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
		{ 0xFF22, 0xFF3A, 1 }, { 0x10400, 0x10427, 1 }, { 0x10C80, 0x10CB2, 1 },
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
	};

	return find_in_range(upper_table, table_size(upper_table),
						utf8_to_unicode((const unsigned char *) s)) != 0;
}
