/*-------------------------------------------------------------------------
 *
 * pspg.c
 *	  a terminal pager designed for usage from psql
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pspg.c
 *
 *-------------------------------------------------------------------------
 */
#include <curses.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <langinfo.h>
#include <libgen.h>
#include <locale.h>
#include <unistd.h>

#include <signal.h>
#include <sys/ioctl.h>

#include <sys/param.h>

#include "pspg.h"
#include "unicode.h"
#include "themes.h"

#define PSPG_VERSION "0.9.3"

/* GNU Hurd does not define MAXPATHLEN */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

int
min_int(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
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
		bool	needle_char_is_upper;

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



static const char *
strcasestr(const char *s1, const char *s2)
{
	/* not implemented yet */
	return strstr(s1, s2);
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
		int	clen;

		if (*src == '\0')
			break;

		clen = opts->force8bit ? 1 : utf8charlen(*src);
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
	else if (!desc->is_expanded_mode && desc->border_head_row != -1 &&
			  desc->headline_transl != NULL)
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
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	desc->fixed_rows = scrdesc->fix_rows_rows;
}

static void
create_layout(ScrDesc *scrdesc, DataDesc *desc, int first_data_row, int first_row)
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
			w_footer(scrdesc) = newwin(scrdesc->footer_rows,
									scrdesc->maxx, scrdesc->main_start_y + scrdesc->fix_rows_rows + scrdesc->rows_rows, 0);
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
		w_footer(scrdesc) = newwin(scrdesc->footer_rows, scrdesc->main_maxx, scrdesc->main_start_y, 0);
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		w_fix_rows(scrdesc) = newwin(scrdesc->fix_rows_rows,
								   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
								   scrdesc->main_start_y, scrdesc->fix_cols_cols);
	}

	if (scrdesc->fix_cols_cols > 0 && scrdesc->rows_rows > 0)
	{
		w_fix_cols(scrdesc) = newwin(scrdesc->rows_rows, scrdesc->fix_cols_cols,
									 scrdesc->fix_rows_rows + scrdesc->main_start_y, 0);
	}

	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		w_luc(scrdesc) = newwin(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols, scrdesc->main_start_y, 0);
	}

	if (scrdesc->rows_rows > 0)
	{
		w_rows(scrdesc) = newwin(scrdesc->rows_rows,
							   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
							   scrdesc->fix_rows_rows + scrdesc->main_start_y, scrdesc->fix_cols_cols);
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
	Theme	   *bottom_bar_theme = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	if (top_bar != NULL)
	{
		delwin(top_bar);
		top_bar = NULL;
		w_top_bar(scrdesc) = NULL;
	}

	if (opts->less_status_bar)
		scrdesc->top_bar_rows = 0;
	else
	{
		scrdesc->top_bar_rows = 1;
		top_bar = newwin(1, 0, 0, 0);
		wbkgd(top_bar, COLOR_PAIR(2));
		wrefresh(top_bar);
		w_top_bar(scrdesc) = top_bar;
	}

	if (bottom_bar != NULL)
	{
		delwin(bottom_bar);
		bottom_bar = NULL;
		w_bottom_bar(scrdesc) = NULL;
	}

	bottom_bar = newwin(1, 0, maxy - 1, 0);
	w_bottom_bar(scrdesc) = bottom_bar;

	if (!opts->less_status_bar > 0)
	{
		wattron(bottom_bar, bottom_bar_theme->bottom_light_attr);
		mvwaddstr(bottom_bar, 0, 1, "Q");
		wattroff(bottom_bar, bottom_bar_theme->bottom_light_attr);
		wattron(bottom_bar, bottom_bar_theme->bottom_attr);
		mvwprintw(bottom_bar, 0, 2, "%-4s", "uit");
		wattroff(bottom_bar, bottom_bar_theme->bottom_attr);
		wrefresh(bottom_bar);

		if (desc->headline_transl != NULL)
		{
			wattron(bottom_bar, bottom_bar_theme->bottom_light_attr);
			mvwaddstr(bottom_bar, 0, 7, "0..4");
			wattroff(bottom_bar, bottom_bar_theme->bottom_light_attr);
			wattron(bottom_bar, bottom_bar_theme->bottom_attr);
			mvwprintw(bottom_bar, 0, 11, "%s", " Col.Freeze ");
			wattroff(bottom_bar, bottom_bar_theme->bottom_attr);
			wrefresh(bottom_bar);
		}
	}

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

		wattron(top_bar, top_bar_theme->title_attr);
		if (desc->title[0] != '\0' && desc->title_rows > 0)
			mvwprintw(top_bar, 0, 0, "%s", desc->title);
		else if (desc->filename[0] != '\0')
			mvwprintw(top_bar, 0, 0, "%s", desc->filename);
		wattroff(top_bar, top_bar_theme->title_attr);

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

		mvwprintw(top_bar, 0, maxx - strlen(buffer), "%s", buffer);
		wrefresh(top_bar);
	}
	else
	{
		/* less-status-bar */
		char	title[65];
		char	*str;
		int		bytes = sizeof(title) - 2;
		char	*ptr = title;

		if (desc->title_rows > 0 && desc->title[0] != '\0')
			str = desc->title;
		else if (desc->filename[0] != '\0')
			str = desc->filename;
		else
			str = "";

		while (bytes > 0 && *str != '\0')
		{
			size_t		sz = opts->force8bit ? strlen(str) : utf8len(str);

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
		wrefresh(bottom_bar);

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
show_info_wait(Options *opts, ScrDesc *scrdesc, char *fmt, char *par, bool beep, bool refresh_first)
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

		return 0;
	}

	wattron(bottom_bar, t->bottom_light_attr);

	if (par != NULL)
		mvwprintw(bottom_bar, 0, 0, fmt, par);
	else
		mvwprintw(bottom_bar, 0, 0, "%s", fmt);

	wclrtoeol(bottom_bar);
	wattroff(bottom_bar,  t->bottom_light_attr);
	wrefresh(bottom_bar);

	refresh();

	if (beep)
		make_beep(opts);

	timeout(2000);
	c = getch();
	timeout(-1);

	return c == ERR ? 0 : c;
}

