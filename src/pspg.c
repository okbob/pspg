/*-------------------------------------------------------------------------
 *
 * pspg.c
 *	  a terminal pager designed for usage from psql
 *
 * Portions Copyright (c) 2017-2017 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pspg.c
 *
 *-------------------------------------------------------------------------
 */
#ifdef __FreeBSD__
#define _WITH_GETLINE
#include <ncurses/curses.h>
#else
#include <curses.h>
#endif
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <locale.h>
#include <unistd.h>

#include <signal.h>
#include <sys/ioctl.h>

#include <sys/param.h>

#define STYLE			1
#define PSPG_VERSION "0.3-devel"

//#define COLORIZED_NO_ALTERNATE_SCREEN
//#define DEBUG_COLORS				1

typedef struct LineBuffer
{
	int		first_row;
	int		nrows;
	char   *rows[1000];
	struct LineBuffer *next;
	struct LineBuffer *prev;
} LineBuffer;

/*
 * Available formats of headline chars
 *
 *  L, R   .. outer border
 *  I      .. inner border
 *  d      .. data
 */

/*
 * This structure should be immutable
 */
typedef struct
{
	int		border_top_row;			/* nrow of bootom outer border or -1 */
	int		border_head_row;		/* nrow of head outer (required) */
	int		border_bottom_row;		/* nrow of bottom outer border or -1 */
	int		border_type;			/* detected type of border: 0, 1, 2 */
	char	linestyle;				/* detected linestyle: a, u */
	bool	is_expanded_mode;		/* true when data are in expanded mode */
	int		expanded_info_minx;		/* begin of info in \x mode .. RECORD x */
	char	title[65];				/* detected title (trimmed) or NULL */
	int		title_rows;				/* number of rows used as table title (skipped later) */
	char	filename[65];			/* filename (printed on top bar) */
	LineBuffer rows;				/* list of rows buffers */
	int		maxy;					/* maxy of used pad area with data */
	int		maxx;					/* maxx of used pad area with data */
	int		maxbytes;				/* max length of line in bytes */
	char   *headline;				/* header separator line */
	int		headline_size;			/* size of headerline in bytes */
	char   *headline_transl;		/* translated headline */
	int		headline_char_size;		/* size of headerline in chars */
	int		first_data_row;			/* fist data row line (starts by zero) */
	int		last_data_row;			/* last line of data row */
	int		footer_row;				/* nrow of first footer row or -1 */
	int		alt_footer_row;			/* alternative footer row (used when border = 1) */
	int		footer_char_size;		/* width of footer */
	int		last_row;				/* last not empty row */
	int		fixed_rows;				/* number of fixed rows */
	int		fixed_columns;			/* number of fixed columns */
	int		data_rows;				/* number of data rows */
	int		footer_rows;			/* number of footer rows */
} DataDesc;

/*
 * This structure can be muttable - depends on displayed data
 */
typedef struct
{
	int		fix_rows_rows;			/* current number of fixed rows in window rows */
	int		fix_cols_cols;			/* current number of fixed colums in window rows */
	int		rows_rows;				/* current number of data rows */
	int		footer_rows;			/* current number of footer rows */
	int		maxy;					/* max y size of screen */
	int		maxx;					/* max x size of screen */
	int		main_maxy;				/* max y size of main place (fixed + data + footer rows) */
	int		main_maxx;				/* max x size of main place (should be same like maxx) */
	int		main_start_y;			/* y position of first row of main place */
	int		main_start_x;			/* x position of first row of main place */
	WINDOW *luc;					/* window for left upper corner */
	WINDOW *fix_rows;				/* window for fixed rows */
	WINDOW *fix_cols;				/* window for fixed columns */
	WINDOW *rows;					/* window for data */
	WINDOW *footer;					/* window for footer */
	WINDOW *top_bar;				/* top bar window */
	WINDOW *bottom_bar;				/* bottom bar window */
	int		theme;					/* color theme number */
	char	searchterm[256];		/* currently active search input */
	int		first_rec_title_y;		/* y of first displayed record title in expanded mode */
	int		last_rec_title_y;		/* y of last displayed record title in expanded mode */
} ScrDesc;

static int
min_int(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
}

/*
 * Returns length of utf8 string in chars.
 */
static size_t
utf8len(char *s)
{
	size_t len = 0;

	for (; *s; ++s)
		if ((*s & 0xC0) != 0x80)
			++len;
	return len;
}

/*
 * Returns length of utf8 char in bytes
 */
static int
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

static int
utf_dsplen(const char *s)
{
	return ucs_wcwidth(utf8_to_unicode((const unsigned char *) s));
}


/*
 * Translate from UTF8 to semantic characters.
 */
static bool
translate_headline(DataDesc *desc)
{
	char   *srcptr;
	char   *destptr;
	char   *last_black_char = NULL;
	bool	broken_format = false;
	int		processed_chars = 0;
	bool	is_expanded_info = false;

	srcptr = desc->headline;
	destptr = malloc(desc->headline_size + 1);
	memset(destptr, 0, desc->headline_size + 1);
	desc->headline_transl = destptr;

	desc->linestyle = 'a';
	desc->border_type = 0;

	desc->expanded_info_minx = -1;

	while (*srcptr != '\0' && *srcptr != '\n' && *srcptr != '\r')
	{
		/* only spaces can be after known right border */
		if (last_black_char != NULL && *last_black_char == 'R')
		{
			if (*srcptr != ' ')
			{
				broken_format = true;
				break;
			}
		}

		if (*srcptr != ' ')
			last_black_char = destptr;

		if (desc->is_expanded_mode && *srcptr == '[')
		{
			if (desc->expanded_info_minx != -1)
			{
				broken_format = true;
				break;
			}

			/* entry to expanded info mode */
			is_expanded_info = true;
			desc->expanded_info_minx = processed_chars;

			*destptr++ = 'd';
			srcptr += utf8charlen(*srcptr);
		}
		else if (is_expanded_info)
		{
			if (*srcptr == ']')
			{
				is_expanded_info = false;
			}
			*destptr++ = 'd';
			srcptr += utf8charlen(*srcptr);
		}
		else if (strncmp(srcptr, "\342\224\214", 3) == 0 || /* ┌ */
		         strncmp(srcptr, "\342\225\224", 3) == 0)   /* ╔ */
		{
			/* should be expanded mode */
			if (processed_chars > 0 || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\220", 3) == 0 || /* ┐ */
		         strncmp(srcptr, "\342\225\227", 3) == 0)   /* ╗ */
		{
			if (desc->linestyle != 'u' || desc->border_type != 2 ||
				!desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\254", 3) == 0 || /* ┬╤ */
			     strncmp(srcptr, "\342\225\244", 3) == 0 ||
			     strncmp(srcptr, "\342\225\245", 3) == 0 || /* ╥╦ */
			     strncmp(srcptr, "\342\225\246", 3) == 0)
		{
			if (desc->linestyle != 'u' || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;

			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\234", 3) == 0 || /* ├╟ */
		         strncmp(srcptr, "\342\225\237", 3) == 0 ||
		         strncmp(srcptr, "\342\225\236", 3) == 0 || /* ╞╠ */
		         strncmp(srcptr, "\342\225\240", 3) == 0)
		{
			if (processed_chars > 0)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\244", 3) == 0 || /* ┤╢ */
		         strncmp(srcptr, "\342\225\242", 3) == 0 ||
		         strncmp(srcptr, "\342\225\241", 3) == 0 || /* ╡╣ */
		         strncmp(srcptr, "\342\225\243", 3) == 0)
		{
			if (desc->linestyle != 'u' || desc->border_type != 2)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\274", 3) == 0 || /* ┼╪ */
		         strncmp(srcptr, "\342\225\252", 3) == 0 ||
		         strncmp(srcptr, "\342\225\253", 3) == 0 || /* ╫╬ */
		         strncmp(srcptr, "\342\225\254", 3) == 0)
		{
			if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;
			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\200", 3) == 0 || /* ─ */
		         strncmp(srcptr, "\342\225\220", 3) == 0) /* ═ */
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'u';
			}
			else if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 3;
		}
		else if (*srcptr == '+')
		{
			if (processed_chars == 0)
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
			}
			else
			{
				if (desc->linestyle != 'a')
				{
					broken_format = true;
					break;
				}
				if (desc->border_type == 0)
					desc->border_type = 1;

				*destptr++ = (srcptr[1] == '-') ? 'I' : 'R';
			}
			srcptr += 1;
		}
		else if (*srcptr == '-')
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'a';
			}
			else if (desc->linestyle != 'a')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 1;
		}
		else if (*srcptr == ' ')
		{
			if (desc->border_type != 0)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'I';
			srcptr += 1;
		}
		else
		{
			broken_format = true;
			break;
		}
		processed_chars += 1;
	}

	/* should not be - unclosed header */
	if (is_expanded_info)
		broken_format = true;
	else if (desc->is_expanded_mode && desc->expanded_info_minx == -1)
		broken_format = true;

	/* trim ending spaces */
	if (!broken_format && last_black_char != 0)
	{
		last_black_char[1] = '\0';
		desc->headline_char_size = strlen(desc->headline_transl);

		return true;
	}

	free(desc->headline_transl);
	desc->headline_transl = NULL;

	return false;
}

/*
 * Trim footer rows - We should to trim footer rows and calculate footer_char_size
 */
