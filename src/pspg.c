/*-------------------------------------------------------------------------
 *
 * pspg.c
 *	  a terminal pager designed for usage from psql
 *
 * Portions Copyright (c) 2017-2019 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pspg.c
 *
 *-------------------------------------------------------------------------
 */
#if defined HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#include <ncurses.h>
#elif defined HAVE_CURSES_H
#include <curses.h>
#else
/* fallback */
#include <ncurses/ncurses.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <langinfo.h>
#include <libgen.h>
#include <locale.h>
#include <signal.h>

#ifdef GWINSZ_IN_SYS_IOCTL
# include <sys/ioctl.h>
#else
# include <termios.h>
#endif

#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "config.h"
#include "pspg.h"
#include "themes.h"
#include "unicode.h"

#ifdef COMPILE_MENU

#include "st_menu.h"

#endif

#ifdef HAVE_LIBREADLINE

#if defined(HAVE_READLINE_READLINE_H)

#include <readline/readline.h>

#elif defined(HAVE_READLINE_H)

#include <readline.h>

#endif

#if RL_VERSION_MAJOR < 6
#define rl_display_prompt rl_prompt
#endif
#endif

#ifdef HAVE_READLINE_HISTORY
#if defined(HAVE_READLINE_HISTORY_H)
#include <readline/history.h>
#elif defined(HAVE_HISTORY_H)
#include <history.h>
#endif
#endif

#define PSPG_VERSION "1.7.0"

/* GNU Hurd does not define MAXPATHLEN */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifdef HAVE_LIBREADLINE

char		readline_buffer[1024];
bool		got_readline_string;
bool		force8bit;
static unsigned char	input;
static bool input_avail = false;

static WINDOW *g_bottom_bar;

#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

#define		USE_EXTENDED_NAMES

#ifdef DEBUG_PIPE

FILE *debug_pipe = NULL;
int	debug_eventno = 0;

#endif

bool	press_alt = false;
MEVENT		event;

static int number_width(int num);
static int get_event(MEVENT *mevent, bool *alt);

int
min_int(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
}

/*
 * Prints error message and stops application
 */
void
leave_ncurses(const char *str)
{
	endwin();
	fprintf(stderr, "%s\n", str);
	exit(1);
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

/*
 * Multiple used block - searching in string based on configuration
 */
const char *
pspg_search(Options *opts, ScrDesc *scrdesc, const char *str)
{
	bool	ignore_case = opts->ignore_case;
	bool	ignore_lower_case = opts->ignore_lower_case;
	bool	force8bit = opts->force8bit;
	bool	has_upperchr = scrdesc->has_upperchr;
	const char *searchterm = scrdesc->searchterm;
	const char *result;

	if (ignore_case || (ignore_lower_case && !has_upperchr))
	{
		result = force8bit ? nstrstr(str, searchterm) : utf8_nstrstr(str, searchterm);
	}
	else if (ignore_lower_case && has_upperchr)
	{
		result = force8bit ? nstrstr_ignore_lower_case(str, searchterm) : utf8_nstrstr_ignore_lower_case(str, searchterm);
	}
	else
		result = strstr(str, searchterm);

	return result;
}

/*
 * Translate from UTF8 to semantic characters.
 */
static bool
translate_headline(Options *opts, DataDesc *desc)
{
	char   *srcptr;
	char   *destptr;
	char   *last_black_char = NULL;
	bool	broken_format = false;
	int		processed_chars = 0;
	bool	is_expanded_info = false;
	bool	force8bit = opts->force8bit;

	srcptr = desc->headline;
	destptr = malloc(desc->headline_size + 1);
	memset(destptr, 0, desc->headline_size + 1);
	desc->headline_transl = destptr;

	desc->linestyle = 'a';
	desc->border_type = 0;

	desc->expanded_info_minx = -1;

	desc->cranges = NULL;
	desc->columns = 0;

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
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
		}
		else if (is_expanded_info)
		{
			if (*srcptr == ']')
			{
				is_expanded_info = false;
			}
			*destptr++ = 'd';
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
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
		else if (*srcptr == '|')
		{
			if (processed_chars == 0 && srcptr[1] == '-')
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
				desc->is_pgcli_fmt = true;
			}
			else if (processed_chars > 0 && desc->is_pgcli_fmt && srcptr[-1] == '-')
			{
				*destptr++ = 'R';
			}
			else
			{
				broken_format = true;
				break;
			}
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
		char	   *ptr;
		int			i;
		int			offset;

		last_black_char[1] = '\0';
		desc->headline_char_size = strlen(desc->headline_transl);

		desc->columns = 1;

		ptr = desc->headline_transl;
		while (*ptr)
		{
			if (*ptr++ == 'I')
				desc->columns += 1;
		}

		desc->cranges = malloc(desc->columns * sizeof(CRange));

		i = 0; offset = 0;
		ptr = desc->headline_transl;
		desc->cranges[0].xmin = 0;

		while (*ptr)
		{
			if (*ptr == 'I')
			{
				desc->cranges[i++].xmax = offset;
				desc->cranges[i].xmin = offset;
			}

			offset += 1;
			ptr +=1;
		}

		desc->cranges[i].xmax = offset - 1;

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
trim_footer_rows(Options *opts, DataDesc *desc)
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

			len = opts->force8bit ? strlen(line) : utf8len(line);
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

	/* ascii */
	if ((str[0] == '+' || str[0] == '-') && str[1] == '-')
		return true;

	/* pgcli fmt */
	if (str[0] == '|' && str[1] == '-')
		return true;

	/* expanded border 1 */
	if (str[0] == '-' && str[1] == '[')
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
bool
is_expanded_header(Options *opts, char *str, int *ei_minx, int *ei_maxx)
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
			str += opts->force8bit ? 1 : utf8charlen(*str);
		}

		*ei_maxx = pos - 1;
	}

	return true;
}

/*
 * Copy trimmed string
 */
static void
strncpytrim(Options *opts, char *dest, const char *src,
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
		size_t	clen;

		if (*src == '\0')
			break;

		clen = (size_t) opts->force8bit ? 1 : utf8charlen(*src);
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

/*
 * Read data from file and fill DataDesc.
 */
static int
readfile(FILE *fp, Options *opts, DataDesc *desc)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	LineBuffer *rows;

	/* safe reset */
	desc->filename[0] = '\0';

	if (fp != NULL)
	{
		if (opts->pathname != NULL)
		{
			char	   *name;

			name = basename(opts->pathname);
			strncpy(desc->filename, name, 64);
			desc->filename[64] = '\0';
		}
	}
	else
		fp = stdin;

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
	desc->is_pgcli_fmt = false;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	rows = &desc->rows;
	desc->rows.prev = NULL;

	errno = 0;

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		clen;

		if (line[read - 1] == '\n')
		{
			line[read - 1] = '\0';
			read -= 1;
		}

		clen = utf8len(line);

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
			strncpytrim(opts, desc->title, line, 63, read);
			desc->title_rows = 1;
		}

		if (desc->border_head_row == -1 && desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
			desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;

			if (!desc->is_expanded_mode)
				desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);

			/* title surely doesn't it there */
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

		nrows += 1;

		line = NULL;
	}

	if (errno != 0)
	{
		fprintf(stderr, "cannot to read file: %s\n", strerror(errno));
		exit(1);
	}

	/*
	 * border headline cannot be higher than 1000, to simply find it
	 * in first row block. Higher number is surelly wrong, probably
	 * some comment.
	 */
	if (desc->border_top_row >= 1000)
		desc->border_top_row = -1;
	if (desc->border_head_row >= 1000)
		desc->border_head_row = -1;

	if (desc->last_row != -1)
		desc->maxy = desc->last_row;

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
		desc->last_data_row = desc->last_row;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	return 0;
}

/*
 * Prepare dimensions of windows layout
 */