static void
get_string(ScrDesc *scrdesc, char *prompt, char *buffer, int maxsize)
{
	WINDOW	*bottom_bar = w_bottom_bar(scrdesc);

	mvwprintw(bottom_bar, 0, 0, "%s", prompt);
	wclrtoeol(bottom_bar);
	curs_set(1);
	echo();
	wgetnstr(bottom_bar, buffer, maxsize);
	curs_set(0);
	noecho();
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


int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		c;
	int		c2 = 0;
	int		c3 = 0;
	int		c4 = 0;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		footer_cursor_col = 0;
	int		first_row = 0;
	int		prev_first_row;
	int		first_data_row;
	int		i;
	DataDesc		desc;
	ScrDesc			scrdesc;
	Options			opts;
	int		_columns = -1;			/* default will be 1 if screen width will be enough */
	int		fixedRows = -1;			/* detect automatically (not yet implemented option) */
	FILE   *fp = NULL;
	bool	detected_format = false;
	bool	no_alternate_screen = false;
	int		fix_rows_offset = 0;

	int		opt;
	int		option_index = 0;
	bool	use_mouse = true;
	mmask_t		prev_mousemask = 0;
	bool	quit_if_one_screen = false;
	int		search_direction = SEARCH_FORWARD;
	bool	redirect_mode;
	bool	noatty;					/* true, when cannot to get keys from stdin */
	bool	fresh_found = false;
	int		fresh_found_cursor_col = -1;

	static struct option long_options[] =
	{
		/* These options set a flag. */
		{"force-uniborder", no_argument, 0, 5},
		{"help", no_argument, 0, 1},
		{"hlite-search", no_argument, 0, 'g'},
		{"HILITE-SEARCH", no_argument, 0, 'G'},
		{"ignore-case", no_argument, 0, 'i'},
		{"IGNORE-CASE", no_argument, 0, 'I'},
		{"no-mouse", no_argument, 0, 2},
		{"no-sound", no_argument, 0, 3},
		{"less-status-bar", no_argument, 0, 4},
		{"quit-if-one-screen", no_argument, 0, 'F'},
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0}
	};

	opts.ignore_case = false;
	opts.ignore_lower_case = false;
	opts.no_sound = false;
	opts.less_status_bar = false;
	opts.no_highlight_search = false;
	opts.force_uniborder = false;
	opts.force8bit = false;
	opts.theme = 1;

	while ((opt = getopt_long(argc, argv, "bs:c:f:XVFgGiI",
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
				fprintf(stderr, "  --help         show this help\n");
				fprintf(stderr, "  --force-uniborder\n");
				fprintf(stderr, "                 replace ascii borders by unicode borders\n");
				fprintf(stderr, "  -g --hlite-search\n");
				fprintf(stderr, "  -G --HILITE-SEARCH\n");
				fprintf(stderr, "                 don't highlight lines for searches\n");
				fprintf(stderr, "  -i --ignore-case\n");
				fprintf(stderr, "                 ignore case in searches that do not contain uppercase\n");
				fprintf(stderr, "  -I --IGNORE-CASE\n");
				fprintf(stderr, "                 ignore case in all searches\n");
				fprintf(stderr, "  --less-status-bar\n");
				fprintf(stderr, "                 status bar like less pager\n");
				fprintf(stderr, "  --no-mouse     don't use own mouse handling\n");
				fprintf(stderr, "  --no-sound     don't use beep when scroll is not possible\n");
				fprintf(stderr, "  -F, --quit-if-one-screen\n");
				fprintf(stderr, "                 quit if content is one screen\n");
				fprintf(stderr, "  -V, --version  show version\n\n");
				fprintf(stderr, "pspg shares lot of key commands with less pager or vi editor.\n");
				exit(0);

			case 'I':
				opts.ignore_case = true;
				break;
			case 'i':
				opts.ignore_lower_case = true;
				break;
			case 2:
				use_mouse = false;
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
			case 'V':
				fprintf(stdout, "pspg-%s\n", PSPG_VERSION);
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
					fprintf(stderr, "only color schemas 0 .. %d are supported.\n", MAX_STYLE);
					exit(EXIT_FAILURE);
				}
				opts.theme = n;
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

	setlocale(LC_ALL, "");

	opts.force8bit = strcmp(nl_langinfo(CODESET), "UTF-8") != 0;

	readfile(fp, &opts, &desc);
	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
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
	{
		endwin();
		fprintf(stderr, "your terminal does not support color\n");
		exit(1);
	}

	start_color();
	initialize_color_pairs(opts.theme);

	clear();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	set_escdelay(25);

	if (use_mouse)
	{

#if NCURSES_MOUSE_VERSION > 1

		mousemask(BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED | BUTTON_ALT |
				  BUTTON1_DOUBLE_CLICKED, NULL);

#else

		mousemask(BUTTON1_CLICKED, NULL);

#endif

	}

	if (desc.headline != NULL)
		detected_format = translate_headline(&opts, &desc);

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

	create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
	create_layout(&scrdesc, &desc, first_data_row, first_row);

	initialize_theme(opts.theme, WINDOW_LUC, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_LUC]);
	initialize_theme(opts.theme, WINDOW_FIX_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_ROWS]);
	initialize_theme(opts.theme, WINDOW_FIX_COLS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_COLS]);
	initialize_theme(opts.theme, WINDOW_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWS]);
	initialize_theme(opts.theme, WINDOW_FOOTER, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FOOTER]);

	print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, 0);

	while (true)
	{
		bool		refresh_scr = false;
		bool		resize_scr = false;

		fix_rows_offset = desc.fixed_rows - scrdesc.fix_rows_rows;

		/*
		 * Draw windows, only when function (key) redirect was not forced.
		 * Redirect emmit immediate redraw.
		 */
		if (c2 == 0)
		{
			window_fill(WINDOW_LUC,
						desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
						0,
						-1,
						&desc, &scrdesc, &opts);

			window_fill(WINDOW_ROWS,
						first_data_row + first_row - fix_rows_offset,
						scrdesc.fix_cols_cols + cursor_col,
						cursor_row - first_row + fix_rows_offset,
						&desc, &scrdesc, &opts);

			window_fill(WINDOW_FIX_COLS,
						first_data_row + first_row - fix_rows_offset,
						0,
						cursor_row - first_row + fix_rows_offset,
						&desc, &scrdesc, &opts);

			window_fill(WINDOW_FIX_ROWS,
						desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
						scrdesc.fix_cols_cols + cursor_col,
						-1,
						&desc, &scrdesc, &opts);

			window_fill(WINDOW_FOOTER,
						first_data_row + first_row + scrdesc.rows_rows - fix_rows_offset,
						footer_cursor_col,
						cursor_row - first_row - scrdesc.rows_rows + fix_rows_offset,
						&desc, &scrdesc, &opts);

			if (w_luc(&scrdesc) != NULL)
				wnoutrefresh(w_luc(&scrdesc));
			if (w_rows(&scrdesc) != NULL)
				wnoutrefresh(w_rows(&scrdesc));
			if (w_fix_cols(&scrdesc) != NULL)
				wnoutrefresh(w_fix_cols(&scrdesc));
			if (w_fix_rows(&scrdesc) != NULL)
				wnoutrefresh(w_fix_rows(&scrdesc));
			if (w_footer(&scrdesc) != NULL)
				wnoutrefresh(w_footer(&scrdesc));

			doupdate();

			if (scrdesc.fmt != NULL)
			{
				c4 = show_info_wait(&opts, &scrdesc, scrdesc.fmt, scrdesc.par, scrdesc.beep, false);
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

			if (c4 != 0)
			{
				c = c4;
				c4 = 0;
			}
			else
				c = getch();
			redirect_mode = false;
		}
		else
		{
			c = c2;
			c2 = 0;
			redirect_mode = true;
		}

		if (c == 'q' || c == KEY_F(10) || c == ERR)
			break;

		prev_first_row = first_row;

		switch (c)
		{
			case 27:
				{
					int		second_char;

					if (c3 != 0)
					{
						second_char = c3;
						c3 = 0;
					}
					else
						second_char = getch();

					if (second_char == 'm')		/* ALT m */
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

						c2 = show_info_wait(&opts, &scrdesc, " mouse handling: %s ", use_mouse ? "on" : "off", false, false);
						refresh_scr = true;
					}
					if (second_char == 'k')		/* ALT k - (un)set bookmark */
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
							{
								endwin();
								fprintf(stderr, "out of memory");
								exit(1);
							}
							memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));
						}

						lnb->lineinfo[_cursor_row].mask ^= LINEINFO_BOOKMARK;
					}
					else if (second_char == 'i')		/* ALT i - prev bookmark */
					{
						LineBuffer *lnb = &desc.rows;
						int		rownum_cursor_row;
						int		rownum = 0;
						bool	found = false;

						rownum_cursor_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset - 1;

						if (rownum_cursor_row >= 0)
						{
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
							cursor_row = rownum - scrdesc.fix_rows_rows - desc.title_rows - fix_rows_offset;
							if (cursor_row < first_row)
								first_row = cursor_row;
						}
						else
							make_beep(&opts);

						break;
					}
					else if (second_char == 'j')		/* ALT j - next bookmark */
					{
						LineBuffer *lnb = &desc.rows;
						int		rownum_cursor_row;
						int		rownum = 0;
						bool	found = false;

						rownum_cursor_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset + 1;

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

							cursor_row = rownum - scrdesc.fix_rows_rows - desc.title_rows - fix_rows_offset;

							if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows - fix_rows_offset - 3)
								first_row = cursor_row - maxy + scrdesc.fix_rows_rows + fix_rows_offset + 3;

							max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
							if (max_first_row < 0)
								max_first_row = 0;
							if (first_row > max_first_row)
								first_row = max_first_row;
						}
						else
							make_beep(&opts);

						break;
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
				else
					make_beep(&opts);

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
						make_beep(&opts);
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
			case 520:	/* CTRL DOWN @xterm */
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
			case 561:	/* CTRL UP @xterm */
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
			case 530:		/* @xterm */
			case 'g':
				cursor_row = 0;
				first_row = 0;
				break;

			case 533:		/* CTRL END */
			case 525:		/* @xterm */
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
					else
						make_beep(&opts);
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
						make_beep(&opts);
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
						c2 = show_info_wait(&opts, &scrdesc, " Cannot write to %s ", buffer, true, false);

					refresh_scr = true;

					break;
				}

			case '/':
				{
					char	locsearchterm[256];

					get_string(&scrdesc, "/", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')
					{
						LineBuffer *lnb = &desc.rows;

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm) - 1);
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = opts.force8bit ? strlen(scrdesc.searchterm) : utf8len(scrdesc.searchterm);

						reset_searching_lineinfo(&desc.rows);
					}

					search_direction = SEARCH_FORWARD;

					/* continue to find next: */
				}
			case 'n':
				{
					int		rownum_cursor_row;
					int		rownum = 0;
					int		skip_bytes = 0;
					LineBuffer   *lnb = &desc.rows;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (c == 'n' && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						c2 = 'N';
						break;
					}

					rownum_cursor_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset;
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

							if (opts.ignore_case || (opts.ignore_lower_case && !scrdesc.has_upperchr))
							{
								if (opts.force8bit)
									str = nstrstr(lnb->rows[rownum_cursor_row] + skip_bytes, scrdesc.searchterm);
								else
									str = utf8_nstrstr(lnb->rows[rownum_cursor_row] + skip_bytes, scrdesc.searchterm);
							}
							else if (opts.ignore_lower_case && scrdesc.has_upperchr)
							{
								if (opts.force8bit)
									/* isn't correct */
									str = nstrstr_ignore_lower_case(lnb->rows[rownum_cursor_row] + skip_bytes, scrdesc.searchterm);
								else
									str = utf8_nstrstr_ignore_lower_case(lnb->rows[rownum_cursor_row] + skip_bytes,
																	 scrdesc.searchterm);
							}
							else
								str = strstr(lnb->rows[rownum_cursor_row] + skip_bytes, scrdesc.searchterm);

							if (str != NULL)
							{
								scrdesc.found_start_x = utf8len_start_stop(lnb->rows[rownum_cursor_row], str);
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

						cursor_row = rownum - scrdesc.fix_rows_rows - desc.title_rows - fix_rows_offset;
						scrdesc.found_row = rownum;
						fresh_found = true;
						fresh_found_cursor_col = -1;

						if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows - fix_rows_offset - 3)
							first_row = cursor_row - maxy + scrdesc.fix_rows_rows + fix_rows_offset + 3;

						max_first_row = desc.last_row - desc.title_rows - scrdesc.main_maxy + 1;
						if (max_first_row < 0)
							max_first_row = 0;
						if (first_row > max_first_row)
							first_row = max_first_row;
					}
					else
						c2 = show_info_wait(&opts, &scrdesc, " Not found ", NULL, true, true);

					refresh_scr = true;
				}
				break;

			case '?':
				{
					char	locsearchterm[256];

					get_string(&scrdesc, "?", locsearchterm, sizeof(locsearchterm) - 1);
					if (locsearchterm[0] != '\0')
					{
						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm) - 1);
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = utf8len(scrdesc.searchterm);

						reset_searching_lineinfo(&desc.rows);
					}

					search_direction = SEARCH_BACKWARD;

					/* continue to find next: */
				}
			case 'N':
				{
					int		rowidx;
					int		search_row;
					LineBuffer   *rows = &desc.rows;
					int		cut_bytes = 0;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (c == 'N' && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						c2 = 'n';
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
							rowidx = 1000;
							continue;
						}

						if (cut_bytes != 0)
						{
							row = malloc(strlen(rows->rows[rowidx]) + 1);
							if (row == NULL)
							{
								endwin();
								fprintf(stderr, "out of memory");
								exit(1);
							}
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
							if (opts.ignore_case || (opts.ignore_lower_case && !scrdesc.has_upperchr))
								if (opts.force8bit)
									str = nstrstr(str, scrdesc.searchterm);
								else
									str = utf8_nstrstr(str, scrdesc.searchterm);
							else if (opts.ignore_lower_case && scrdesc.has_upperchr)
								if (opts.force8bit)
									nstrstr_ignore_lower_case(str, scrdesc.searchterm);
								else
									str = utf8_nstrstr_ignore_lower_case(str, scrdesc.searchterm);
							else
								str = str = strstr(str, scrdesc.searchterm);

							if (str != NULL)
							{
								cursor_row = search_row;
								if (first_row > cursor_row)
									first_row = cursor_row;

								scrdesc.found_start_x = utf8len_start_stop(row, str);
								scrdesc.found_start_bytes = str - row;
								scrdesc.found_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset;
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
						c2 = show_info_wait(&opts, &scrdesc, " Not found ", NULL, true, true);

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
								make_beep(&opts);
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
							else
								make_beep(&opts);
						}
						else

#endif

						if (event.bstate & BUTTON1_CLICKED || event.bstate & BUTTON1_DOUBLE_CLICKED)
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

							if (event.bstate & BUTTON_ALT && event.bstate & BUTTON1_DOUBLE_CLICKED)
							{
								c2 = 27;
								c3 = 'k';
							}
						}
					}
				}
				break;
		} /* end switch */

		if (fresh_found && scrdesc.found)
		{
			int		maxy, maxx;
			bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);

			UNUSED(maxy);

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
						c2 = KEY_LEFT;
					else if (cursor_col + scrdesc.fix_cols_cols + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						c2 = KEY_RIGHT;
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
						c2 = KEY_LEFT;
					else if (footer_cursor_col + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						c2 = KEY_RIGHT;
				}
			}

			if (c2 != 0)
			{
				/* protect agains infinity loop */
				if (fresh_found_cursor_col != -1)
				{
					/* the direction should not be changed */
					if (_is_footer_cursor)
					{
						if ((fresh_found_cursor_col > footer_cursor_col && c2 == KEY_RIGHT) ||
							(fresh_found_cursor_col < footer_cursor_col && c2 == KEY_LEFT) ||
							(fresh_found_cursor_col == footer_cursor_col))
							{
								c2 = 0;
								fresh_found = false;
							}
						}
					else
					{
						if ((fresh_found_cursor_col > cursor_col && c2 == KEY_RIGHT) ||
							(fresh_found_cursor_col < cursor_col && c2 == KEY_LEFT) ||
							(fresh_found_cursor_col == cursor_col))
						{
							c2 = 0;
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

			refresh_aux_windows(&opts, &scrdesc, &desc);
			create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
			create_layout(&scrdesc, &desc, first_data_row, first_row);
			print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset);

			refresh_scr = false;
		}
	}

	endwin();

	if (no_alternate_screen)
	{
		draw_data(&opts, &scrdesc, &desc, first_data_row, first_row, cursor_col,
				  footer_cursor_col, fix_rows_offset);
	}

	return 0;
}