static void
trim_footer_rows(DataDesc *desc)
{
	if (desc->headline_transl != NULL && desc->footer_row != -1)
	{
		LineBuffer *rows = &desc->rows;
		int			rowidx = 0;
		int			rownum;

		desc->footer_char_size = 0;

		for (rownum = 0, rowidx = 0; rownum < desc->footer_row; rownum++, rowidx++)
		{
			if (rowidx == 1000)
			{
				rows = rows->next;
				rowidx = 0;
			}
		}

		while (rows != NULL && rowidx < rows->nrows)
		{
			char   *line;
			char   *endptr;
			int		len;

			if (rowidx == 1000)
			{
				rows = rows->next;
				rowidx = 0;
				continue;
			}

			line = rows->rows[rowidx++];
			endptr = line + strlen(line) - 1;

			while (endptr > line)
			{
				if (*endptr != ' ')
				{
					endptr[1] = '\0';
					break;
				}
				endptr -= 1;
			}

			len = utf8len(line);
			if (len > desc->footer_char_size)
				desc->footer_char_size = len;
		}
	}
	else
		desc->footer_char_size = desc->maxx;
}

/*
 * Returns true when char is left upper corner
 */
static bool
isTopLeftChar(char *str)
{
	const char *u1 = "\342\224\214";
	const char *u2 = "\342\225\224";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * Returns true when char is top left header char
 */
static bool
isHeadLeftChar(char *str)
{
	const char *u1 = "\342\224\200";
	const char *u2 = "\342\225\220";
	const char *u3 = "\342\225\236";
	const char *u4 = "\342\224\234";
	const char *u5 = "\342\225\240";
	const char *u6 = "\342\225\237";

	if (str[0] == '+' || str[0] == '-')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;
	if (strncmp(str, u3, 3) == 0)
		return true;
	if (strncmp(str, u4, 3) == 0)
		return true;
	if (strncmp(str, u5, 3) == 0)
		return true;
	if (strncmp(str, u6, 3) == 0)
		return true;


	return false;
}

/*
 * Returns true when char is bottom left corner
 */
static bool
isBottomLeftChar(char *str)
{
	const char *u1 = "\342\224\224";
	const char *u2 = "\342\225\232";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * detect different faces of headline in extended mode
 */
static bool
is_expanded_header(char *str, int *ei_minx, int *ei_maxx)
{
	int		pos = 0;

	if (*str == '+')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\214", 3) == 0 || /* ┌ */
	         strncmp(str, "\342\225\224", 3) == 0 || /* ╔ */
	         strncmp(str, "\342\224\234", 3) == 0 || /* ├╟ */
	         strncmp(str, "\342\225\237", 3) == 0 ||
	         strncmp(str, "\342\225\236", 3) == 0 || /* ╞╠ */
	         strncmp(str, "\342\225\240", 3) == 0)
	{
		str += 3;
		pos += 1;
	}

	if (*str == '-')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\200", 3) == 0 || /* ─ */
	         strncmp(str, "\342\225\220", 3) == 0) /* ═ */
	{
		str += 3;
		pos += 1;
	}

	if (strncmp(str, "[ ", 2) != 0)
		return false;

	if (ei_minx != NULL && ei_maxx != NULL)
	{
		pos += 2;
		str += 2;
		*ei_minx = pos - 1;

		while (*str != ']' && *str != '\0')
		{
			pos += 1;
			str += utf8charlen(*str);
		}

		*ei_maxx = pos - 1;
	}

	return true;
}

/*
 * Copy trimmed string
 */
static void
strncpytrim(char *dest, const char *src,
			size_t ndest, size_t nsrc)
{
	const char *endptr;

	endptr = src + nsrc - 1;

	/* skip trailing spaces */
	while (*src == ' ')
	{
		if (nsrc-- <= 0)
			break;
		src++;
	}

	/* skip ending spaces */
	while (*endptr == ' ')
	{
		if (nsrc-- <= 0)
			break;
		endptr--;
	}

	while(nsrc > 0)
	{
		int	clen;

		if (*src == '\0')
			break;

		clen = utf8charlen(*src);
		if (clen <= ndest && clen <= nsrc)
		{
			int		i;

			for (i = 0; i < clen; i++)
			{
				*dest++ = *src++;
				ndest--;
				nsrc--;
			}
		}
		else
			break;
	}

	*dest = '\0';
}

#define MAX_STYLE		14

/*
 * Set color pairs based on style
 */
static void
initialize_color_pairs(int theme)
{
		init_pair(9, -1, -1);							/* footer */
		init_pair(10, COLOR_BLACK, COLOR_WHITE);		/* footer cursor */

		init_pair(11, COLOR_BLACK, COLOR_GREEN);
		init_pair(12, COLOR_BLACK, COLOR_MAGENTA);
		init_pair(13, COLOR_BLACK, COLOR_YELLOW);

	if (theme == 0)
	{
		use_default_colors();

		init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
		init_pair(3, -1, -1);							/* data alphanumeric */
		init_pair(4, -1, -1);							/* fix rows, columns */
		init_pair(5, COLOR_BLACK, COLOR_WHITE);			/* active cursor over fixed cols */
		init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
		init_pair(8, COLOR_BLACK, COLOR_WHITE);			/* expanded header */
		init_pair(9, -1, -1);							/* footer */
		init_pair(10, COLOR_BLACK, COLOR_WHITE);		/* footer cursor */
		init_pair(11, COLOR_BLACK, COLOR_WHITE);							/* cursor over decoration */
		init_pair(12, COLOR_BLACK, COLOR_WHITE);		/* bottom bar colors */
		init_pair(13, COLOR_BLACK, COLOR_WHITE);		/* light bottom bar colors */
	}
	else if (theme == 1)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_YELLOW, COLOR_BLUE);
		init_pair(5, COLOR_YELLOW, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_RED, COLOR_BLUE);
		init_pair(9, COLOR_CYAN, COLOR_BLUE);
		init_pair(10, COLOR_WHITE, COLOR_CYAN);
		init_pair(11, COLOR_WHITE, COLOR_CYAN);
		init_pair(12, COLOR_WHITE, COLOR_CYAN);
		init_pair(13, COLOR_YELLOW, COLOR_CYAN);
	}
	else if (theme == 2)
	{
		assume_default_colors(COLOR_WHITE, COLOR_CYAN);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_WHITE, COLOR_CYAN);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(7, COLOR_YELLOW, COLOR_WHITE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_BLUE, COLOR_CYAN);
		init_pair(10, COLOR_WHITE, COLOR_BLUE);
		init_pair(11, COLOR_WHITE, COLOR_BLUE);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 3)
	{
		assume_default_colors(COLOR_BLACK, COLOR_CYAN);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_BLACK, COLOR_CYAN);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLACK);
		init_pair(6, COLOR_CYAN, COLOR_BLACK);
		init_pair(8, COLOR_WHITE, COLOR_CYAN);
		init_pair(9, COLOR_BLACK, COLOR_CYAN);
		init_pair(10, COLOR_CYAN, COLOR_BLACK);
		init_pair(11, COLOR_CYAN, COLOR_BLACK);
		init_pair(12, COLOR_CYAN, COLOR_BLACK);
		init_pair(13, COLOR_WHITE, COLOR_BLACK);
	}
	else if (theme == 4)
	{
		assume_default_colors(COLOR_BLACK, COLOR_WHITE);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_BLACK, COLOR_WHITE);
		init_pair(4, COLOR_BLACK, COLOR_WHITE);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_BLACK, COLOR_WHITE);
		init_pair(10, COLOR_WHITE, COLOR_BLUE);
		init_pair(11, COLOR_WHITE, COLOR_BLUE);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 5)
	{
		use_default_colors();

		init_pair(2, COLOR_GREEN, COLOR_BLUE);
		init_pair(3, -1, -1);
		init_pair(4, COLOR_CYAN, -1);
		init_pair(5, COLOR_BLACK, COLOR_CYAN);
		init_pair(6, COLOR_BLACK, COLOR_CYAN);
		init_pair(8, COLOR_BLACK, COLOR_BLUE);
		init_pair(9, COLOR_BLACK, COLOR_CYAN);
		init_pair(10, COLOR_BLACK, COLOR_CYAN);
		init_pair(11, -1, COLOR_CYAN);
		init_pair(12, COLOR_BLACK, COLOR_CYAN);
		init_pair(13, COLOR_BLACK, COLOR_CYAN);
	}
	else if (theme == 6)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLACK);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, COLOR_CYAN, COLOR_BLACK);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_CYAN, COLOR_BLACK);
		init_pair(10, COLOR_WHITE, COLOR_BLUE);
		init_pair(11, COLOR_WHITE, COLOR_BLUE);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 7)
	{
		assume_default_colors(COLOR_GREEN, COLOR_BLACK);

		init_pair(2, COLOR_CYAN, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_GREEN, COLOR_BLACK);
		init_pair(5, COLOR_WHITE, COLOR_GREEN);
		init_pair(6, COLOR_WHITE, COLOR_GREEN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_CYAN, COLOR_BLACK);
		init_pair(10, COLOR_WHITE, COLOR_GREEN);
		init_pair(11, COLOR_WHITE, COLOR_GREEN);
		init_pair(12, COLOR_WHITE, COLOR_GREEN);
		init_pair(13, COLOR_WHITE, COLOR_GREEN);
	}
	else if (theme == 8)
	{
		assume_default_colors(COLOR_CYAN, COLOR_BLUE);

		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_WHITE, COLOR_BLUE);
		init_pair(5, COLOR_WHITE, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_WHITE, COLOR_BLUE);
		init_pair(10, COLOR_WHITE, COLOR_CYAN);
		init_pair(11, COLOR_BLUE, COLOR_CYAN);
		init_pair(12, COLOR_WHITE, COLOR_CYAN);
		init_pair(13, COLOR_WHITE, COLOR_CYAN);
	}
	else if (theme == 9)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_CYAN, COLOR_BLUE);
		init_pair(5, COLOR_WHITE, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_WHITE, COLOR_BLUE);
		init_pair(10, COLOR_WHITE, COLOR_CYAN);
		init_pair(11, COLOR_WHITE, COLOR_CYAN);
		init_pair(12, COLOR_WHITE, COLOR_CYAN);
		init_pair(13, COLOR_WHITE, COLOR_CYAN);
	}
	else if (theme == 10)
	{
		assume_default_colors(COLOR_BLUE, COLOR_CYAN);

		init_pair(2, COLOR_BLUE, COLOR_CYAN);
		init_pair(3, COLOR_BLUE, COLOR_CYAN);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_BLUE, COLOR_CYAN);
		init_pair(10, COLOR_WHITE, COLOR_BLUE);
		init_pair(11, COLOR_CYAN, COLOR_BLUE);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 11)
	{
		assume_default_colors(COLOR_CYAN, COLOR_BLACK);

		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_CYAN, COLOR_BLACK);
		init_pair(4, COLOR_CYAN, COLOR_BLACK);
		init_pair(5, COLOR_WHITE, COLOR_MAGENTA);
		init_pair(6, COLOR_WHITE, COLOR_MAGENTA);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_WHITE, COLOR_BLACK);
		init_pair(10, COLOR_WHITE, COLOR_MAGENTA);
		init_pair(11, COLOR_WHITE, COLOR_MAGENTA);
		init_pair(12, COLOR_WHITE, COLOR_MAGENTA);
		init_pair(13, COLOR_WHITE, COLOR_MAGENTA);
	}
	else if (theme == 12)
	{
		assume_default_colors(COLOR_BLUE, COLOR_CYAN);

		init_pair(2, COLOR_BLUE, COLOR_CYAN);
		init_pair(3, COLOR_WHITE, COLOR_CYAN);
		init_pair(4, COLOR_BLUE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_BLUE, COLOR_CYAN);
		init_pair(10, COLOR_WHITE, COLOR_BLUE);
		init_pair(11, COLOR_CYAN, COLOR_BLUE);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 13)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_WHITE, COLOR_BLUE);
		init_pair(5, COLOR_BLACK, COLOR_CYAN);
		init_pair(6, COLOR_BLACK, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_WHITE, COLOR_BLUE);
		init_pair(10, COLOR_BLACK, COLOR_CYAN);
		init_pair(11, COLOR_WHITE, COLOR_CYAN);
		init_pair(12, COLOR_WHITE, COLOR_BLACK);
		init_pair(13, COLOR_WHITE, COLOR_BLACK);
	}
	else if (theme == MAX_STYLE)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_MAGENTA, COLOR_BLUE);
		init_pair(5, COLOR_BLACK, COLOR_CYAN);
		init_pair(6, COLOR_BLACK, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
		init_pair(9, COLOR_WHITE, COLOR_BLUE);
		init_pair(10, COLOR_BLACK, COLOR_CYAN);
		init_pair(11, COLOR_WHITE, COLOR_CYAN);
		init_pair(12, COLOR_WHITE, COLOR_BLACK);
		init_pair(13, COLOR_WHITE, COLOR_BLACK);
	}

}