static void
create_layout_dimensions(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
				   int fixCols, int fixRows,
				   int maxy, int maxx)
{
	scrdesc->maxy = maxy;
	scrdesc->maxx = maxx;

	if (opts->show_rownum)
	{
		int startx = number_width(desc->maxy) + 2;

		scrdesc->main_start_x = startx;
		scrdesc->main_maxx -= startx;
	}

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
	else if (!desc->is_expanded_mode && desc->border_head_row != -1 &&
			  desc->headline_transl != NULL)
	{
		scrdesc->fix_rows_rows = desc->border_head_row + 1 - desc->title_rows;
	}

	/* disable fixed parts when is not possible draw in screen */
	if (scrdesc->fix_cols_cols > scrdesc->main_maxx)
		scrdesc->fix_cols_cols = 0;

	if (scrdesc->fix_rows_rows > scrdesc->main_maxy)
		scrdesc->fix_rows_rows = 0;

	if (scrdesc->fix_rows_rows == 0 && !desc->is_expanded_mode)
	{
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	desc->fixed_rows = scrdesc->fix_rows_rows;
}

static void
create_layout(Options *opts, ScrDesc *scrdesc, DataDesc *desc, int first_data_row, int first_row)
{
	if (w_luc(scrdesc) != NULL)
	{
		delwin(w_luc(scrdesc));
		w_luc(scrdesc) = NULL;
	}
	if (w_fix_rows(scrdesc) != NULL)
	{
		delwin(w_fix_rows(scrdesc));
		w_fix_rows(scrdesc) = NULL;
	}
	if (w_fix_cols(scrdesc) != NULL)
	{
		delwin(w_fix_cols(scrdesc));
		w_fix_cols(scrdesc) = NULL;
	}
	if (w_rows(scrdesc) != NULL)
	{
		delwin(w_rows(scrdesc));
		w_rows(scrdesc) = NULL;
	}
	if (w_footer(scrdesc) != NULL)
	{
		delwin(w_footer(scrdesc));
		w_footer(scrdesc) = NULL;
	}
	if (w_rownum(scrdesc) != NULL)
	{
		delwin(w_rownum(scrdesc));
		w_rownum(scrdesc) = NULL;
	}
	if (w_rownum_luc(scrdesc) != NULL)
	{
		delwin(w_rownum_luc(scrdesc));
		w_rownum_luc(scrdesc) = NULL;
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
			w_footer(scrdesc) = subwin(stdscr,
										scrdesc->footer_rows,
										scrdesc->main_maxx,
										scrdesc->main_start_y + scrdesc->fix_rows_rows + scrdesc->rows_rows,
										scrdesc->main_start_x);
		}
	}
	else if (desc->headline_transl != NULL)
	{
		scrdesc->rows_rows = min_int(scrdesc->main_maxy - scrdesc->fix_rows_rows,
									 desc->last_row - desc->first_data_row + 1);
	}
	else
	{
		scrdesc->rows_rows = 0;
		scrdesc->fix_rows_rows = 0;
		scrdesc->footer_rows = min_int(scrdesc->main_maxy, desc->last_row + 1);
		w_footer(scrdesc) = subwin(stdscr,
									scrdesc->footer_rows,
									scrdesc->main_maxx,
									scrdesc->main_start_y,
									scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		w_fix_rows(scrdesc) = subwin(stdscr,
									  scrdesc->fix_rows_rows,
									  scrdesc->main_maxx - scrdesc->fix_cols_cols,
									  scrdesc->main_start_y,
									  scrdesc->fix_cols_cols + scrdesc->main_start_x);
	}

	if (scrdesc->fix_cols_cols > 0 && scrdesc->rows_rows > 0)
	{
		w_fix_cols(scrdesc) = subwin(stdscr,
									  scrdesc->rows_rows,
									  scrdesc->fix_cols_cols,
									  scrdesc->fix_rows_rows + scrdesc->main_start_y,
									  scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		w_luc(scrdesc) = subwin(stdscr,
								scrdesc->fix_rows_rows,
								scrdesc->fix_cols_cols,
								scrdesc->main_start_y,
								scrdesc->main_start_x);
	}

	if (scrdesc->rows_rows > 0)
	{
		w_rows(scrdesc) = subwin(stdscr, scrdesc->rows_rows,
								   scrdesc->main_maxx - scrdesc->fix_cols_cols,
								   scrdesc->fix_rows_rows + scrdesc->main_start_y,
								   scrdesc->fix_cols_cols + scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0 && opts->show_rownum)
	{
		Theme   *theme = &scrdesc->themes[WINDOW_ROWNUM_LUC];

		w_rownum_luc(scrdesc) = subwin(stdscr,
								   scrdesc->fix_rows_rows,
								   scrdesc->main_start_x,
								   scrdesc->main_start_y,
								   0);

		wbkgd(w_rownum_luc(scrdesc), theme->data_attr);
	}
	if (scrdesc->rows_rows + scrdesc->footer_rows > 0 && opts->show_rownum)
	{
		w_rownum(scrdesc) = subwin(stdscr,
								   scrdesc->rows_rows + scrdesc->footer_rows,
								   scrdesc->main_start_x,
								   scrdesc->fix_rows_rows + scrdesc->main_start_y,
								   0);
	}
}

/*
 * Refresh aux windows like top bar or bottom bar.
 */
static void
refresh_aux_windows(Options *opts, ScrDesc *scrdesc, DataDesc *desc)
{
	int		maxy, maxx;
	WINDOW	   *top_bar = w_top_bar(scrdesc);
	WINDOW	   *bottom_bar = w_bottom_bar(scrdesc);

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	if (top_bar != NULL)
	{
		delwin(top_bar);
		top_bar = NULL;
		w_top_bar(scrdesc) = NULL;
	}

	if (opts->no_topbar)
		scrdesc->top_bar_rows = 0;
	else
	{
		scrdesc->top_bar_rows = 1;
		top_bar = subwin(stdscr, 1, 0, 0, 0);
		wbkgd(top_bar, COLOR_PAIR(2));
		wnoutrefresh(top_bar);
		w_top_bar(scrdesc) = top_bar;
	}

	if (bottom_bar != NULL)
	{
		delwin(bottom_bar);
		bottom_bar = NULL;
		w_bottom_bar(scrdesc) = NULL;
	}

	bottom_bar = subwin(stdscr, 1, 0, maxy - 1, 0);
	w_bottom_bar(scrdesc) = bottom_bar;
	werase(bottom_bar);

	/* data colours are better than default */
	wbkgd(bottom_bar, COLOR_PAIR(3));
	wnoutrefresh(bottom_bar);

	scrdesc->main_maxy = maxy;
	scrdesc->main_maxx = maxx;
	scrdesc->main_start_y = 0;
	scrdesc->main_start_x = 0;

	if (top_bar != NULL)
	{
		scrdesc->main_maxy -= 1;
		scrdesc->main_start_y = 1;
	}

	if (bottom_bar != NULL)
	{
		if (!opts->no_commandbar)
			scrdesc->main_maxy -= 1;
	}
}

/*
 * Returns width of number
 */
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
	if (w_footer(scrdesc) == NULL)
		return false;
	else if (scrdesc->rows_rows == 0)
		return true;

	return cursor_row + scrdesc->fix_rows_rows + desc->title_rows + 1 > desc->footer_row;
}

static void
print_status(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
						 int cursor_row, int cursor_col, int first_row, int fix_rows_offset)
{
	int		maxy, maxx;
	int		smaxy, smaxx;
	char	buffer[200];
	WINDOW   *top_bar = w_top_bar(scrdesc);
	WINDOW   *bottom_bar = w_bottom_bar(scrdesc);
	Theme	*top_bar_theme = &scrdesc->themes[WINDOW_TOP_BAR];
	Theme	*bottom_bar_theme = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	/* do nothing when there are not top status bar */
	if (scrdesc->top_bar_rows > 0)
	{
		getmaxyx(top_bar, maxy, maxx);
		getmaxyx(stdscr, smaxy, smaxx);

		(void) maxy;

		werase(top_bar);

		wattron(top_bar, top_bar_theme->title_attr);
		if (desc->title[0] != '\0' && desc->title_rows > 0)
			mvwprintw(top_bar, 0, 0, "%s", desc->title);
		else if (desc->filename[0] != '\0')
			mvwprintw(top_bar, 0, 0, "%s", desc->filename);
		wattroff(top_bar, top_bar_theme->title_attr);

		if (opts->no_cursor)
		{
			if (desc->headline_transl)
			{
				snprintf(buffer, 199, "FC:%*d C:%*d..%*d/%*d  L:%*d/%*d %3.0f%%",
								number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
								number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
								number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
								number_width(desc->headline_char_size), desc->headline_char_size,
								number_width(desc->maxy - desc->fixed_rows), first_row + scrdesc->main_maxy - fix_rows_offset - desc->fixed_rows - desc->title_rows,
								number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
								(first_row + scrdesc->main_maxy - 1 - desc->fixed_rows - desc->title_rows) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
			}
			else
			{
				snprintf(buffer, 199, "C:%*d..%*d/%*d  L:%*d/%*d %3.0f%%",
								number_width(desc->maxx), cursor_col + scrdesc->fix_cols_cols + 1,
								number_width(desc->maxx), min_int(smaxx + cursor_col, desc->maxx),
								number_width(desc->maxx), desc->maxx,
								number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + scrdesc->main_maxy,
								number_width(desc->last_row), desc->last_row + 1,
								((first_row + scrdesc->main_maxy) / ((double) (desc->last_row + 1))) * 100.0);
			}
		}
		else
		{
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
								number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
								(cursor_row + 1) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
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
		}

		mvwprintw(top_bar, 0, maxx - strlen(buffer), "%s", buffer);
		wnoutrefresh(top_bar);
	}

	if (opts->less_status_bar)
	{
		/* less-status-bar */
		char	title[65];
		char	*str;
		size_t	bytes = sizeof(title) - 2;
		char	*ptr = title;

		if (desc->title_rows > 0 && desc->title[0] != '\0')
			str = desc->title;
		else if (desc->filename[0] != '\0')
			str = desc->filename;
		else
			str = "";

		while (bytes > 0 && *str != '\0')
		{
			size_t		sz = opts->force8bit ? 1 : utf8charlen(*str);

			if (sz > bytes)
				break;

			memcpy(ptr, str, sz);
			ptr += sz;
			str += sz;
			bytes -= sz;
		}

		if (ptr != title)
			*ptr++ = ' ';
		*ptr = '\0';

		wattron(bottom_bar, bottom_bar_theme->prompt_attr);

		if (desc->headline_transl)
		{
			snprintf(buffer, 199, "%slines %d-%d/%d %.0f%% ",
								title,
								first_row + 1 - fix_rows_offset,
								first_row + 1 - fix_rows_offset + scrdesc->rows_rows,
								desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
								(cursor_row + 1) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
		}
		else
		{
			snprintf(buffer, 199, "%slines %d-%d/%d %.0f%% ",
								title,
								first_row + 1,
								first_row + 1 + scrdesc->footer_rows,
								desc->last_row + 1,
								((cursor_row + 1) / ((double) (desc->last_row + 1))) * 100.0);
		}

		mvwprintw(bottom_bar, 0, 0, "%s", buffer);
		wclrtoeol(bottom_bar);
		wnoutrefresh(bottom_bar);

		wattroff(bottom_bar, bottom_bar_theme->prompt_attr);
	}
}

static void
make_beep(Options *opts)
{
	if (!opts->no_sound)
		beep();
}

/*
 * It is used for result of action info
 */
static int
show_info_wait(Options *opts, ScrDesc *scrdesc, char *fmt, char *par, bool beep, bool refresh_first, bool applytimeout)
{
	int		c;
	WINDOW	*bottom_bar = w_bottom_bar(scrdesc);
	Theme	*t = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	/*
	 * When refresh is required first, then store params and quit immediately.
	 * Only once can be info moved after refresh
	 */
	if (refresh_first && scrdesc->fmt == NULL)
	{
		if (fmt != NULL)
			scrdesc->fmt = strdup(fmt);
		else
			scrdesc->fmt = NULL;

		if (par != NULL)
			scrdesc->par = strdup(par);
		else
			scrdesc->par = NULL;
		scrdesc->beep = beep;
		scrdesc->applytimeout = applytimeout;

		return 0;
	}

	wattron(bottom_bar, t->bottom_light_attr);

	if (par != NULL)
		mvwprintw(bottom_bar, 0, 0, fmt, par);
	else
		mvwprintw(bottom_bar, 0, 0, "%s", fmt);

	wclrtoeol(bottom_bar);
	wattroff(bottom_bar,  t->bottom_light_attr);
	wnoutrefresh(bottom_bar);

	refresh();

	if (beep)
		make_beep(opts);

	if (applytimeout)
		timeout(2000);
	c = get_event(&event, &press_alt);
	timeout(-1);

	/*
	 * Screen should be refreshed after show any info.
	 */
	scrdesc->refresh_scr = true;

	return c == ERR ? 0 : c;
}

#ifdef HAVE_LIBREADLINE

static int
readline_input_avail(void)
{
    return input_avail;
}

static int
readline_getc(FILE *dummy)
{
    input_avail = false;
    return input;
}

static void
forward_to_readline(char c)
{
    input = c;
    input_avail = true;
    rl_callback_read_char();
}


static void
got_string(char *line)
{
	if (line)
	{

#ifdef HAVE_READLINE_HISTORY

		if (*line)
			add_history(line);

#endif

		strcpy(readline_buffer, line);
	}
	else
		readline_buffer[0] = '\0';

	got_readline_string = true;
}

static void
readline_redisplay()
{
	size_t cursor_col;

	if (!force8bit)
	{
		size_t prompt_dsplen = utf_string_dsplen(rl_display_prompt, SIZE_MAX);

		cursor_col = prompt_dsplen
					  + readline_utf_string_dsplen(rl_line_buffer, rl_point, prompt_dsplen);
	}
	else
	{
		cursor_col = strlen(rl_display_prompt) + min_int(strlen(rl_line_buffer), rl_point);
	}

	werase(g_bottom_bar);
	mvwprintw(g_bottom_bar, 0, 0, "%s%s", rl_display_prompt, rl_line_buffer);

	if (cursor_col >= COLS)
		curs_set(0);
	else
	{
		wmove(g_bottom_bar, 0, cursor_col);
		curs_set(2);
	}

	wrefresh(g_bottom_bar);
}

#endif

static void
get_string(Options *opts, ScrDesc *scrdesc, char *prompt, char *buffer, int maxsize)
{
	WINDOW	*bottom_bar = w_bottom_bar(scrdesc);

#ifdef HAVE_LIBREADLINE

	int		c;
	mmask_t		prev_mousemask = 0;

	g_bottom_bar = bottom_bar;
	got_readline_string = false;
	force8bit = opts->force8bit;

	mvwprintw(bottom_bar, 0, 0, "");
	wclrtoeol(bottom_bar);
	curs_set(1);
	echo();

	rl_getc_function = readline_getc;

#if RL_READLINE_VERSION >= 0x0603

	rl_input_available_hook = readline_input_avail;

#endif

	rl_redisplay_function = readline_redisplay;

	rl_callback_handler_install(prompt, got_string);

	mousemask(0, &prev_mousemask);

	while (!got_readline_string)
	{
		c = wgetch(bottom_bar);

		forward_to_readline(c);
		wrefresh(bottom_bar);
	}

	mousemask(prev_mousemask, NULL);

	rl_callback_handler_remove();

	curs_set(0);
	noecho();

	buffer[maxsize] = '\0';
	strncpy(buffer, readline_buffer, maxsize - 1);

#else

	mvwprintw(bottom_bar, 0, 0, "%s", prompt);
	wclrtoeol(bottom_bar);
	curs_set(1);
	echo();
	wgetnstr(bottom_bar, buffer, maxsize);
	curs_set(0);
	noecho();

#endif

	/*
	 * Screen should be refreshed after show any info.
	 */
	scrdesc->refresh_scr = true;
}

#define SEARCH_FORWARD			1
#define SEARCH_BACKWARD			2

static bool
has_upperchr(Options *opts, char *str)
{
	if (opts->force8bit)
	{
		while (*str != '\0')
		{
			if (isupper(*str))
				return true;

			str += 1;
		}
	}
	else
	{
		while (*str != '\0')
		{
			if (utf8_isupper(str))
				return true;

			str += utf8charlen(*str);
		}
	}

	return false;
}

static void
reset_searching_lineinfo(LineBuffer *lnb)
{
	while (lnb != NULL)
	{
		if (lnb->lineinfo != NULL)
		{
			int		i;

			for (i = 0; i < lnb->nrows; i++)
			{
				lnb->lineinfo[i].mask |= LINEINFO_UNKNOWN;
				lnb->lineinfo[i].mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);
			}
		}
		lnb = lnb->next;
	}
}

/*
 * Replace tilde by HOME dir
 */
static char *
tilde(char *path)
{
	static char	writebuf[MAXPATHLEN];
	int			chars = 0;
	char *w;

	w = writebuf;

	while (*path && chars < MAXPATHLEN - 1)
	{
		if (*path == '~')
		{
			char *home = getenv("HOME");

			if (home == NULL)
				leave_ncurses("HOME directory is not defined");

			while (*home && chars < MAXPATHLEN - 1)
			{
				*w++ = *home++;
				chars += 1;
			}
			path++;
		}
		else
		{
			*w++ = *path++;
			chars += 1;
		}
	}

	*w = '\0';

	return writebuf;
}

/*
 * Calculate focus point from left border of selected columns.
 */
static int
get_x_focus(int vertical_cursor_column,
			int cursor_col,
			DataDesc *desc,
			ScrDesc *scrdesc)
{
	int xmin = desc->cranges[vertical_cursor_column - 1].xmin;

	return xmin > scrdesc->fix_cols_cols ? xmin - cursor_col : xmin;
}

/*
 * Reads keycode. When keycode is Esc - then read next keycode, and sets flag alt.
 * When keycode is related to mouse, then get mouse event details.
 */
static int
get_event(MEVENT *mevent, bool *alt)
{
	bool	first_event = true;
	int		c;

#ifdef DEBUG_PIPE

	char buffer[20];

#endif

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	wint_t	ch;
	int		ret;

#endif

	*alt = false;

repeat:

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	ret = get_wch(&ch);
	(void) ret;

	c = ch;

#else

	c = getch();

#endif

	if (c == KEY_MOUSE)
	{
		int	ok = getmouse(mevent);

		if (ok != OK)
		{

#ifdef DEBUG_PIPE

			/*
			 * This is almost all problematic error, that can disable mouse
			 * functionality. It is based on ncurses resets (endwin) or unwanted
			 * mouse reconfigurations (mousemask).
			 */
			fprintf(debug_pipe, "Attention: error reading mouse event\n");

#endif

			goto repeat;
		}
	}

	if (c == 27) /* Escape (before ALT chars) */
	{
		if (first_event)
		{
			first_event = false;
			goto repeat;
		}
	}

	*alt = !first_event;

#ifdef DEBUG_PIPE

	debug_eventno += 1;
	if (c == KEY_MOUSE)
	{
		sprintf(buffer, ", bstate: %08x", mevent->bstate);
	}
	else
		buffer[0] = '\0';

	fprintf(debug_pipe, "*** eventno: %d, key: %s%s%s ***\n",
			  debug_eventno,
			  *alt ? "Alt " : "",
			  keyname(c),
			  buffer);
	fflush(debug_pipe);

#endif

	return c;
}

#define VISIBLE_DATA_ROWS		(scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset)
#define MAX_FIRST_ROW			(desc.last_row - desc.title_rows - scrdesc.main_maxy + 1)
#define MAX_CURSOR_ROW			(desc.last_row - desc.first_data_row)
#define CURSOR_ROW_OFFSET		(scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset)

int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		event_keycode = 0;
	int		prev_event_keycode = 0;
	int		next_event_keycode = 0;
	int		command = cmd_Invalid;
	int		translated_command = cmd_Invalid;
	int		translated_command_history = cmd_Invalid;
	long	last_ms = 0;							/* time of last mouse release in ms */
	time_t	last_sec = 0;							/* time of last mouse release in sec */
	int		next_command = cmd_Invalid;
	bool	reuse_event = false;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		footer_cursor_col = 0;
	int		vertical_cursor_column = -1;			/* table columns are counted from one */
	int		last_x_focus = -1;						/* it is used for repeated vertical cursor display */
	int		first_row = 0;
	int		prev_first_row;
	int		first_data_row;
	int		i;
	DataDesc		desc;
	ScrDesc			scrdesc;
	Options			opts;
	int		fixedRows = -1;			/* detect automatically (not yet implemented option) */
	FILE   *fp = NULL;
	bool	detected_format = false;
	bool	no_alternate_screen = false;
	int		fix_rows_offset = 0;

	int		opt;
	int		option_index = 0;
	mmask_t		prev_mousemask = 0;
	bool	quit_if_one_screen = false;
	int		search_direction = SEARCH_FORWARD;
	bool	redirect_mode;
	bool	noatty;					/* true, when cannot to get keys from stdin */
	bool	fresh_found = false;
	int		fresh_found_cursor_col = -1;
	bool	reinit = false;

	bool	ignore_mouse_release = false;		/* after leave menu by press ignore release too */
	bool	no_doupdate = false;				/* when we sure stdstr refresh is useles */
	bool	prev_event_is_mouse_press = false;
	int		prev_mouse_event_y = -1;
	int		prev_mouse_event_x = -1;
	bool	only_for_tables = false;
	bool	raw_output_quit = false;

	struct winsize size;

	static struct option long_options[] =
	{
		/* These options set a flag. */
		{"force-uniborder", no_argument, 0, 5},
		{"help", no_argument, 0, 1},
		{"hlite-search", no_argument, 0, 'g'},
		{"HILITE-SEARCH", no_argument, 0, 'G'},
		{"ignore-case", no_argument, 0, 'i'},
		{"IGNORE-CASE", no_argument, 0, 'I'},
		{"no-bars", no_argument, 0, 8},
		{"no-mouse", no_argument, 0, 2},
		{"no-sound", no_argument, 0, 3},
		{"less-status-bar", no_argument, 0, 4},
		{"no-commandbar", no_argument, 0, 6},
		{"no-topbar", no_argument, 0, 7},
		{"no-cursor", no_argument, 0, 10},
		{"vertical-cursor", no_argument, 0, 15},
		{"tabular-cursor", no_argument, 0, 11},
		{"line-numbers", no_argument, 0, 9},
		{"quit-if-one-screen", no_argument, 0, 'F'},
		{"version", no_argument, 0, 'V'},
		{"bold-labels", no_argument, 0, 12},
		{"bold-cursor", no_argument, 0, 13},
		{"only-for-tables", no_argument, 0, 14},
		{0, 0, 0, 0}
	};

#ifdef COMPILE_MENU

	bool	menu_is_active = false;
	struct ST_MENU		*menu = NULL;
	struct ST_CMDBAR	*cmdbar = NULL;

#endif

	opts.pathname = NULL;
	opts.ignore_case = false;
	opts.ignore_lower_case = false;
	opts.no_sound = false;
	opts.no_mouse = false;
	opts.less_status_bar = false;
	opts.no_highlight_search = false;
	opts.force_uniborder = false;
	opts.force8bit = false;
	opts.no_commandbar = false;
	opts.no_topbar = false;
	opts.theme = 1;
	opts.show_rownum = false;
	opts.no_cursor = false;
	opts.vertical_cursor = false;
	opts.tabular_cursor = false;
	opts.freezed_cols = -1;				/* default will be 1 if screen width will be enough */
	opts.force_ascii_art = false;
	opts.bold_labels = false;
	opts.bold_cursor = false;

	load_config(tilde("~/.pspgconf"), &opts);

#ifdef DEBUG_PIPE

	debug_pipe = fopen(DEBUG_PIPE, "w");
	setlinebuf(debug_pipe);
	fprintf(debug_pipe, "demo application start\n");

#endif

	while ((opt = getopt_long(argc, argv, "abs:c:f:XVFgGiI",
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
				fprintf(stderr, "  -a             force ascii menu border\n");
				fprintf(stderr, "  -b             black-white style\n");
				fprintf(stderr, "  -s N           set color style number (0..%d)\n", MAX_STYLE);
				fprintf(stderr, "  -c N           fix N columns (0..9)\n");
				fprintf(stderr, "  -f file        open file\n");
				fprintf(stderr, "  -X             don't use alternate screen\n");
				fprintf(stderr, "  --bold-labels  row, column labels use bold font\n");
				fprintf(stderr, "  --bold-cursor  cursor use bold font\n");
				fprintf(stderr, "  --help         show this help\n");
				fprintf(stderr, "  --force-uniborder\n");
				fprintf(stderr, "                 replace ascii borders by unicode borders\n");
				fprintf(stderr, "  -F, --quit-if-one-screen\n");
				fprintf(stderr, "                 quit if content is one screen\n");
				fprintf(stderr, "  -g --hlite-search\n");
				fprintf(stderr, "  -G --HILITE-SEARCH\n");
				fprintf(stderr, "                 don't highlight lines for searches\n");
				fprintf(stderr, "  -i --ignore-case\n");
				fprintf(stderr, "                 ignore case in searches that do not contain uppercase\n");
				fprintf(stderr, "  -I --IGNORE-CASE\n");
				fprintf(stderr, "                 ignore case in all searches\n");
				fprintf(stderr, "  --less-status-bar\n");
				fprintf(stderr, "                 status bar like less pager\n");
				fprintf(stderr, "  --line-numbers\n");
				fprintf(stderr, "                 show line number column\n");
				fprintf(stderr, "  --no-mouse     don't use own mouse handling\n");
				fprintf(stderr, "  --no-sound     don't use beep when scroll is not possible\n");
				fprintf(stderr, "  --no-cursor    row cursor will be hidden\n");
				fprintf(stderr, "  --no-commandbar\n");
				fprintf(stderr, "  --no-topbar\n");
				fprintf(stderr, "  --no-bars\n");
				fprintf(stderr, "                 don't show bottom, top bar or both\n");
				fprintf(stderr, "  --tabular-cursor\n");
				fprintf(stderr, "                 cursor is visible only when data has table format\n");
				fprintf(stderr, "  --vertical-cursor\n");
				fprintf(stderr, "                 show vertical column cursor\n");
				fprintf(stderr, "  --only-for-tables\n");
				fprintf(stderr, "                 use std pager when content is not table\n");
				fprintf(stderr, "  -V, --version  show version\n\n");
				fprintf(stderr, "pspg shares lot of key commands with less pager or vi editor.\n");
				exit(0);

			case 'a':
				opts.force_ascii_art = true;
				break;
			case 'I':
				opts.ignore_case = true;
				break;
			case 'i':
				opts.ignore_lower_case = true;
				break;
			case 2:
				opts.no_mouse = true;
				break;
			case 3:
				opts.no_sound = true;
				break;
			case 4:
				opts.less_status_bar = true;
				break;
			case 5:
				opts.force_uniborder = true;
				break;
			case 6:
				opts.no_commandbar = true;
				break;
			case 7:
				opts.no_topbar = true;
				break;
			case 8:
				opts.no_commandbar = true;
				opts.no_topbar = true;
				break;
			case 9:
				opts.show_rownum = true;
				break;
			case 10:
				opts.no_cursor = true;
				break;
			case 11:
				opts.tabular_cursor = true;
				break;
			case 12:
				opts.bold_labels = true;
				break;
			case 13:
				opts.bold_cursor = true;
				break;
			case 14:
				only_for_tables = true;
				break;
			case 15:
				opts.vertical_cursor = true;
				break;

			case 'V':
				fprintf(stdout, "pspg-%s\n", PSPG_VERSION);

#ifdef HAVE_LIBREADLINE

				fprintf(stdout, "with readline (version: 0x%04x)\n", RL_READLINE_VERSION);

#endif

#ifdef COMPILE_MENU

				fprintf(stdout, "with integrated menu\n");

#endif

#ifdef NCURSES_VERSION

				fprintf(stdout, "ncurses version: %s, patch: %ld\n",
							NCURSES_VERSION,
							(long) NCURSES_VERSION_PATCH);

#endif

#ifdef HAVE_NCURSESW

				fprintf(stdout, "ncurses with wide char support\n");

#endif

#ifdef NCURSES_WIDECHAR

				fprintf(stdout, "ncurses widechar num: %d\n", NCURSES_WIDECHAR);

#endif

				exit(0);
			case 'X':
				no_alternate_screen = true;
				break;
			case 'b':
				opts.theme = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > MAX_STYLE)
				{
					fprintf(stderr, "only color schemas 0 .. %d are supported\n", MAX_STYLE);
					exit(EXIT_FAILURE);
				}
				opts.theme = n;
				break;
			case 'c':
				n = atoi(optarg);
				if (n < 0 || n > 9)
				{
					fprintf(stderr, "fixed columns should be between 0 and 4\n");
					exit(EXIT_FAILURE);
				}
				opts.freezed_cols = n;
				break;
			case 'f':
				fp = fopen(optarg, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s\n", optarg);
					exit(1);
				}
				opts.pathname = strdup(optarg);
				break;
			case 'F':
				quit_if_one_screen = true;
				break;
			case 'g':
				opts.no_highlight_lines = true;
				break;
			case 'G':
				opts.no_highlight_search = true;
				break;
			default:
				fprintf(stderr, "Try %s --help\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (opts.less_status_bar)
		opts.no_topbar = true;

	setlocale(LC_ALL, "");

	/* Don't use UTF when terminal doesn't use UTF */
	opts.force8bit = strcmp(nl_langinfo(CODESET), "UTF-8") != 0;

	readfile(fp, &opts, &desc);
	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
	}

	if (desc.headline != NULL)
		detected_format = translate_headline(&opts, &desc);

	if (!detected_format && only_for_tables)
	{
		const char *pagerprog;
		FILE	   *fout = NULL;
		LineBuffer *lnb = &desc.rows;
		int			lnb_row = 0;

		pagerprog = getenv("PSPG_TEXT_PAGER");
		if (!pagerprog)
			pagerprog = getenv("PAGER");
		if (!pagerprog)
			pagerprog = "more";
		else
		{
			/* if PAGER is empty or all-white-space, don't use pager */
			if (strspn(pagerprog, " \t\r\n") == strlen(pagerprog))
				fout = stdout;
		}

		if (!fout)
		{
			fout = popen(pagerprog, "w");
			if (!fout)
			{
				/* if popen fails, silently proceed without pager */
				fout = stdout;
			}
		}

		if (fout != stdout)
		{
			signal(SIGPIPE, SIG_IGN);
			signal(SIGINT, SIG_IGN);
		}

		while (lnb_row < lnb->nrows)
		{
			int r;
			r = fprintf(fout, "%s\n", lnb->rows[lnb_row++]);
			if (r < 0)
				break;
		}

		if (fout != stdout)
			pclose(fout);

		return 0;
	}

	if (!isatty(fileno(stdin)))
	{
		if (freopen("/dev/tty", "r", stdin) != NULL)
			noatty = false;
		else if (freopen(ttyname(fileno(stdout)), "r", stdin) != NULL)
			noatty = false;
		else
		{
			/*
			 * cannot to reopen terminal device. See discussion to issue #35
			 * fallback solution - read keys directly from stderr. Just check
			 * it it is possible.
			 */
			if (!isatty(fileno(stderr)))
			{
				fprintf(stderr, "missing a access to terminal device\n");
				exit(1);
			}
			noatty = true;
			fclose(stdin);
		}
	}
	else
		noatty = false;

	if (noatty)
		/* use stderr like stdin. This is fallback solution used by less */
		newterm(termname(), stdout, stderr);
	else
		initscr();

	if(!has_colors())
		leave_ncurses("your terminal does not support color");

	start_color();

reinit_theme:

	initialize_color_pairs(opts.theme, opts.bold_labels, opts.bold_cursor);

	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

#ifdef NCURSES_EXT_FUNCS

	set_escdelay(25);

#endif

	initialize_special_keycodes();


	if (!opts.no_mouse)
	{

		mouseinterval(0);


#if NCURSES_MOUSE_VERSION > 1

		mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | BUTTON_ALT, NULL);

#else

		mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED, NULL);

#endif

	}

	if (desc.headline_transl != NULL && !desc.is_expanded_mode)
		desc.first_data_row = desc.border_head_row + 1;
	else if (desc.title_rows > 0 && desc.is_expanded_mode)
		desc.first_data_row = desc.title_rows;
	else
	{
		desc.first_data_row = 0;
		desc.last_data_row = desc.last_row;
		desc.title_rows = 0;
		desc.title[0] = '\0';
	}

	first_data_row = desc.first_data_row;

	trim_footer_rows(&opts, &desc);

	memset(&scrdesc, 0, sizeof(ScrDesc));

	initialize_theme(opts.theme, WINDOW_TOP_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_TOP_BAR]);
	initialize_theme(opts.theme, WINDOW_BOTTOM_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_BOTTOM_BAR]);

	/*
	 * The issue #75 - COLUMNS, LINES are not correctly initialized.
	 * Get real terminal size, and refresh ncurses data.
	 */
	if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
		resize_term(size.ws_row, size.ws_col);

	clear();

	refresh_aux_windows(&opts, &scrdesc, &desc);

	getmaxyx(stdscr, maxy, maxx);

	if (quit_if_one_screen)
	{
		/* the content can be displayed in one screen */
		if (maxy >= desc.last_row && maxx >= desc.maxx)
		{
			LineBuffer *lnb = &desc.rows;
			int			lnb_row = 0;

			endwin();

			while (lnb_row < lnb->nrows)
				printf("%s\n", lnb->rows[lnb_row++]);

			return 0;
		}
	}

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
					str += opts.force8bit ? 1 : utf8charlen(*str);
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

				trim_footer_rows(&opts, &desc);
			}
		}
	}

	if (opts.tabular_cursor && !opts.no_cursor)
		opts.no_cursor = desc.headline_transl == NULL;

	/* run this part only once, don't repeat it when theme is reinitialized */
	if (opts.vertical_cursor && vertical_cursor_column == -1)
	{
		int freezed_cols = opts.freezed_cols != -1 ?  opts.freezed_cols : 1;

		/* The position of vertical cursor should be set */
		if (freezed_cols + 1 <= desc.columns)
			vertical_cursor_column = freezed_cols + 1;
		else
			vertical_cursor_column = 1;

		/* in this moment, there are not any vertical offset, calculation is simple */
		last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
	}

	initialize_theme(opts.theme, WINDOW_ROWNUM_LUC, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWNUM_LUC]);
	initialize_theme(opts.theme, WINDOW_ROWNUM, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWNUM]);

	create_layout_dimensions(&opts, &scrdesc, &desc, opts.freezed_cols, fixedRows, maxy, maxx);
	create_layout(&opts, &scrdesc, &desc, first_data_row, first_row);

	initialize_theme(opts.theme, WINDOW_LUC, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_LUC]);
	initialize_theme(opts.theme, WINDOW_FIX_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_ROWS]);
	initialize_theme(opts.theme, WINDOW_FIX_COLS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_COLS]);
	initialize_theme(opts.theme, WINDOW_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWS]);
	initialize_theme(opts.theme, WINDOW_FOOTER, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FOOTER]);

	print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, 0);

	/* initialize readline if it is active */