/*
 * Read data from file and fill DataDesc.
 */
static int
readfile(FILE *fp, DataDesc *desc)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	int			trimmed_nrows = 0;
	bool		use_stdin = false;
	LineBuffer *rows;

	/* safe reset */
	desc->filename[0] = '\0';

	if (fp == NULL)
	{
		use_stdin = true;
		fp = stdin;
	}
	else
	{
		int		fno;
		char	proclnk[MAXPATHLEN + 1];
		char	path[MAXPATHLEN + 1];
		ssize_t r;

		fno = fileno(fp);

		sprintf(proclnk, "/proc/self/fd/%d", fno);

		r = readlink(proclnk, path, MAXPATHLEN);
		if (r > 0)
		{
			char	   *name;

			path[r] = '\0';
			name = basename(path);
			strncpy(desc->filename, name, 64);
			desc->filename[64] = '\0';
		}
	}

	desc->title[0] = '\0';
	desc->title_rows = 0;
	desc->border_top_row = -1;
	desc->border_head_row = -1;
	desc->border_bottom_row = -1;
	desc->first_data_row = -1;
	desc->last_data_row = -1;
	desc->is_expanded_mode = false;
	desc->headline_transl = NULL;
	desc->footer_row = -1;
	desc->alt_footer_row = -1;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	rows = &desc->rows;
	desc->rows.prev = NULL;

	errno = 0;

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		clen = utf8len(line);

		if (rows->nrows == 1000)
		{
			LineBuffer *newrows = malloc(sizeof(LineBuffer));
			memset(newrows, 0, sizeof(LineBuffer));
			rows->next = newrows;
			newrows->prev = rows;
			rows = newrows;
		}

		rows->rows[rows->nrows++] = line;


		/* save possible table name */
		if (nrows == 0 && !isTopLeftChar(line))
		{
			strncpytrim(desc->title, line, 63, len);
			desc->title_rows = 1;
		}

		if (desc->border_head_row == -1 && desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
			desc->is_expanded_mode = is_expanded_header(line, NULL, NULL);
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;

			if (!desc->is_expanded_mode)
				desc->is_expanded_mode = is_expanded_header(line, NULL, NULL);

			/* title surelly doesn't it there */
			if ((!desc->is_expanded_mode && nrows == 1) ||
			    (desc->is_expanded_mode && nrows == 0))
			{
				desc->title[0] = '\0';
				desc->title_rows = 0;
			}
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row == -1 && isBottomLeftChar(line))
		{
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row != -1 && desc->footer_row == -1)
		{
			desc->footer_row = nrows;
		}
		else if (desc->is_expanded_mode && isBottomLeftChar(line))
		{
			/* Outer border is repeated in expanded mode, use last detected row */
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}

		if (!desc->is_expanded_mode && desc->border_head_row != -1 && desc->border_head_row < nrows
			 && desc->alt_footer_row == -1)
		{
			if (*line != '\0' && *line != ' ')
				desc->alt_footer_row = nrows;
		}

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > desc->maxx + 1)
			desc->maxx = clen - 1;

		if ((int) clen > 1 || (clen == 1 && *line != '\n'))
			desc->last_row = nrows;

		if (*line != '\0')
			trimmed_nrows = nrows;

		nrows += 1;


		line = NULL;
	}

	if (errno != 0)
	{
		fprintf(stderr, "cannot to read file: %s\n", strerror(errno));
		exit(1);
	}

	if (!use_stdin)
		fclose(fp);

	desc->maxy = trimmed_nrows ;

	desc->headline_char_size = 0;

	if (desc->border_head_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_head_row];
		desc->headline_size = strlen(desc->headline);

		if (desc->last_data_row == -1)
			desc->last_data_row = desc->last_row - 1;
	}
	else if (desc->is_expanded_mode && desc->border_top_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_top_row];
		desc->headline_size = strlen(desc->headline);
	}
	else
	{
		desc->headline = NULL;
		desc->headline_size = 0;
		desc->headline_char_size = 0;

		/* there are not a data set */
		desc->last_row = trimmed_nrows;
		desc->last_data_row = trimmed_nrows;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	if (freopen("/dev/tty", "rw", stdin) == NULL)
	{
		fprintf(stderr, "cannot to reopen stdin: %s\n", strerror(errno));
		exit(1);
	}

	return 0;
}

static void
window_fill(WINDOW *win,
			int srcy, int srcx,				/* offset to displayed data */
			int cursor_row,					/* row of row cursor */
			DataDesc *desc,
			attr_t data_attr,				/* colors for data (alphanums) */
			attr_t line_attr,				/* colors for borders */
			attr_t expi_attr,				/* colors for expanded headers */
			attr_t cursor_data_attr,		/* colors for cursor on data positions */
			attr_t cursor_line_attr,		/* colors for cursor on border position */
			attr_t cursor_expi_attr,		/* colors for cursor on expanded headers */
			bool is_footer,					/* true if window is footer */
			ScrDesc *scrdesc)				/* used for searching records limits in expanded mode */
{
	int			maxy, maxx;
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;
	char		*free_row;

	/* when we want to detect expanded records titles */
	if (scrdesc != NULL && desc->is_expanded_mode)
	{
		scrdesc->first_rec_title_y = -1;
		scrdesc->last_rec_title_y = -1;
	}

	/* fast leaving */
	if (win == NULL)
		return;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	getmaxyx(win, maxy, maxx);

	while (row < maxy )
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;
		bool		is_cursor_row;

		is_cursor_row = row == cursor_row;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
			rowstr = lnb->rows[lnb_row++];
		else
			rowstr = NULL;

		if (!is_footer)
		{
			if (desc->border_type == 2)
				active_attr = is_cursor_row ? cursor_line_attr : line_attr;
			else
				active_attr = is_cursor_row ? cursor_data_attr : data_attr;
		}
		else
			active_attr = is_cursor_row ? cursor_data_attr : data_attr;

		wattron(win, active_attr);

		wmove(win, row++, 0);

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;
			int		left_spaces;							/* aux left spaces */

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
				if (is_expand_head && scrdesc != NULL)
				{
					if (scrdesc->first_rec_title_y == -1)
						scrdesc->first_rec_title_y = row - 1;
					else
						scrdesc->last_rec_title_y = row - 1;
				}
			}
			else
			{
				if (!is_footer)
				{
					fix_line_attr_style = effective_row == desc->border_top_row ||
												effective_row == desc->border_head_row ||
												effective_row == desc->border_bottom_row;
				}
				else
					fix_line_attr_style = false;

				is_expand_head = false;
			}

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;
			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= utf_dsplen(rowstr);
					rowstr += utf8charlen(*rowstr);
					if (i < 0)
						left_spaces = -i;
				}
				else
					break;
			}

			/* Fix too hungry cutting when some multichar char is removed */
			if (left_spaces > 0)
			{
				char *p;

				free_row = malloc(left_spaces + strlen(rowstr) + 1);
				p = free_row;
				while (left_spaces-- > 0)
				{
					*p++ = ' ';
				}
				strcpy(p, rowstr);
				rowstr = free_row;
			}
			else
				free_row = NULL;

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0')
			{
				i = 0;
				while (i < maxx)
				{
					if (is_expand_head)
					{
						int		pos = srcx + i;
						int		new_attr;

						if (is_cursor_row)
							new_attr = pos >= ei_min && pos <= ei_max ? cursor_expi_attr : cursor_line_attr;
						else
							new_attr = pos >= ei_min && pos <= ei_max ? expi_attr : line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
							}

							/* disable current style */
							wattroff(win, active_attr);

							/* active new style */
							active_attr = new_attr;
							wattron(win, active_attr);
						}
					}
					else if (!fix_line_attr_style)
					{
						int htrpos = srcx + i;

						if (htrpos < desc->headline_char_size)
						{
							int		new_attr;

							if (!is_footer)
							{
								if (is_cursor_row)
									new_attr = desc->headline_transl[htrpos] == 'd' ? cursor_data_attr : cursor_line_attr;
								else
									new_attr = desc->headline_transl[htrpos] == 'd' ? data_attr : line_attr;
							}
							else
								new_attr = is_cursor_row ? cursor_data_attr : data_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									waddnstr(win, rowstr, bytes);
									rowstr += bytes;
									bytes = 0;
								}

								/* disable current style */
								wattroff(win, active_attr);

								/* active new style */
								active_attr = new_attr;
								wattron(win, active_attr);
							}
						}
					}
					else
					{
						if (!is_footer)
						{
							int		new_attr;

							if (is_cursor_row)
								new_attr = cursor_line_attr;
							else
								new_attr = line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									waddnstr(win, rowstr, bytes);
									rowstr += bytes;
									bytes = 0;
								}

								/* disable current style */
								wattroff(win, active_attr);

								/* active new style */
								active_attr = new_attr;
								wattron(win, active_attr);
							}
						}
					}

					if (*ptr != '\0')
					{
						int len  = utf8charlen(*ptr);
						i += utf_dsplen(ptr);
						ptr += len;
						bytes += len;
					}
					else
						break;
				}
			}
			else if (is_cursor_row)
				/* in this case i is not valid, but it is necessary for cursor line printing */
				i = 1;

			if (bytes > 0)
				waddnstr(win, rowstr, bytes);

			/* clean other chars on line */
			if (i < maxx)
				wclrtoeol(win);

			/* draw cursor line to screan end of line */
			if (is_cursor_row && i < maxx)
				mvwchgat(win, row - 1, i + 1, -1, 0, PAIR_NUMBER(cursor_data_attr), 0);

			if (free_row != NULL)
			{
				free(free_row);
				free_row = NULL;
			}
		}
		else
		{
			wclrtobot(win);
			break;
		}

		wattroff(win, active_attr);
	}
}

#ifdef COLORIZED_NO_ALTERNATE_SCREEN

static void
ansi_colors(int pairno, short int *fc, short int *bc)
{
	pair_content(pairno, fc, bc);
	*fc = *fc != -1 ? *fc + 30 : 39;
	*bc = *bc != -1 ? *bc + 40 : 49;
}

#endif

static char *
ansi_attr(attr_t attr)
{

#ifndef COLORIZED_NO_ALTERNATE_SCREEN

	return "";

#else

	static char result[20];
	int		pairno;
	short int fc, bc;

	pairno = PAIR_NUMBER(attr);
	ansi_colors(pairno, &fc, &bc);

	if ((attr & A_BOLD) != 0)
	{
		snprintf(result, 20, "\e[1;%d;%dm", fc, bc);
	}
	else
		snprintf(result, 20, "\e[0;%d;%dm", fc, bc);

	return result;

#endif

}

/*
 * Print data to primary screen without ncurses
 */
static void
draw_rectange(int offsety, int offsetx,			/* y, x offset on screen */
			int maxy, int maxx,				/* size of visible rectangle */
			int srcy, int srcx,				/* offset to displayed data */
			DataDesc *desc,
			attr_t data_attr,				/* colors for data (alphanums) */
			attr_t line_attr,				/* colors for borders */
			attr_t expi_attr,				/* colors for expanded headers */
			bool clreoln)					/* force clear to eoln */
{
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	if (offsety)
		printf("\e[%dB", offsety);

	while (row < maxy )
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
			rowstr = lnb->rows[lnb_row++];
		else
			rowstr = NULL;

		active_attr = line_attr;
		printf("%s", ansi_attr(active_attr));

		row += 1;

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;
			int		left_spaces;
			char   *free_row;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
			}
			else
			{
				fix_line_attr_style = effective_row == desc->border_top_row ||
											effective_row == desc->border_head_row ||
											effective_row >= desc->border_bottom_row;
				is_expand_head = false;
			}

			if (offsetx != 0)
				printf("\e[%dC", offsetx);

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;
			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= utf_dsplen(rowstr);
					rowstr += utf8charlen(*rowstr);
					if (i < 0)
						left_spaces = -i;
				}
				else
					break;
			}

			/* Fix too hungry cutting when some multichar char is removed */
			if (left_spaces > 0)
			{
				char *p;

				free_row = malloc(left_spaces + strlen(rowstr) + 1);
				p = free_row;
				while (left_spaces-- > 0)
				{
					*p++ = ' ';
				}
				strcpy(p, rowstr);
				rowstr = free_row;
			}
			else
				free_row = NULL;

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0' && *ptr != '\n')
			{
				i = 0;
				while (i < maxx)
				{
					if (is_expand_head)
					{
						int		pos = srcx + i;
						int		new_attr;

						new_attr = pos >= ei_min && pos <= ei_max ? expi_attr : line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								printf("%.*s", bytes, rowstr);
								rowstr += bytes;
								bytes = 0;
							}

							/* active new style */
							active_attr = new_attr;
							printf("%s", ansi_attr(active_attr));
						}
					}
					else if (!fix_line_attr_style && desc->headline_transl != NULL)
					{
						int htrpos = srcx + i;

						if (htrpos < desc->headline_char_size)
						{
							int		new_attr;

							new_attr = desc->headline_transl[htrpos] == 'd' ? data_attr : line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									//waddnstr(win, rowstr, bytes);
									printf("%.*s", bytes, rowstr);
									rowstr += bytes;
									bytes = 0;
								}

								/* active new style */
								active_attr = new_attr;
								printf("%s", ansi_attr(active_attr));
							}
						}
					}

					if (*ptr != '\0' && *ptr != '\n')
					{
						int len  = utf8charlen(*ptr);
						i += utf_dsplen(ptr);
						ptr += len;
						bytes += len;
					}
					else
						break;
				}
			}

			if (bytes > 0)
			{
				printf("%.*s", bytes, rowstr);
				if (clreoln)
					printf("\e[K");
				printf("\n");
			}

			if (free_row != NULL)
			{
				free(free_row);
				free_row = NULL;
			}
		}
		else
			break;
	}
}