#ifdef HAVE_LIBREADLINE

	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;

#if RL_READLINE_VERSION > 0x0603

	rl_change_environment = 0;

#endif

	rl_inhibit_completion = 1;

#ifdef HAVE_READLINE_HISTORY

	if (!reinit)
		read_history(tilde("~/.pspg_history"));

#endif

#endif

#ifdef COMPILE_MENU

	init_menu_config(&opts);
	if (!opts.less_status_bar && !opts.no_commandbar)
		cmdbar = init_cmdbar(cmdbar);

#endif


	while (true)
	{
		bool		refresh_scr = false;
		bool		resize_scr = false;
		bool		after_freeze_signal = false;
		bool		recheck_vertical_cursor_visibility = false;

		fix_rows_offset = desc.fixed_rows - scrdesc.fix_rows_rows;

		/*
		 * Next code allows to inject event, and later process original event again.
		 * It is used for reuse mouse event: 1. replace top bar by menubar, 2. activate
		 * field on menubar - possibly pulldown menu. Following code holds event one
		 * iteration.
		 */
		if (reuse_event)
		{
			/* unfortunately, gcc raises false warning here -Wmaybe-uninitialized */
			if (prev_event_keycode == 0)
			{
				prev_event_keycode = event_keycode;
			}
			else
			{
				next_event_keycode = prev_event_keycode;
				reuse_event = false;
				prev_event_keycode = 0;
			}
		}



		/*
		 * Draw windows, only when function (key) redirect was not forced.
		 * Redirect emmit immediate redraw.
		 */
		if (next_command == cmd_Invalid)
		{
			if (!no_doupdate)
			{
				int		vcursor_xmin_fix = -1;
				int		vcursor_xmax_fix = -1;
				int		vcursor_xmin_data = -1;
				int		vcursor_xmax_data = -1;

				if (opts.vertical_cursor)
				{
					int		vcursor_xmin = desc.cranges[vertical_cursor_column - 1].xmin;
					int		vcursor_xmax = desc.cranges[vertical_cursor_column - 1].xmax;

					if (vcursor_xmin < scrdesc.fix_cols_cols)
					{
						vcursor_xmin_fix = vcursor_xmin;
						vcursor_xmin_data = vcursor_xmin - scrdesc.fix_cols_cols;
					}
					else
					{
						vcursor_xmin_fix = vcursor_xmin - cursor_col;
						vcursor_xmin_data = vcursor_xmin - scrdesc.fix_cols_cols - cursor_col;
					}

					if (vcursor_xmax < scrdesc.fix_cols_cols)
					{
						vcursor_xmax_fix = vcursor_xmax;
						vcursor_xmax_data = vcursor_xmax - scrdesc.fix_cols_cols;
					}
					else
					{
						vcursor_xmax_fix = vcursor_xmax - cursor_col;
						vcursor_xmax_data = vcursor_xmax - scrdesc.fix_cols_cols - cursor_col;
					}
				}

				window_fill(WINDOW_LUC,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							0,
							-1,
							vcursor_xmin_fix, vcursor_xmax_fix,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWS,
							first_data_row + first_row - fix_rows_offset,
							scrdesc.fix_cols_cols + cursor_col,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_data, vcursor_xmax_data,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_COLS,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_fix, vcursor_xmax_fix,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_ROWS,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							scrdesc.fix_cols_cols + cursor_col,
							-1,
							vcursor_xmin_data, vcursor_xmax_data,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FOOTER,
							first_data_row + first_row + scrdesc.rows_rows - fix_rows_offset,
							footer_cursor_col,
							cursor_row - first_row - scrdesc.rows_rows + fix_rows_offset,
							-1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM_LUC,
							0,
							0,
							0,
							-1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							-1, -1,
							&desc, &scrdesc, &opts);

				if (w_luc(&scrdesc))
					wnoutrefresh(w_luc(&scrdesc));
				if (w_rows(&scrdesc))
					wnoutrefresh(w_rows(&scrdesc));
				if (w_fix_cols(&scrdesc))
					wnoutrefresh(w_fix_cols(&scrdesc));
				if (w_fix_rows(&scrdesc))
					wnoutrefresh(w_fix_rows(&scrdesc));
				if (w_footer(&scrdesc))
					wnoutrefresh(w_footer(&scrdesc));
				if (w_rownum(&scrdesc))
					wnoutrefresh(w_rownum(&scrdesc));
				if (w_rownum_luc(&scrdesc))
					wnoutrefresh(w_rownum_luc(&scrdesc));
			} /* !no_doupdate */

#ifdef COMPILE_MENU

			if (cmdbar)
				st_cmdbar_post(cmdbar);

			if (menu != NULL && menu_is_active)
			{
				st_menu_post(menu);
				st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
			}

#endif

			if (no_doupdate)
				no_doupdate = false;
			else if (next_command == 0 || scrdesc.fmt != NULL)
				doupdate();

			if (scrdesc.fmt != NULL)
			{
				next_event_keycode = show_info_wait(&opts, &scrdesc,
									scrdesc.fmt, scrdesc.par, scrdesc.beep,
									false, scrdesc.applytimeout);
				if (scrdesc.fmt != NULL)
				{
					free(scrdesc.fmt);
					scrdesc.fmt = NULL;
				}
				if (scrdesc.par != NULL)
				{
					free(scrdesc.par);
					scrdesc.par = NULL;
				}

				refresh_aux_windows(&opts, &scrdesc, &desc);
				continue;
			}

			if (next_event_keycode != 0)
			{
				event_keycode = next_event_keycode;
				next_event_keycode = 0;
			}
			else
			{
				/*
				 * Store previous event, if this event is mouse press. With it we
				 * can join following mouse release together, and reduce useles
				 * refresh.
				 */
				if (event_keycode == KEY_MOUSE && event.bstate == BUTTON1_PRESSED)
				{
					prev_event_is_mouse_press = true;
					prev_mouse_event_y = event.y;
					prev_mouse_event_x = event.x;
				}
				else
					prev_event_is_mouse_press = false;

				event_keycode = get_event(&event, &press_alt);

				/* the comment for ignore_mouse_release follow */
				if (ignore_mouse_release)
				{
					ignore_mouse_release = false;
					if (event_keycode == KEY_MOUSE && event.bstate & event.bstate & BUTTON1_RELEASED)
					{
						no_doupdate = true;
						continue;
					}
				}
			}

			redirect_mode = false;
		}
		else
		{
			command = next_command;
			next_command = cmd_Invalid;
			redirect_mode = true;
			no_doupdate = false;
		}

		/* Exit immediately on F10 or input error */
		if ((event_keycode == ERR || event_keycode == KEY_F(10)) && !redirect_mode)
			break;


#ifndef COMPILE_MENU

		if (!redirect_mode)
		{
			translated_command_history = translated_command;
			command = translate_event(event_keycode, press_alt);
			translated_command = command;
		}

#else

		/*
		 * Don't send RESIZE to menu. It cannot to handle this event, and it
		 * cannot to translate this event. This event is lost in menu.
		 * So, don't do it.
		 */
		if (!redirect_mode && event_keycode != KEY_RESIZE)
		{
			bool	processed = false;
			bool	activated = false;
			ST_MENU_ITEM		*ami;
			ST_CMDBAR_ITEM		*aci;

			processed = st_menu_driver(menu, event_keycode, press_alt, &event);
			if (processed)
			{
				ami = st_menu_selected_item(&activated);
				if (activated)
				{
					next_command = ami->code;
					goto hide_menu;
				}

				aci = st_menu_selected_command(&activated);
				if (activated)
				{
					next_command = aci->code;
					goto refresh;
				}
			}

			if (menu_is_active && !processed &&
					(event_keycode == ST_MENU_ESCAPE || event_keycode == KEY_MOUSE))
			{
hide_menu:
				st_menu_unpost(menu, true);
				menu_is_active = false;
				st_menu_set_focus(menu, ST_MENU_FOCUS_NONE);

				/*
				 * When we leave menu due mouse action, and this mouse action
				 * is button1 press, then we would to ignore button1 release.
				 * The behave is consistent for this mouse click (press, release).
				 */
				if (event_keycode == KEY_MOUSE && event.bstate & event.bstate & BUTTON1_PRESSED)
					ignore_mouse_release = true;

				goto refresh;
			}

			if (!processed)
			{
				translated_command_history = translated_command;
				command = translate_event(event_keycode, press_alt);
				translated_command = command;
			}
			else
				continue;
		}
		else
		{
			if (!redirect_mode)
			{
				translated_command_history = translated_command;
				command = translate_event(event_keycode, press_alt);
				translated_command = command;
			}
		}

#endif

		prev_first_row = first_row;

#ifdef DEBUG_PIPE

		fprintf(debug_pipe, "main switch: %s\n", cmd_string(command));

#endif

		if (command == cmd_Quit)
			break;
		else if (command == cmd_Invalid)
			continue;
		else if (command == cmd_RawOutputQuit)
		{
			raw_output_quit = true;
			break;
		}

		switch (command)
		{

#ifdef COMPILE_MENU

			case cmd_ShowMenu:
				{
					if (menu == NULL || reinit)
					{
						st_menu_set_desktop_window(stdscr);
						init_menu_config(&opts);
						menu = init_menu(menu);
					}

					st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
					post_menu(&opts, menu);

					menu_is_active = true;
					continue;
				}

#endif

			case cmd_NoHighlight:
				opts.no_highlight_search = true;
				opts.no_highlight_lines = false;
				goto reset_search;

			case cmd_HighlightValues:
				opts.no_highlight_search = false;
				opts.no_highlight_lines = true;
				goto reset_search;

			case cmd_HighlightLines:
				opts.no_highlight_search = false;
				opts.no_highlight_lines = false;
				goto reset_search;

			case cmd_CISearchSet:
				opts.ignore_lower_case = false;
				opts.ignore_case = true;
				goto reset_search;

			case cmd_USSearchSet:
				opts.ignore_lower_case = true;
				opts.ignore_case = false;
				goto reset_search;

			case cmd_CSSearchSet:
				opts.ignore_lower_case = false;
				opts.ignore_case = false;

reset_search:

				scrdesc.searchterm[0] = '\0';
				scrdesc.searchterm_size = 0;
				scrdesc.searchterm_char_size = 0;

				reset_searching_lineinfo(&desc.rows);
				break;

			case cmd_ShowTopBar:
				opts.no_topbar = !opts.no_topbar;
				refresh_scr = true;
				break;

			case cmd_ShowBottomBar:
				opts.no_commandbar = !opts.no_commandbar;
				if (opts.no_commandbar)
				{
					if (cmdbar)
					{
						st_cmdbar_unpost(cmdbar);
						st_cmdbar_free(cmdbar);
						cmdbar = NULL;
					}
				}
				else
					if (!opts.less_status_bar)
					cmdbar = init_cmdbar(cmdbar);

				refresh_scr = true;
				break;

			case cmd_RowNumToggle:
				opts.show_rownum = !opts.show_rownum;
				refresh_scr = true;
				break;

			case cmd_UtfArtToggle:
				opts.force_uniborder = !opts.force_uniborder;
				refresh_scr = true;
				break;

			case cmd_MenuAsciiArtToggle:
				opts.force_ascii_art = !opts.force_ascii_art;
				reinit = true;
				goto reinit_theme;

			case cmd_SoundToggle:
				opts.no_sound = !opts.no_sound;
				break;

			case cmd_SaveSetup:
				if (!save_config(tilde("~/.pspgconf"), &opts))
				{
					if (errno != 0)
						show_info_wait(&opts, &scrdesc, " Cannot write to ~/.pspgconf (%s)", strerror(errno), true, true, false);
					else
						show_info_wait(&opts, &scrdesc, " Cannot write to ~/.pspgconf", NULL, true, true, false);
				}
				else
					show_info_wait(&opts, &scrdesc, " Setup saved to ~/.pspgconf", NULL, true, true, true);
				break;

			case cmd_SetTheme_MidnightBlack:
			case cmd_SetTheme_Midnight:
			case cmd_SetTheme_Foxpro:
			case cmd_SetTheme_Pdmenu:
			case cmd_SetTheme_White:
			case cmd_SetTheme_Mutt:
			case cmd_SetTheme_Pcfand:
			case cmd_SetTheme_Green:
			case cmd_SetTheme_Blue:
			case cmd_SetTheme_WP:
			case cmd_SetTheme_Lowcontrast:
			case cmd_SetTheme_Darkcyan:
			case cmd_SetTheme_Paradox:
			case cmd_SetTheme_DBase:
			case cmd_SetTheme_DBasemagenta:
			case cmd_SetTheme_Red:
			case cmd_SetTheme_Simple:
			case cmd_SetTheme_SolarDark:
			case cmd_SetTheme_SolarLight:
			case cmd_SetTheme_GruvboxLight:
			case cmd_SetTheme_TaoLight:
				opts.theme = cmd_get_theme(command);
				reinit = true;
				goto reinit_theme;

			case cmd_BoldLabelsToggle:
				opts.bold_labels = !opts.bold_labels;
				reinit = true;
				goto reinit_theme;

			case cmd_BoldCursorToggle:
				opts.bold_cursor = !opts.bold_cursor;
				reinit = true;
				goto reinit_theme;

			case cmd_MouseToggle:
				{
					if (!opts.no_mouse)
					{
						mousemask(0, &prev_mousemask);
						opts.no_mouse = true;
					}
					else
					{
						mousemask(prev_mousemask, NULL);
						opts.no_mouse= false;
					}

					show_info_wait(&opts, &scrdesc, " mouse handling: %s ", opts.no_mouse ? "off" : "on", false, true, true);
					break;
				}

			case cmd_ShowCursor:
				opts.no_cursor = !opts.no_cursor;
				refresh_scr = true;
				break;

			case cmd_ShowVerticalCursor:
				{
					opts.vertical_cursor = !opts.vertical_cursor;

					if (opts.vertical_cursor && !is_footer_cursor(cursor_row, &scrdesc, &desc))
					{
						int		i;
						int		xpoint;
						int		prev_command = translated_command_history;

						if (scrdesc.found && (
								prev_command == cmd_SearchPrev || prev_command == cmd_SearchNext ||
								prev_command == cmd_ForwardSearch || prev_command == cmd_BackwardSearch))
						{
							/*
							 * When immediately previous command was some search command, try to
							 * set vertical cursor by searching result.
							 */
							for (i = 0; i < desc.columns; i++)
							{
								if (desc.cranges[i].xmin <= scrdesc.found_start_x &&
										scrdesc.found_start_x < desc.cranges[i].xmax)
								{
									vertical_cursor_column = i + 1;
									last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
									break;
								}
							}
						}

						if (last_x_focus == -1)
						{
							/* try to find first visible columns after fixed columns */
							last_x_focus = scrdesc.fix_cols_cols;
						}

						if (last_x_focus >= scrdesc.fix_cols_cols - 1)
							xpoint = last_x_focus + cursor_col;
						else
							xpoint = last_x_focus;

						for (i = 0; i  < desc.columns; i++)
						{
							if (desc.cranges[i].xmin <= xpoint && desc.cranges[i].xmax > xpoint)
							{
								vertical_cursor_column = i + 1;

								if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : 1))
								{
									if (desc.cranges[i].xmax > scrdesc.main_maxx + cursor_col)
									{
										cursor_col = desc.cranges[i].xmax - scrdesc.main_maxx;
									}
									else if (desc.cranges[i].xmin < scrdesc.fix_cols_cols + cursor_col)
									{
										cursor_col = desc.cranges[i].xmin - scrdesc.fix_cols_cols + 1;
									}
								}

								break;
							}
						}
					}

					refresh_scr = true;
				}
				break;

			case cmd_FlushBookmarks:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;

					while (lnb != NULL)
					{
						if (lnb->lineinfo != NULL)
						{
							rownum_cursor_row = 0;

							while (rownum_cursor_row < lnb->nrows)
							{
								if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
									lnb->lineinfo[rownum_cursor_row].mask ^= LINEINFO_BOOKMARK;

								rownum_cursor_row += 1;
							}
						}

						lnb = lnb->next;
					}
				}
				break;

			case cmd_ToggleBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int			_cursor_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset;

					/* skip first x LineBuffers */
					while (_cursor_row > 1000)
					{
						lnb = lnb->next;
						_cursor_row -= 1000;
					}

					if (lnb->lineinfo == NULL)
					{
						lnb->lineinfo = malloc(1000 * sizeof(LineInfo));
						if (lnb->lineinfo == NULL)
							leave_ncurses("out of memory");

						memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));
					}

					lnb->lineinfo[_cursor_row].mask ^= LINEINFO_BOOKMARK;
				}
				break;

			case cmd_PrevBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;
					int		rownum = 0;
					bool	found = false;

					/* start from previous line before cursor */
					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET - 1;

					if (rownum_cursor_row >= 0)
					{
						/* skip first x LineBuffers */
						while (rownum_cursor_row >= 1000 && lnb != NULL)
						{
							lnb = lnb->next;
							rownum_cursor_row -= 1000;
							rownum += 1000;
						}

						rownum += rownum_cursor_row;

						while (lnb != NULL)
						{
							if (lnb->lineinfo != NULL)
							{
								if (rownum_cursor_row < 0)
									rownum_cursor_row = lnb->nrows - 1;

								while (rownum_cursor_row >= 0)
								{
									if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
									{
										found = true;
										goto exit_search_prev_bookmark;
									}
									rownum -= 1;
									rownum_cursor_row -= 1;
								}
							}
							else
								rownum -= 1000;

							lnb = lnb->prev;
						}
					}

exit_search_prev_bookmark:

					if (found)
					{
						cursor_row = rownum - CURSOR_ROW_OFFSET;
						if (cursor_row < first_row)
							first_row = cursor_row;
						}
					else
						make_beep(&opts);
				}
				break;

			case cmd_NextBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;
					int		rownum = 0;
					bool	found = false;

					/* start after (next line) cursor line */
					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET + 1;

					/* skip first x LineBuffers */
					while (rownum_cursor_row >= 1000 && lnb != NULL)
					{
						lnb = lnb->next;
						rownum_cursor_row -= 1000;
						rownum += 1000;
					}

					rownum += rownum_cursor_row;

					while (lnb != NULL)
					{
						if (lnb->lineinfo != NULL)
						{
							while (rownum_cursor_row < lnb->nrows)
							{
								if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
								{
									found = true;
									goto exit_search_next_bookmark;
								}
								rownum += 1;
								rownum_cursor_row += 1;
							}
						}
						else
							rownum += 1000;

						rownum_cursor_row = 0;
						lnb = lnb->next;
					}

exit_search_next_bookmark:

					if (found)
					{
						int		max_first_row;

						cursor_row = rownum - CURSOR_ROW_OFFSET;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

						max_first_row = MAX_FIRST_ROW;
						if (max_first_row < 0)
							max_first_row = 0;
						if (first_row > max_first_row)
							first_row = max_first_row;
					}
					else
						make_beep(&opts);
				}
				break;

			case cmd_ReleaseCols:
				opts.freezed_cols = 0;