static void
draw_data(ScrDesc *scrdesc, DataDesc *desc,
		  int first_data_row, int first_row, int cursor_col,
		  int footer_cursor_col, int fix_rows_offset)
{
	struct winsize size;
	int		i;

	if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
	{

		for (i = 0; i < min_int(size.ws_row - 2, desc->last_row); i++)
			printf("\eD");

		/* Go wit cursor to up */
		printf("\e[%dA", min_int(size.ws_row - 2, desc->last_row));

		/* Save cursor */
		printf("\e[s");

		if (scrdesc->fix_cols_cols > 0)
		{
			draw_rectange(scrdesc->fix_rows_rows, 0,
						  scrdesc->rows_rows, scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset , 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}
		if (scrdesc->fix_rows_rows > 0)
		{
			printf("\e[u\e[s");

			draw_rectange(0, scrdesc->fix_cols_cols,
						  scrdesc->fix_rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
		{
			printf("\e[u\e[s");

			draw_rectange(0, 0,
						  scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}

		if (scrdesc->rows_rows > 0)
		{
			printf("\e[u\e[s");

			draw_rectange(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  scrdesc->rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset , scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  scrdesc->theme == 2 ? 0 | A_BOLD : 0,
						  scrdesc->theme == 2 && (desc->headline_transl == NULL) ? A_BOLD : 0,
						  COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (scrdesc->footer != NULL)
		{
			printf("\e[u\e[s");

			draw_rectange(scrdesc->fix_rows_rows + scrdesc->rows_rows, 0,
						  scrdesc->footer_rows, scrdesc->maxx,
						  first_data_row + first_row + scrdesc->rows_rows - fix_rows_offset, footer_cursor_col,
						  desc,
						  COLOR_PAIR(9), 0, 0, true);
		}

		/* reset */
		printf("\e[0m\r");

	}
}


/*
 * Prepare dimensions of windows layout
 */
static void
create_layout_dimensions(ScrDesc *scrdesc, DataDesc *desc,
				   int fixCols, int fixRows,
				   int maxy, int maxx)
{
	scrdesc->maxy = maxy;
	scrdesc->maxx = maxx;

	scrdesc->fix_cols_cols = 0;

	if (fixCols == -1)
		fixCols = 1;

	/* search end of fixCol'th column */
	if (desc->headline_transl != NULL && fixCols > 0)
	{
		char   *c = desc->headline_transl;

		while (*c != 0)
		{
			if (*c == 'I' && --fixCols == 0)
			{
				scrdesc->fix_cols_cols = c - desc->headline_transl + 1;
				break;
			}
			c += 1;
		}
	}

	scrdesc->fix_rows_rows = 0;
	scrdesc->footer_rows = 0;

	if (fixRows != -1)
	{
		scrdesc->fix_rows_rows = fixRows;
	}
	else if (!desc->is_expanded_mode && desc->border_head_row != -1)
	{
		scrdesc->fix_rows_rows = desc->border_head_row + 1 - desc->title_rows;
	}

	/* disable fixed parts when is not possible draw in screen */
	if (scrdesc->fix_cols_cols > maxx)
		scrdesc->fix_cols_cols = 0;

	if (scrdesc->fix_rows_rows > maxy)
		scrdesc->fix_rows_rows = 0;

	if (scrdesc->fix_rows_rows == 0 && !desc->is_expanded_mode)
	{
		scrdesc->fix_rows_rows = 0;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	desc->fixed_rows = scrdesc->fix_rows_rows;
}

static void
create_layout(ScrDesc *scrdesc, DataDesc *desc, int first_data_row, int first_row)
{
	if (scrdesc->luc != NULL)
	{
		delwin(scrdesc->luc);
		scrdesc->luc = NULL;
	}
	if (scrdesc->fix_rows != NULL)
	{
		delwin(scrdesc->fix_rows);
		scrdesc->fix_rows = NULL;
	}
	if (scrdesc->fix_cols != NULL)
	{
		delwin(scrdesc->fix_cols);
		scrdesc->fix_cols = NULL;
	}
	if (scrdesc->rows != NULL)
	{
		delwin(scrdesc->rows);
		scrdesc->rows = NULL;
	}

	if (scrdesc->footer != NULL)
	{
		delwin(scrdesc->footer);
		scrdesc->footer = NULL;
	}

	if (desc->headline_transl != NULL && desc->footer_row > 0)
	{
		int		rows_rows = desc->footer_row - first_row - first_data_row;
		int		data_rows;

		/* desc->footer_row == desc->first_data_row when result is empty */
		if (rows_rows > 0 || desc->footer_row == desc->first_data_row)
		{
			data_rows = scrdesc->main_maxy - desc->fixed_rows;
			scrdesc->rows_rows = min_int(rows_rows, data_rows);
		}
		else
		{
			int		new_fix_rows_rows = scrdesc->fix_rows_rows + rows_rows - 1;

			scrdesc->fix_rows_rows = new_fix_rows_rows > 0 ? new_fix_rows_rows : 0;
			scrdesc->rows_rows = new_fix_rows_rows >= 0 ? 1 : 0;
			data_rows = scrdesc->main_maxy - scrdesc->fix_rows_rows;
		}

		scrdesc->footer_rows = min_int(data_rows - scrdesc->rows_rows,
									   desc->last_row - desc->footer_row + 1);

		if (scrdesc->footer_rows > 0)
		{
			scrdesc->footer = newwin(scrdesc->footer_rows,
									scrdesc->maxx, scrdesc->main_start_y + scrdesc->fix_rows_rows + scrdesc->rows_rows, 0);
		}
	}
	else if (desc->headline_transl != NULL)
	{
		scrdesc->rows_rows = min_int(scrdesc->main_maxy - scrdesc->fix_rows_rows,
									 desc->last_row - desc->first_data_row);
	}
	else
	{
		scrdesc->rows_rows = 0;
		scrdesc->fix_rows_rows = 0;
		scrdesc->footer_rows = min_int(scrdesc->main_maxy, desc->last_row + 1);
		scrdesc->footer = newwin(scrdesc->footer_rows, scrdesc->main_maxx, scrdesc->main_start_y, 0);
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		scrdesc->fix_rows = newwin(scrdesc->fix_rows_rows,
								   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
								   1, scrdesc->fix_cols_cols);
	}

	if (scrdesc->fix_cols_cols > 0 && scrdesc->rows_rows > 0)
	{
		scrdesc->fix_cols = newwin(scrdesc->rows_rows, scrdesc->fix_cols_cols, scrdesc->fix_rows_rows + 1, 0);
	}

	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		scrdesc->luc = newwin(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols, scrdesc->main_start_y, 0);
	}

	if (scrdesc->rows_rows > 0)
	{
		scrdesc->rows = newwin(scrdesc->rows_rows,
							   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
							   scrdesc->fix_rows_rows + scrdesc->main_start_y, scrdesc->fix_cols_cols);
	}

#ifdef DEBUG_COLORS

	if (scrdesc->rows != NULL)
		wbkgd(scrdesc->rows, COLOR_PAIR(2));
	if (scrdesc->luc != NULL)
		wbkgd(scrdesc->luc, COLOR_PAIR(10));
	if (scrdesc->fix_cols != NULL)
		wbkgd(scrdesc->fix_cols, COLOR_PAIR(11));
	if (scrdesc->fix_rows != NULL)
		wbkgd(scrdesc->fix_rows, COLOR_PAIR(12));
	if (scrdesc->footer != NULL)
	{
		wbkgd(scrdesc->footer, COLOR_PAIR(13));
		wrefresh(scrdesc->footer);
	}

#endif
}

static int
if_in_int(int v, const int *s, int v1, int v2)
{
	while(*s != -1)
	{
		if (v == *s)
			return v1;
		s += 1;
	}
	return v2;
}

static int
if_notin_int(int v, const int *s, int v1, int v2)
{
	while(*s != -1)
	{
		if (v == *s)
			return v2;
		s += 1;
	}
	return v1;
}

/*
 * Rewresh aux windows like top bar or bottom bar.
 */
static void
refresh_aux_windows(ScrDesc *scrdesc, DataDesc *desc)
{
	int		maxy, maxx;

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	if (scrdesc->top_bar != NULL)
		delwin(scrdesc->top_bar);

	scrdesc->top_bar = newwin(1, 0, 0, 0);
	wbkgd(scrdesc->top_bar, COLOR_PAIR(2));
	wrefresh(scrdesc->top_bar);

	if (scrdesc->bottom_bar != NULL)
		delwin(scrdesc->bottom_bar);

	scrdesc->bottom_bar = newwin(1, 0, maxy - 1, 0);

	wattron(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(13));
	mvwaddstr(scrdesc->bottom_bar, 0, 1, "Q");
	wattroff(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(13));
	wattron(scrdesc->bottom_bar, COLOR_PAIR(12) | if_notin_int(scrdesc->theme, (int[]) {13, 14, -1}, A_BOLD, 0));
	mvwprintw(scrdesc->bottom_bar, 0, 2, "%-4s", "uit");
	wattroff(scrdesc->bottom_bar, COLOR_PAIR(12) | if_notin_int(scrdesc->theme, (int[]) {13, 14, -1}, A_BOLD, 0));
	wrefresh(scrdesc->bottom_bar);

	if (desc->headline_transl != NULL)
	{
		wattron(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(13));
		mvwaddstr(scrdesc->bottom_bar, 0, 7, "0..4");
		wattroff(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(13));
		wattron(scrdesc->bottom_bar, COLOR_PAIR(12) | if_notin_int(scrdesc->theme, (int[]) {13, 14, -1}, A_BOLD, 0));
		mvwprintw(scrdesc->bottom_bar, 0, 11, "%s", " Col.Freeze ");
		wattroff(scrdesc->bottom_bar, COLOR_PAIR(12) | if_notin_int(scrdesc->theme, (int[]) {13, 14, -1}, A_BOLD, 0));
		wrefresh(scrdesc->bottom_bar);
	}

	scrdesc->main_maxy = maxy;
	scrdesc->main_maxx = maxx;
	scrdesc->main_start_y = 0;
	scrdesc->main_start_x = 0;

	if (scrdesc->top_bar != NULL)
	{
		scrdesc->main_maxy -= 1;
		scrdesc->main_start_y = 1;
	}

	if (scrdesc->bottom_bar != NULL)
	{
		scrdesc->main_maxy -= 1;
	}
}

static int
number_width(int num)
{
	if (num < 10)
		return 1;
	if (num < 100)
		return 2;
	if (num < 1000)
		return 3;
	if (num < 10000)
		return 4;
	if (num < 100000)
		return 5;
	if (num < 1000000)
		return 6;
	if (num < 10000000)
		return 7;

	return 8;
}

/*
 * returns true when cursor is on footer window
 */
static bool
is_footer_cursor(int cursor_row, ScrDesc *scrdesc, DataDesc *desc)
{
	if (scrdesc->footer == NULL)
		return false;
	else if (scrdesc->rows_rows == 0)
		return true;

	return cursor_row + scrdesc->fix_rows_rows + desc->title_rows + 1 > desc->footer_row;
}

static void
print_top_window_context(ScrDesc *scrdesc, DataDesc *desc,
						 int cursor_row, int cursor_col, int first_row, int fix_rows_offset)
{
	int		maxy, maxx;
	int		smaxy, smaxx;
	char	buffer[200];

	getmaxyx(scrdesc->top_bar, maxy, maxx);
	getmaxyx(stdscr, smaxy, smaxx);

	(void) maxy;

	if (scrdesc->theme == 2)
		wattron(scrdesc->top_bar, A_BOLD | COLOR_PAIR(7));

	if (desc->title[0] != '\0')
		mvwprintw(scrdesc->top_bar, 0, 0, "%s", desc->title);
	else if (desc->filename[0] != '\0')
		mvwprintw(scrdesc->top_bar, 0, 0, "%s", desc->filename);

	if (scrdesc->theme == 2)
		wattroff(scrdesc->top_bar, A_BOLD | COLOR_PAIR(7));

	if (desc->headline_transl)
	{
		snprintf(buffer, 199, "FC:%*d C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
							number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
							number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
							number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
							number_width(desc->headline_char_size), desc->headline_char_size,
							number_width(desc->maxy - desc->fixed_rows), first_row + 1 - fix_rows_offset,
							number_width(smaxy), cursor_row - first_row + fix_rows_offset,
							number_width(desc->maxy - desc->fixed_rows - desc->title_rows), cursor_row + 1,
							number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy - desc->fixed_rows - desc->title_rows,
							(cursor_row + 1) / ((double) (desc->maxy - desc->fixed_rows - desc->title_rows)) * 100.0);
	}
	else
	{
		snprintf(buffer, 199, "C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
							number_width(desc->maxx), cursor_col + scrdesc->fix_cols_cols + 1,
							number_width(desc->maxx), min_int(smaxx + cursor_col, desc->maxx),
							number_width(desc->maxx), desc->maxx,
							number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + 1,
							number_width(smaxy), cursor_row - first_row,
							number_width(desc->last_row), cursor_row + 1,
							number_width(desc->last_row), desc->last_row + 1,
							((cursor_row + 1) / ((double) (desc->last_row + 1))) * 100.0);
	}

	mvwprintw(scrdesc->top_bar, 0, maxx - strlen(buffer), "%s", buffer);
	wrefresh(scrdesc->top_bar);
}


/*
 * It is used for result of action info
 */
static int
show_info_wait(ScrDesc *scrdesc, char *fmt, char *par)
{
	wattron(scrdesc->bottom_bar, COLOR_PAIR(13) | A_BOLD);

	if (par != NULL)
		mvwprintw(scrdesc->bottom_bar, 0, 0, fmt, par);
	else
		mvwprintw(scrdesc->bottom_bar, 0, 0, "%s", fmt);

	wclrtoeol(scrdesc->bottom_bar);
	wattroff(scrdesc->bottom_bar, COLOR_PAIR(13) | A_BOLD);
	wrefresh(scrdesc->bottom_bar);

	refresh();

	return getch();
}

static void
get_string(ScrDesc *scrdesc, char *prompt, char *buffer, int maxsize)
{
	mvwprintw(scrdesc->bottom_bar, 0, 0, "%s", prompt);
	wclrtoeol(scrdesc->bottom_bar);
	curs_set(1);
	echo();
	wgetnstr(scrdesc->bottom_bar, buffer, maxsize);
	curs_set(0);
	noecho();
}

int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		c;
	int		c2 = 0;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		footer_cursor_col = 0;
	int		first_row = 0;
	int		prev_first_row;
	int		first_data_row;
	int		i;
	int		style = STYLE;
	DataDesc		desc;
	ScrDesc			scrdesc;
	int		_columns = -1;			/* default will be 1 if screen width will be enough */
	int		fixedRows = -1;			/* detect automaticly (not yet implemented option) */
	FILE   *fp = NULL;
	bool	detected_format = false;
	bool	no_alternate_screen = false;
	bool	no_sound = false;
	int		fix_rows_offset = 0;

	int		opt;
	int		option_index = 0;
	bool	use_mouse = true;
	mmask_t		prev_mousemask = 0;

	static struct option long_options[] =
	{
		/* These options set a flag. */
		{"help", no_argument, 0, 1},
		{"no-mouse", no_argument, 0, 2},
		{"no-sound", no_argument, 0, 3},
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "bs:c:f:XV",
							  long_options, &option_index)) != -1)
	{
		int		n;

		switch (opt)
		{
			case 1:
				fprintf(stderr, "pspg is a Unix pager optimized for table browsing.\n\n");
				fprintf(stderr, "Usage:\n");
				fprintf(stderr, "  %s [OPTION]\n\n", argv[0]);
				fprintf(stderr, "Options:\n");
				fprintf(stderr, "  -b             black-white style\n");
				fprintf(stderr, "  -s N           set color style number (1..%d)\n", MAX_STYLE);
				fprintf(stderr, "  -c N           fix N columns (1..4)\n");
				fprintf(stderr, "  -f file        open file\n");
				fprintf(stderr, "  -X             don't use alternate screen\n");
				fprintf(stderr, "  --help         show this help\n\n");
				fprintf(stderr, "  --no-mouse     don't use own mouse handling\n");
				fprintf(stderr, "  --no-sound     don't use beep when scroll is not possible\n");
				fprintf(stderr, "  -V, --version  show version\n\n");
				fprintf(stderr, "pspg shares lot of key commands with less pager or vi editor.\n");
				exit(0);

			case 2:
				use_mouse = false;
				break;
			case 3:
				no_sound = true;
				break;
			case 'V':
				fprintf(stdout, "pspg-%s\n", PSPG_VERSION);
				exit(0);
			case 'X':
				no_alternate_screen = true;
				break;
			case 'b':
				style = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > MAX_STYLE)
				{
					fprintf(stderr, "Only color schemas 0 .. %d are supported.\n", MAX_STYLE);
					exit(EXIT_FAILURE);
				}
				style = n;
				break;
			case 'c':
				n = atoi(optarg);
				if (n < 0 || n > 4)
				{
					fprintf(stderr, "fixed columns should be between 0 and 4.\n");
					exit(EXIT_FAILURE);
				}
				_columns = n;
				break;
			case 'f':
				fp = fopen(optarg, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s\n", optarg);
					exit(1);
				}
				break;
			default:
				fprintf(stderr, "Try %s --help\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	setlocale(LC_ALL, "");

	readfile(fp, &desc);

	initscr();

	if(!has_colors())
	{
		endwin();
		fprintf(stderr, "Your terminal does not support color\n");
		exit(1);
	}

	start_color();
	initialize_color_pairs(style);

	clear();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	set_escdelay(25);

	if (use_mouse)
	{

#if NCURSES_MOUSE_VERSION > 1

		mousemask(BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED | BUTTON_ALT, NULL);

#else

		mousemask(BUTTON1_CLICKED, NULL);

#endif

		mouseinterval(10);

	}

	if (desc.headline != NULL)
		detected_format = translate_headline(&desc);

	if (desc.headline_transl != NULL && !desc.is_expanded_mode)
		desc.first_data_row = desc.border_head_row + 1;
	else if (desc.title_rows > 0)
		desc.first_data_row = desc.title_rows;
	else
		desc.first_data_row = 0;

	first_data_row = desc.first_data_row;

	trim_footer_rows(&desc);

	memset(&scrdesc, 0, sizeof(ScrDesc));
	scrdesc.theme = style;
	refresh_aux_windows(&scrdesc, &desc);
	getmaxyx(stdscr, maxy, maxx);

	/* some corrections */
	if (detected_format)
	{
		if (desc.is_expanded_mode)
		{
			if (strchr(desc.headline_transl,'I') == NULL)
			{
				char *str = desc.rows.rows[desc.title_rows + 1];
				int pos = 0;

				/* fallback point, didn't find separator already */
				while (pos < 40)
				{
					if ((desc.linestyle == 'a' && *str == '|' && pos > 1) ||
					    (desc.linestyle == 'u' && pos > 1 &&
					    (strncmp(str, /* │ */ "\342\224\202", 3) == 0 ||
					     strncmp(str, /* ║ */ "\342\225\221", 3) == 0)))
					{
						desc.headline_transl[pos] = 'I';
						break;
					}
					pos += 1;
					str += utf8charlen(*str);
				}
			}
		}
		else
		{
			if (desc.border_type != 2)
			{
				if (desc.border_bottom_row == -1 && desc.footer_row == -1)
				{
					if (desc.alt_footer_row != -1 && desc.border_type == 1)
					{
						desc.footer_row = desc.alt_footer_row;
						desc.last_data_row = desc.footer_row - 1;
					}
					else
					{
						/* fallback */
						desc.last_data_row = desc.last_row - 1;
						desc.footer_row = desc.last_row;
					}
				}

				trim_footer_rows(&desc);
			}
		}
	}

	create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
	create_layout(&scrdesc, &desc, first_data_row, first_row);

	print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row, 0);

	while (true)
	{
		bool		refresh_scr = false;
		bool		resize_scr = false;
		bool		generic_pager = desc.headline_transl == NULL;

		fix_rows_offset = desc.fixed_rows - scrdesc.fix_rows_rows;

		window_fill(scrdesc.luc, desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows, 0, -1, &desc, COLOR_PAIR(4) | ((scrdesc.theme != 12) ? A_BOLD : 0), 0, 0, 0, 0, 10, false, NULL);
		window_fill(scrdesc.rows, first_data_row + first_row - fix_rows_offset, scrdesc.fix_cols_cols + cursor_col, cursor_row - first_row + fix_rows_offset, &desc,
					COLOR_PAIR(3) | if_in_int(scrdesc.theme, (int[]) { 2, 12, 13, 14, -1}, A_BOLD, 0),
					(scrdesc.theme == 2 && generic_pager) ? A_BOLD : 0,
					COLOR_PAIR(8) | A_BOLD,
					COLOR_PAIR(6) | if_notin_int(scrdesc.theme, (int[]) { 13, 14, -1}, A_BOLD, 0),
					COLOR_PAIR(11) | if_in_int(scrdesc.theme, (int[]) {-1}, A_BOLD, 0) | (generic_pager ? A_BOLD : 0),
					COLOR_PAIR(6) | A_BOLD,
					false, &scrdesc);
		window_fill(scrdesc.fix_cols, first_data_row + first_row - fix_rows_offset, 0, cursor_row - first_row + fix_rows_offset, &desc,
					COLOR_PAIR(4) | ((scrdesc.theme != 12) ? A_BOLD : 0), 0, COLOR_PAIR(8) | A_BOLD,
					COLOR_PAIR(5) |  if_notin_int(scrdesc.theme, (int[]) {13, 14, -1}, A_BOLD, 0),
					COLOR_PAIR(11) | if_in_int(scrdesc.theme, (int[]) {-1}, A_BOLD, 0),
					COLOR_PAIR(6) | A_BOLD,
					false, NULL);
		window_fill(scrdesc.fix_rows, desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows, scrdesc.fix_cols_cols + cursor_col, -1, &desc, COLOR_PAIR(4) | ((scrdesc.theme != 12) ? A_BOLD : 0), 0, 0, 0, 0, 0, false, NULL);

		if (scrdesc.footer != NULL)
		{
			int		color;

			if (!generic_pager)
				color = COLOR_PAIR(9) | if_in_int(scrdesc.theme, (int[]) { -1}, A_BOLD, 0);
			else
				color = COLOR_PAIR(3) | if_in_int(scrdesc.theme, (int[]) { 2, 12, 13, 14, -1}, A_BOLD, 0);

			window_fill(scrdesc.footer,
								first_data_row + first_row + scrdesc.rows_rows - fix_rows_offset,
								footer_cursor_col,
								cursor_row - first_row - scrdesc.rows_rows + fix_rows_offset, &desc,
								color, 0, 0,
								COLOR_PAIR(10) | if_notin_int(scrdesc.theme, (int[]) { 13, 14, -1}, A_BOLD, 0), 0, 0, true,
								NULL);
		}

		if (scrdesc.luc != NULL)
			wrefresh(scrdesc.luc);
		if (scrdesc.rows != NULL)
			wrefresh(scrdesc.rows);
		if (scrdesc.fix_cols != NULL)
			wrefresh(scrdesc.fix_cols);
		if (scrdesc.fix_rows != NULL)
			wrefresh(scrdesc.fix_rows);
		if (scrdesc.footer != NULL)
			wrefresh(scrdesc.footer);

		refresh();
		if (c2 != 0)
		{
			c = c2;
			c2 = 0;
		}
		else
			c = getch();

		if (c == 'q' || c == KEY_F(10))
			break;

		prev_first_row = first_row;

		switch (c)
		{
			case 27:
				{
					int		second_char = getch();

					if (second_char == 'm')
					{
						if (use_mouse)
						{
							mousemask(0, &prev_mousemask);
							use_mouse = false;
						}
						else
						{
							mousemask(prev_mousemask, NULL);
							use_mouse = true;
						}

						c2 = show_info_wait(&scrdesc, " mouse handling: %s ", use_mouse ? "on" : "off");
						refresh_scr = true;
					}
					else if (second_char == 27 || second_char == '0')
						c2 = 'q';
				}
				break;

			case KEY_UP:
			case 'k':
				if (cursor_row > 0)
				{
					/*
					 * When we are on data position, and we are going up, and a fixed rows are hidden,
					 * then unhide fixed rows first (by decreasing first_row)
					 */
					if (fix_rows_offset > 0 && !is_footer_cursor(cursor_row, &scrdesc, &desc))
						first_row -= 1;
					else
						cursor_row -= 1;

					/*
					 * When fixed rows are hidden, then gap between first row and cursor row
					 * can be bigger (about fix_rows_offset.
					 */
					if (cursor_row + fix_rows_offset < first_row)
						first_row = cursor_row + fix_rows_offset;
				}
				else if (!no_sound)
					beep();
				break;

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
				_columns = c - '0';
				cursor_col = 0;
				refresh_scr = true;
				break;

			case KEY_DOWN:
			case 'j':
				{
					int		max_cursor_row;
					int		max_first_row;

					max_cursor_row = desc.last_row - desc.first_data_row;

					if (++cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						if (!no_sound)
							beep();
					}

					if (cursor_row - first_row > scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset - 1)
						first_row += 1;

					max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;

					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case 4:		/* CTRL D - forward half win */
				{
					int		offset = ((maxy - scrdesc.fix_rows_rows + desc.title_rows - 3) >> 1);
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
					max_cursor_row = desc.last_row - desc.first_data_row;

					if (first_row + offset <= max_first_row)
					{
						first_row += offset;
						cursor_row += offset;
					}
					else if (cursor_row + offset <= max_cursor_row)
					{
						cursor_row += offset;
						first_row = max_first_row;
					}
					else
					{
						cursor_row = max_cursor_row;
						first_row = max_first_row;
					}
				}
				break;

			case 21:	/* CTRL U - backward half win */
				{
					int		offset = ((maxy - scrdesc.fix_rows_rows + desc.title_rows - 3) >> 1);

					if (first_row - offset > 0)
					{
						first_row -= offset;
						cursor_row -= offset;
					}
					else if (cursor_row - offset > 0)
					{
						first_row = 0;
						cursor_row -= offset;
					}
					else
					{
						first_row = 0;
						cursor_row = 0;
					}
				}
				break;

			case 5:		/* CTRL E */
				{
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
					max_cursor_row = desc.last_row - desc.first_data_row;

					if (first_row < max_first_row)
					{
						first_row += 1;
						cursor_row += 1;
					}
					else if (cursor_row < max_cursor_row)
					{
						cursor_row += 1;
					}
				}
				break;

			case 25:	/* CTRL Y */
				if (first_row > 0)
				{
					first_row -= 1;
					cursor_row -= 1;
				}
				else if (cursor_row > 0)
					cursor_row -= 1;
				break;

			case KEY_LEFT:
			case 'h':
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_left:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col > 0)
							footer_cursor_col -= 1;
						else if (scrdesc.rows_rows >= 0)
						{
							_is_footer_cursor = false;
							footer_cursor_col = 0;
							goto recheck_left;
						}
					}
					else
					{
						int		move_left = 30;

						if (cursor_col == 0 && scrdesc.footer_rows > 0)
						{
							_is_footer_cursor = true;
							goto recheck_left;
						}

						if (desc.headline_transl != NULL)
						{
							int		i;

							for (i = 1; i <= 30; i++)
							{
								int		pos = scrdesc.fix_cols_cols + cursor_col - i;

								if (pos < 0)
									break;

								if (desc.headline_transl[i] == 'I')
								{
									move_left = i;
									break;
								}
							}
						}

						cursor_col -= move_left;
						if (cursor_col < 3)
							cursor_col = 0;
					}
				}
				break;

			case KEY_RIGHT:
			case 'l':
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_right:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						int max_footer_cursor_col = desc.footer_char_size - maxx;

						if (footer_cursor_col + 1 >= max_footer_cursor_col && scrdesc.rows_rows >= 0)
						{
							_is_footer_cursor = false;
							footer_cursor_col = max_footer_cursor_col;
							goto recheck_right;
						}
						else
							footer_cursor_col += 1;

						if (footer_cursor_col > max_footer_cursor_col)
							footer_cursor_col = max_footer_cursor_col;
					}
					else
					{
						int		move_right = 30;
						int		max_cursor_col;
						int		new_cursor_col = cursor_col;

						if (desc.headline_transl != NULL)
						{
							int		i;
							char   *str = &desc.headline_transl[scrdesc.fix_cols_cols + cursor_col];

							for (i = 1; i <= 30; i++)
							{
								if (str[i] == 'I')
								{
									move_right = i + 1;
									break;
								}
							}
						}

						new_cursor_col += move_right;

						if (desc.headline_transl != NULL)
							max_cursor_col = desc.headline_char_size - maxx;
						else
							max_cursor_col = desc.maxx - maxx - 1;

						max_cursor_col = max_cursor_col > 0 ? max_cursor_col : 0;

						if (new_cursor_col > max_cursor_col)
							new_cursor_col = max_cursor_col;

						if (new_cursor_col == cursor_col && scrdesc.footer_rows > 0)
						{
							_is_footer_cursor = true;
							goto recheck_right;
						}
						cursor_col = new_cursor_col;
					}
				}
				break;

			case 538:		/* CTRL HOME */
			case 'g':
				cursor_row = 0;
				first_row = 0;
				break;

			case 533:		/* CTRL END */
			case 'G':
				cursor_row = desc.last_row - desc.first_data_row;
				first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
				break;

			case 'H':
				cursor_row = first_row;
				break;
			case 'L':
				cursor_row = first_row + maxy - scrdesc.fix_rows_rows + desc.title_rows - 3;
				break;
			case 'M':
				cursor_row = first_row + ((maxy - scrdesc.fix_rows_rows + desc.title_rows - 3) >> 1);
				break;

			case KEY_PPAGE:
			case 2:		/* CTRL B */
				{
					int		offset;

					if (desc.is_expanded_mode &&
							scrdesc.first_rec_title_y != -1 && scrdesc.last_rec_title_y != -1)
						offset = scrdesc.last_rec_title_y - scrdesc.first_rec_title_y;
					else
						offset = scrdesc.main_maxy - scrdesc.fix_rows_rows;

					if (first_row > 0)
					{
						first_row -= offset;
						if (first_row < 0)
							first_row = 0;
					}
					if (cursor_row > 0)
					{
						cursor_row -= offset;
						if (cursor_row < 0)
							cursor_row = 0;
					}
					else if (!no_sound)
						beep();
				}
				break;

			case KEY_NPAGE:
			case ' ':
			case 6:		/* CTRL F */
				{
					int		max_cursor_row;
					int		max_first_row;
					int		offset;

					if (desc.is_expanded_mode &&
							scrdesc.first_rec_title_y != -1 && scrdesc.last_rec_title_y != -1)
						offset = scrdesc.last_rec_title_y - scrdesc.first_rec_title_y;
					else
						offset = scrdesc.main_maxy - scrdesc.fix_rows_rows;

					first_row += offset;
					cursor_row += offset;

					max_cursor_row = desc.last_row - desc.first_data_row;
					if (cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						if (!no_sound)
							beep();
					}

					if (cursor_row - first_row > scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset - 1)
						first_row += 1;

					max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case KEY_RESIZE:
				refresh_scr = true;
				resize_scr = true;
			break;

			case KEY_HOME:
			case '^':
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_home:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col > 0)
							footer_cursor_col = 0;
						else if (scrdesc.rows_rows > 0)
						{
							footer_cursor_col = 0;
							_is_footer_cursor = false;
							goto recheck_home;
						}
					}
					else
					{
						if (cursor_col > 0)
							cursor_col = 0;
						else if (scrdesc.footer_rows > 0)
						{
							cursor_col = 0;
							_is_footer_cursor = true;
							goto recheck_home;
						}
					}
					break;
				}

			case KEY_END:
			case '$':
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_end:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col < desc.footer_char_size - maxx)
							footer_cursor_col = desc.footer_char_size - maxx;
						else if (scrdesc.rows_rows > 0)
						{
							footer_cursor_col = desc.footer_char_size - maxx;
							_is_footer_cursor = false;
							goto recheck_end;
						}
					}
					else
					{
						int		new_cursor_col;

						if (desc.headline != NULL)
							new_cursor_col = desc.headline_char_size - maxx;
						else
							new_cursor_col = desc.maxx - maxx - 1;

						new_cursor_col = new_cursor_col > 0 ? new_cursor_col : 0;
						if (new_cursor_col > cursor_col)
							cursor_col = new_cursor_col;
						else if (scrdesc.footer_rows > 0)
						{
							_is_footer_cursor = true;
							cursor_col = new_cursor_col;
							goto recheck_end;
						}
					}
					break;
				}
			case 's':
				{
					char	buffer[1024];
					FILE   *fp;
					bool	ok = false;

					get_string(&scrdesc, "log file: ", buffer, sizeof(buffer) - 1);

					fp = fopen(buffer, "w");
					if (fp != NULL)
					{
						LineBuffer *lnb = &desc.rows;

						ok = true;

						while (lnb != NULL)
						{
							for (i = 0; i < lnb->nrows; i++)
							{
								fprintf(fp, "%s", lnb->rows[i]);
								if (errno != 0)
								{
									ok = false;
									goto exit;
								}
							}
							lnb = lnb->next;
						}

						fclose(fp);
					}
exit:

					if (!ok)
						c2 = show_info_wait(&scrdesc, " Cannot write to %s ", buffer);

					refresh_scr = true;

					break;
				}

			case '/':
				{
					char	locsearchterm[256];

					get_string(&scrdesc, "/", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm) - 1);

					/* continue to find next: */
				}
			case 'n':
				{
					int		current_row = scrdesc.fix_rows_rows;
					int		nrows;
					int		max_first_row;
					LineBuffer   *rows = &desc.rows;
					bool	found = false;

					for (nrows = 0; nrows < desc.last_row - scrdesc.fix_rows_rows; nrows++, current_row++)
					{
						if (current_row == 1000)
						{
							rows = rows->next;
							current_row = 0;
						}

						if (nrows <= cursor_row) /* skip to start */
							continue;
						if (!strstr(rows->rows[current_row], scrdesc.searchterm))
							continue;

						cursor_row = nrows - desc.title_rows;
						found = true;

						if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows - 3)
							first_row = cursor_row - maxy + scrdesc.fix_rows_rows + 3;

						max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
						if (max_first_row < 0)
							max_first_row = 0;
						if (first_row > max_first_row)
							first_row = max_first_row;

						break;
					}

					if (!found)
						c2 = show_info_wait(&scrdesc, " Not found ", NULL);

					refresh_scr = true;
				}
				break;

			case '?':
				{
					char	locsearchterm[256];

					get_string(&scrdesc, "?", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm) - 1);

					/* continue to find next: */
				}
			case 'N':
				{
					int		rowidx;
					int		search_row;

					LineBuffer   *rows = &desc.rows;
					bool	found = false;

					rowidx = cursor_row + scrdesc.fix_rows_rows + desc.title_rows - 1;
					search_row = cursor_row - 1;

					while (rowidx > 1000)
					{
						rows = rows->next;
						rowidx -= 1000;
					}

					while (search_row > 0)
					{
						if (rowidx < 0)
						{
							rows = rows->prev;
							rowidx = 1000;
							continue;
						}

						if (strstr(rows->rows[rowidx], scrdesc.searchterm) != NULL)
						{
							cursor_row = search_row;
							if (first_row > cursor_row)
								first_row = cursor_row;

							found = true;
							break;
						}

						rowidx -= 1;
						search_row -= 1;
					}

					if (!found)
						c2 = show_info_wait(&scrdesc, " Not found ", NULL);

					refresh_scr = true;
				}
				break;

			case KEY_MOUSE:
				{
					MEVENT		event;

					if (getmouse(&event) == OK)
					{

#if NCURSES_MOUSE_VERSION > 1

						if (event.bstate & BUTTON_ALT && event.bstate & BUTTON5_PRESSED)
						{
							c2 = 'l';
							break;
						}

						if (event.bstate & BUTTON_ALT && event.bstate & BUTTON4_PRESSED)
						{
							c2 = 'h';
							break;
						}

						if (event.bstate & BUTTON5_PRESSED)
						{
							int		max_cursor_row;
							int		max_first_row;
							int		offset = 1;

							max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
							if (max_first_row < 0)
								max_first_row = 0;

							if (desc.headline_transl != NULL)
								offset = (scrdesc.main_maxy - scrdesc.fix_rows_rows) / 3;

							if (first_row + offset > max_first_row)
								offset = 1;

							first_row += offset;
							cursor_row += offset;

							max_cursor_row = desc.last_row - desc.first_data_row;
							if (cursor_row > max_cursor_row)
							{
								cursor_row = max_cursor_row;
								if (!no_sound)
									beep();
							}

							if (cursor_row - first_row > scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset - 1)
								first_row += 1;

							if (first_row > max_first_row)
								first_row = max_first_row;
						}
						else if (event.bstate & BUTTON4_PRESSED)
						{
							int		offset = 1;

							if (desc.headline_transl != NULL)
								offset = (scrdesc.main_maxy - scrdesc.fix_rows_rows) / 3;

							if (first_row <= offset)
								offset = 1;

							if (first_row > 0)
							{
								first_row -= offset;
								if (first_row < 0)
									first_row = 0;
							}
							if (cursor_row > 0)
							{
								cursor_row -= offset;
								if (cursor_row < 0)
									cursor_row = 0;
							}
							else if (!no_sound)
								beep();
						}
						else

#endif

						if (event.bstate & BUTTON1_PRESSED)
						{
							int		max_cursor_row;
							int		max_first_row;

							cursor_row = event.y - scrdesc.fix_rows_rows - 1 + first_row - fix_rows_offset;
							if (cursor_row < 0)
								cursor_row = 0;

							if (cursor_row + fix_rows_offset < first_row)
								first_row = cursor_row + fix_rows_offset;

							max_cursor_row = desc.last_row - desc.first_data_row;
							if (cursor_row > max_cursor_row)
								cursor_row = max_cursor_row;

							if (cursor_row - first_row > scrdesc.main_maxy - scrdesc.fix_rows_rows + desc.title_rows - fix_rows_offset)
								first_row += 1;

							max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
							if (max_first_row < 0)
								max_first_row = 0;
							if (first_row > max_first_row)
								first_row = max_first_row;
						}
					}
				}
				break;
		}

		print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset);

		if (first_row != prev_first_row)
		{
			/* now, maybe more/less rows from footer should be displayed */
			if (desc.headline_transl != NULL && desc.footer_row > 0)
			{
				int		rows_rows;

				rows_rows = min_int(desc.footer_row - scrdesc.fix_rows_rows - first_row - desc.title_rows,
									 scrdesc.main_maxy - scrdesc.fix_rows_rows);
				rows_rows = rows_rows > 0 ? rows_rows : 0;

				if (!refresh_scr)
				{
					refresh_scr = scrdesc.rows_rows != rows_rows;
				}
			}
		}

		if (refresh_scr)
		{
			if (resize_scr)
			{
				struct winsize size;

				/*
				 * Workaround - the variables COLS, LINES are not refreshed
				 * when pager is resized and executed inside psql.
				 */
				if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
				{
					resize_term(size.ws_row, size.ws_col);
					clear();
				}

				resize_scr = false;
			}

			getmaxyx(stdscr, maxy, maxx);

			refresh_aux_windows(&scrdesc, &desc);
			create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
			create_layout(&scrdesc, &desc, first_data_row, first_row);
			print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset);

			refresh_scr = false;
		}
	}

	endwin();

	if (no_alternate_screen)
	{
		draw_data(&scrdesc, &desc, first_data_row, first_row, cursor_col,
				  footer_cursor_col, fix_rows_offset);
	}

	return 0;
}