show_first_col:

				if (after_freeze_signal && opts.vertical_cursor &&
						vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : 1))
					recheck_vertical_cursor_visibility = true;
				else
					cursor_col = 0;

				refresh_scr = true;
				break;

			case cmd_FreezeOneCol:
				opts.freezed_cols = 1;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeTwoCols:
				opts.freezed_cols = 2;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeThreeCols:
				opts.freezed_cols = 3;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeFourCols:
				opts.freezed_cols = 4;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeFiveCols:
				opts.freezed_cols = 5;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeSixCols:
				opts.freezed_cols = 6;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeSevenCols:
				opts.freezed_cols = 7;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeEightCols:
				opts.freezed_cols = 8;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeNineCols:
				opts.freezed_cols = 9;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_CursorFirstRow:
				cursor_row = 0;
				first_row = 0;
				break;

			case cmd_CursorLastRow:
				cursor_row = MAX_CURSOR_ROW;
				first_row = MAX_FIRST_ROW;
				if (first_row < 0)
					first_row = 0;
				break;

			case cmd_CursorUp:
				{
					if (opts.no_cursor)
					{
						next_command = cmd_ScrollUp;
						break;
					}

					if (cursor_row > 0)
					{
						/*
						 * When we are on data position, and we are going up, and a
						 * fixed rows are hidden, then unhide fixed rows first (by
						 * decreasing first_row)
						 */
						if (fix_rows_offset > 0 &&
								!is_footer_cursor(cursor_row, &scrdesc, &desc))
							first_row -= 1;
						else
							cursor_row -= 1;

						/*
						 * When fixed rows are hidden, then gap between first
						 * row and cursor row can be bigger (about fix_rows_offset.
						 */
						if (cursor_row + fix_rows_offset < first_row)
							first_row = cursor_row + fix_rows_offset;
					}
					else
						make_beep(&opts);

					break;
				}

			case cmd_CursorDown:
				{
					int		max_cursor_row;
					int		max_first_row;

					if (opts.no_cursor)
					{
						next_command = cmd_ScrollDown;
						break;
					}

					max_cursor_row = MAX_CURSOR_ROW;

					if (++cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						make_beep(&opts);
					}

					if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
						first_row += 1;

					max_first_row = MAX_FIRST_ROW;

					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case cmd_ScrollDownHalfPage:
				{
					int		offset = ((VISIBLE_DATA_ROWS - 1) >> 1);
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = MAX_FIRST_ROW;
					max_cursor_row = MAX_CURSOR_ROW;

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

			case cmd_ScrollUpHalfPage:
				{
					int		offset = ((VISIBLE_DATA_ROWS - 1) >> 1);

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

			case cmd_ScrollDown:
				{
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = MAX_FIRST_ROW;
					max_cursor_row = MAX_CURSOR_ROW;

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

			case cmd_ScrollUp:
				if (first_row > 0)
				{
					first_row -= 1;
					cursor_row -= 1;
				}
				else if (cursor_row > 0)
					cursor_row -= 1;
				break;

			case cmd_MoveLeft:
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

						if (cursor_col == 0 && scrdesc.footer_rows > 0 &&
							(!opts.vertical_cursor || (opts.vertical_cursor && vertical_cursor_column == 1)))
						{
							_is_footer_cursor = true;
							goto recheck_left;
						}

						if (desc.headline_transl != NULL)
						{
							if (opts.vertical_cursor)
							{
								move_left = 0;

								if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : 1))
								{
									int		left_border = scrdesc.fix_cols_cols + cursor_col - 1;
									int		xmin = desc.cranges[vertical_cursor_column - 1].xmin;

									if (xmin < left_border)
									{
										move_left = left_border - xmin;

										if (move_left > 30)
											move_left = 30;
									}
									else
									{
										if (vertical_cursor_column > 1)
										{
											vertical_cursor_column -= 1;
											last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);

											xmin = desc.cranges[vertical_cursor_column - 1].xmin;

											if (xmin < left_border)
											{
												move_left = left_border - xmin;
												if (move_left > 30)
													move_left = 30;
											}
										}
									}
								}
								else
								{
									if (vertical_cursor_column > 1)
										vertical_cursor_column -= 1;

									cursor_col = 0;
									break;
								}
							}
							else
							{
								int		i;

								for (i = 1; i <= 30; i++)
								{
									int		pos = scrdesc.fix_cols_cols + cursor_col - i - 1;

									if (pos < 0)
										break;

									if (desc.headline_transl[pos] == 'I')
									{
										move_left = i;
										break;
									}
								}
							}
						}

						cursor_col -= move_left;
						if (cursor_col < 3)
							cursor_col = 0;
					}
				}
				break;

			case cmd_MoveRight:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_right:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						int max_footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;

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
							if (opts.vertical_cursor)
							{
								int vmaxx = desc.cranges[vertical_cursor_column - 1].xmax;

								/* move only right when right corner is not visible already */
								if (cursor_col + scrdesc.main_maxx < vmaxx)
								{
									int wx = vmaxx - scrdesc.main_maxx - cursor_col + 1;

									move_right = wx > 30 ? 30 : wx;
								}
								else
								{
									if (vertical_cursor_column < desc.columns)
									{
										vertical_cursor_column += 1;
										last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);

										vmaxx = desc.cranges[vertical_cursor_column - 1].xmax;
										if (cursor_col + scrdesc.main_maxx < vmaxx)
										{
											int wx = vmaxx - scrdesc.main_maxx - cursor_col + 1;

											move_right = wx > 30 ? 30 : wx;
										}
										else
											move_right = 0;
									}
								}
							}
							else
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
						}

						new_cursor_col += move_right;

						if (desc.headline_transl != NULL)
							max_cursor_col = desc.headline_char_size - scrdesc.main_maxx;
						else
							max_cursor_col = desc.maxx - scrdesc.maxx - 1;

						max_cursor_col = max_cursor_col > 0 ? max_cursor_col : 0;

						if (new_cursor_col > max_cursor_col)
							new_cursor_col = max_cursor_col;

						if (new_cursor_col == cursor_col && scrdesc.footer_rows > 0 &&
							(!opts.vertical_cursor || (opts.vertical_cursor && vertical_cursor_column == desc.columns)))
						{
							_is_footer_cursor = true;
							goto recheck_right;
						}
						cursor_col = new_cursor_col;
					}
				}
				break;

			case cmd_CursorFirstRowPage:
				cursor_row = first_row;
				break;

			case cmd_CursorLastRowPage:
				cursor_row = first_row + VISIBLE_DATA_ROWS - 1;
				break;

			case cmd_CursorHalfPage:
				cursor_row = first_row + ((VISIBLE_DATA_ROWS - 1) >> 1);
				break;

			case cmd_PageUp:
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
						else
						make_beep(&opts);
				}
				break;

			case cmd_PageDown:
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

					max_cursor_row = MAX_CURSOR_ROW;
					if (cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						make_beep(&opts);
					}

					if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
						first_row += 1;

					max_first_row = MAX_FIRST_ROW;
					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case cmd_RESIZE_EVENT:
				refresh_scr = true;
				resize_scr = true;
				break;

			case cmd_ShowFirstCol:
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
						if (opts.vertical_cursor)
						{
							vertical_cursor_column = 1;
							last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						}

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

			case cmd_ShowLastCol:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_end:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col < desc.footer_char_size - scrdesc.main_maxx)
							footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;
						else if (scrdesc.rows_rows > 0)
						{
							footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;
							_is_footer_cursor = false;
							goto recheck_end;
						}
					}
					else
					{
						int		new_cursor_col;

						if (opts.vertical_cursor)
						{
							vertical_cursor_column = desc.columns;
							last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						}

						if (desc.headline != NULL)
							new_cursor_col = desc.headline_char_size - scrdesc.main_maxx;
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

			case cmd_GotoLine:
				{
					char	linenotxt[256];

					get_string(&opts, &scrdesc, "line: ", linenotxt, sizeof(linenotxt) - 1);
					if (linenotxt[0] != '\0')
					{
						char   *endptr;
						long lineno;

						errno = 0;
						lineno = strtol(linenotxt, &endptr, 10);

						if (endptr == linenotxt)
							show_info_wait(&opts, &scrdesc, " Cannot convert input string to number", NULL, true, true, false);
						else if (errno != 0)
							show_info_wait(&opts, &scrdesc, " Cannot convert input string to number (%s)", strerror(errno), true, true, false);
						else
						{
							int max_cursor_row;
							int max_first_row;

							cursor_row = lineno - 1;
							if (cursor_row < 0)
								cursor_row = 0;

							max_cursor_row = MAX_CURSOR_ROW;
		  					if (cursor_row > max_cursor_row)
							{
								cursor_row = max_cursor_row;
								make_beep(&opts);
							}

							if (cursor_row < first_row || cursor_row - first_row > VISIBLE_DATA_ROWS)
							{
								max_first_row = MAX_FIRST_ROW;

								if (max_first_row < 0)
									max_first_row = 0;

								first_row = cursor_row - VISIBLE_DATA_ROWS / 2;

								if (first_row > max_first_row)
									first_row = max_first_row;
								if (first_row < 0)
									first_row = 0;
							}
						}
					}
					break;
				}

			case cmd_SaveData:
				{
					char	buffer[MAXPATHLEN + 1024];
					char   *path;
					FILE   *fp;
					bool	ok = false;

					errno = 0;

					get_string(&opts, &scrdesc, "log file: ", buffer, sizeof(buffer) - 1);

					if (buffer[0] != '\0')
					{
						path = tilde(buffer);
						fp = fopen(path, "w");
						if (fp != NULL)
						{
							LineBuffer *lnb = &desc.rows;

							ok = true;

							while (lnb != NULL)
							{
								for (i = 0; i < lnb->nrows; i++)
								{
									/*
									 * Reset errno. Previous openf can dirty it, when file was
									 * created.
									 */
									errno = 0;

									fprintf(fp, "%s\n", lnb->rows[i]);
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
						{
							if (errno != 0)
								snprintf(buffer, sizeof(buffer), "%s (%s)", path, strerror(errno));
							else
								strcpy(buffer, path);

							next_command = show_info_wait(&opts, &scrdesc, " Cannot write to %s", buffer, true, false, false);
						}
					}

					refresh_scr = true;

					break;
				}

			case cmd_ForwardSearch:
				{
					char	locsearchterm[256];

					get_string(&opts, &scrdesc, "/", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')
					{
						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = opts.force8bit ? strlen(scrdesc.searchterm) : utf8len(scrdesc.searchterm);
					}
					else
					{
						scrdesc.searchterm[0] = '\0';
						scrdesc.searchterm_size = 0;
						scrdesc.searchterm_char_size = 0;
					}

					reset_searching_lineinfo(&desc.rows);

					search_direction = SEARCH_FORWARD;

					/* continue to find next: */
				}

			case cmd_SearchNext:
				{
					int		rownum_cursor_row;
					int		rownum = 0;
					int		skip_bytes = 0;
					LineBuffer   *lnb = &desc.rows;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchNext && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchPrev;
						break;
					}

					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET;
					if (scrdesc.found && rownum_cursor_row == scrdesc.found_row)
						skip_bytes = scrdesc.found_start_bytes + scrdesc.searchterm_size;

					scrdesc.found = false;

					/* skip first x LineBuffers */
					while (rownum_cursor_row > 1000 && lnb != NULL)
					{
						lnb = lnb->next;
						rownum_cursor_row -= 1000;
						rownum += 1000;
					}

					rownum += rownum_cursor_row;

					while (lnb != NULL)
					{
						while (rownum_cursor_row < lnb->nrows)
						{
							const char	   *str;

							str = pspg_search(&opts, &scrdesc, lnb->rows[rownum_cursor_row] + skip_bytes);

							if (str != NULL)
							{
								scrdesc.found_start_x = opts.force8bit ? str - lnb->rows[rownum_cursor_row] : utf8len_start_stop(lnb->rows[rownum_cursor_row], str);
								scrdesc.found_start_bytes = str - lnb->rows[rownum_cursor_row];
								scrdesc.found = true;
								goto found_next_pattern;
							}

							rownum += 1;
							rownum_cursor_row += 1;
							skip_bytes = 0;
						}

						rownum_cursor_row = 0;
						lnb = lnb->next;
					}

found_next_pattern:

					if (scrdesc.found)
					{
						int		max_first_row;

						cursor_row = rownum - CURSOR_ROW_OFFSET;
						scrdesc.found_row = rownum;
						fresh_found = true;
						fresh_found_cursor_col = -1;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

						max_first_row = MAX_FIRST_ROW;
						if (max_first_row < 0)
							max_first_row = 0;
						if (first_row > max_first_row)
							first_row = max_first_row;
					}
					else
						show_info_wait(&opts, &scrdesc, " Not found (press any key)", NULL, true, true, false);
					break;
				}

			case cmd_BackwardSearch:
				{
					char	locsearchterm[256];

					get_string(&opts, &scrdesc, "?", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')
					{
						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = utf8len(scrdesc.searchterm);
					}
					else
					{
						scrdesc.searchterm[0] = '\0';
						scrdesc.searchterm_size = 0;
						scrdesc.searchterm_char_size = 0;
					}

					reset_searching_lineinfo(&desc.rows);

					search_direction = SEARCH_BACKWARD;

					/* continue to find next: */
				}

			case cmd_SearchPrev:
				{
					int		rowidx;
					int		search_row;
					LineBuffer   *rows = &desc.rows;
					int		cut_bytes = 0;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchPrev && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchNext;
						break;
					}

					rowidx = cursor_row + scrdesc.fix_rows_rows + desc.title_rows;
					search_row = cursor_row;

					/*
					 * when we can search on found line, the use it,
					 * else try start searching from previous row.
					 */
					if (scrdesc.found && rowidx == scrdesc.found_row && scrdesc.found_start_bytes > 0)
						cut_bytes = scrdesc.found_start_bytes;
					else
					{
						rowidx -= 1;
						search_row -= 1;
					}

					scrdesc.found = false;

					while (rowidx > 1000)
					{
						rows = rows->next;
						rowidx -= 1000;
					}

					while (search_row >= 0)
					{
						const char *str;
						char *row;
						bool	free_row;

						if (rowidx < 0)
						{
							rows = rows->prev;
							rowidx = 999;
							continue;
						}

						if (cut_bytes != 0)
						{
							row = malloc(strlen(rows->rows[rowidx]) + 1);
							if (row == NULL)
								leave_ncurses("out of memory");

							strcpy(row, rows->rows[rowidx]);
							row[cut_bytes] = '\0';
							free_row = true;
						}
						else
						{
							row = rows->rows[rowidx];
							free_row = false;
						}

						str = row;

						/* try to find most right pattern */
						while (str != NULL)
						{
							str = pspg_search(&opts, &scrdesc, str);

							if (str != NULL)
							{
								cursor_row = search_row;
								if (first_row > cursor_row)
									first_row = cursor_row;

								scrdesc.found_start_x = opts.force8bit ? str - row : utf8len_start_stop(row, str);
								scrdesc.found_start_bytes = str - row;
								scrdesc.found_row = cursor_row + CURSOR_ROW_OFFSET;
								scrdesc.found = true;
								fresh_found = true;
								fresh_found_cursor_col = -1;

								str += scrdesc.searchterm_size;
							}
						}

						if (free_row)
							free(row);

						if (scrdesc.found)
							break;

						rowidx -= 1;
						search_row -= 1;
						cut_bytes = 0;
					}

					if (!scrdesc.found)
						show_info_wait(&opts, &scrdesc, " Not found (press any key)", NULL, true, true, false);

					break;
				}

			case cmd_MOUSE_EVENT:
				{

#if NCURSES_MOUSE_VERSION > 1

					if (event.bstate & BUTTON_ALT && event.bstate & BUTTON5_PRESSED)
					{
						next_command = cmd_MoveRight;
						break;
					}

					if (event.bstate & BUTTON_ALT && event.bstate & BUTTON4_PRESSED)
					{
						next_command = cmd_MoveLeft;
						break;
					}

					if (event.bstate & BUTTON5_PRESSED)
					{
						int		max_cursor_row;
						int		max_first_row;
						int		offset = 1;

						max_first_row = MAX_FIRST_ROW;
						if (max_first_row < 0)
							max_first_row = 0;

						if (desc.headline_transl != NULL)
							offset = (scrdesc.main_maxy - scrdesc.fix_rows_rows) / 3;

						if (first_row + offset > max_first_row)
							offset = 1;

						first_row += offset;
						cursor_row += offset;

						max_cursor_row = MAX_CURSOR_ROW;
						if (cursor_row > max_cursor_row)
						{
							cursor_row = max_cursor_row;
							make_beep(&opts);
						}

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
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
						else
							make_beep(&opts);
					}
					else

#endif

					if (event.bstate & BUTTON1_PRESSED || event.bstate & BUTTON1_RELEASED)
					{
						int		max_cursor_row;
						int		max_first_row;
						bool	is_double_click = false;
						long	ms;
						time_t	sec;
						struct timespec spec;

						if (event.y == 0 && scrdesc.top_bar_rows > 0)
						{
							next_command = cmd_ShowMenu;
							reuse_event = true;
							prev_event_keycode = 0;
							break;
						}

						if (event.bstate & BUTTON1_RELEASED)
						{
							clock_gettime(CLOCK_MONOTONIC, &spec);
							ms = roundl(spec.tv_nsec / 1.0e6);
							sec = spec.tv_sec;

							if (last_sec > 0)
							{
								long	td;

								td = (sec - last_sec) * 1000 + ms - last_ms;
								if (td < 250)
									is_double_click = true;
							}

							last_sec = sec;
							last_ms = ms;
						}

						/*
						 * When current event is MOUSE RELEASE, and prev event was MOUSE_PRESS
						 * and it is not double click, and there are same position, then we can
						 * ignore this event.
						 */
						if (prev_event_is_mouse_press && !is_double_click &&
								prev_mouse_event_y == event.y && prev_mouse_event_x == event.x)
						{
							no_doupdate = true;
							continue;
						}

						cursor_row = event.y - scrdesc.fix_rows_rows - scrdesc.top_bar_rows + first_row - fix_rows_offset;
						if (cursor_row < 0)
							cursor_row = 0;

						if (cursor_row + fix_rows_offset < first_row)
							first_row = cursor_row + fix_rows_offset;

						max_cursor_row = MAX_CURSOR_ROW;
						if (cursor_row > max_cursor_row)
							cursor_row = max_cursor_row;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row += 1;

						max_first_row = MAX_FIRST_ROW;
						if (max_first_row < 0)
							max_first_row = 0;
						if (first_row > max_first_row)
							first_row = max_first_row;

						/*
						 * Save last x focused point. It will be used for repeated hide/unhide
						 * vertical cursor.
						 */
						last_x_focus = event.x;

						if (opts.vertical_cursor)
						{
							int		xpoint = event.x - scrdesc.main_start_x;
							int		i;

							if (xpoint > scrdesc.fix_cols_cols - 1)
								xpoint += cursor_col;

							if (xpoint >= 0)
							{
								for (i = 0; i  < desc.columns; i++)
								{
									if (desc.cranges[i].xmin <= xpoint && desc.cranges[i].xmax > xpoint)
									{
										int		xmin = desc.cranges[i].xmin;
										int		xmax = desc.cranges[i].xmax;

										vertical_cursor_column = i + 1;

										if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : 1))
										{
											if (xmax > scrdesc.main_maxx + cursor_col)
											{
												cursor_col = xmax - scrdesc.main_maxx;
											}
											else if (xmin < scrdesc.fix_cols_cols + cursor_col)
											{
												cursor_col = xmin - scrdesc.fix_cols_cols + 1;
											}
										}

										last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
										break;
									}
								}
							}
						}

						if (event.bstate & BUTTON_ALT && is_double_click)
							next_command = cmd_ToggleBookmark;
					}
					break;
				}

		} /* end switch */

		if (fresh_found && scrdesc.found)
		{
			int		maxy, maxx;
			bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);

			UNUSED(maxy);

			if (opts.vertical_cursor && !_is_footer_cursor)
			{
				int		i;

				for (i = 0; i < desc.columns; i++)
				{
					if (desc.cranges[i].xmin <= scrdesc.found_start_x &&
						scrdesc.found_start_x < desc.cranges[i].xmax)
					{
						vertical_cursor_column = i + 1;
						last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						break;
					}
				}
			}

			if (fresh_found && w_fix_cols(&scrdesc) != NULL)
			{
				getmaxyx(w_fix_cols(&scrdesc), maxy, maxx);

				if (scrdesc.found_start_x + scrdesc.searchterm_char_size <= maxx)
					fresh_found = false;
			}

			if (fresh_found && !_is_footer_cursor &&  w_rows(&scrdesc) != NULL)
			{
				getmaxyx(w_rows(&scrdesc), maxy, maxx);

				if (cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						cursor_col + scrdesc.fix_cols_cols + maxx >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (cursor_col + scrdesc.fix_cols_cols > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (cursor_col + scrdesc.fix_cols_cols + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						next_command = cmd_MoveRight;
				}
			}

			if (fresh_found  && _is_footer_cursor && w_footer(&scrdesc) != NULL)
			{
				getmaxyx(w_footer(&scrdesc), maxy, maxx);

				if (footer_cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						footer_cursor_col + maxx >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (footer_cursor_col > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (footer_cursor_col + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						next_command = cmd_MoveRight;
				}
			}

			if (next_command != 0)
			{
				/* protect agains infinity loop */
				if (fresh_found_cursor_col != -1)
				{
					/* the direction should not be changed */
					if (_is_footer_cursor)
					{
						if ((fresh_found_cursor_col > footer_cursor_col && next_command == cmd_MoveRight) ||
							(fresh_found_cursor_col < footer_cursor_col && next_command == cmd_MoveLeft) ||
							(fresh_found_cursor_col == footer_cursor_col))
							{
								next_command = cmd_Invalid;
								fresh_found = false;
							}
						}
					else
					{
						if ((fresh_found_cursor_col > cursor_col && next_command == cmd_MoveRight) ||
							(fresh_found_cursor_col < cursor_col && next_command == cmd_MoveLeft) ||
							(fresh_found_cursor_col == cursor_col))
						{
							next_command = cmd_Invalid;
							fresh_found = false;
						}
					}
				}
				else
					fresh_found_cursor_col = _is_footer_cursor? footer_cursor_col : cursor_col;
			}
			else
				fresh_found = false;
		}

		print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset);

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

		if (refresh_scr || scrdesc.refresh_scr)
		{
			if (resize_scr)
			{
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

refresh:

			getmaxyx(stdscr, maxy, maxx);

			refresh_aux_windows(&opts, &scrdesc, &desc);
			create_layout_dimensions(&opts, &scrdesc, &desc, opts.freezed_cols, fixedRows, maxy, maxx);
			create_layout(&opts, &scrdesc, &desc, first_data_row, first_row);

			/* recheck visibility of vertical cursor. now we have fresh fix_cols_cols data */
			if (recheck_vertical_cursor_visibility)
			{
				int		vminx = desc.cranges[vertical_cursor_column - 1].xmin;
				int		left_border = scrdesc.fix_cols_cols + cursor_col - 1;

				if (vminx < left_border)
					cursor_col = vminx -  scrdesc.fix_cols_cols + 1;
			}

			print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset);

			if (cmdbar)
				cmdbar = init_cmdbar(cmdbar);

			refresh_scr = false;
			scrdesc.refresh_scr = false;
		}
	}

	endwin();

	if (raw_output_quit)
	{
		LineBuffer *lnb = &desc.rows;
		int			lnb_row = 0;

		while (lnb_row < lnb->nrows)
			printf("%s\n", lnb->rows[lnb_row++]);
	}
	else if (no_alternate_screen)
	{
		draw_data(&opts, &scrdesc, &desc, first_data_row, first_row, cursor_col,
				  footer_cursor_col, fix_rows_offset);
	}

#ifdef HAVE_READLINE_HISTORY

	write_history(tilde("~/.pspg_history"));

#endif

#ifdef DEBUG_PIPE

	fclose(debug_pipe);

#endif

	return 0;
}
