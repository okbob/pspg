/*-------------------------------------------------------------------------
 *
 * print.c
 *	  visualisation loaded data
 *
 * Portions Copyright (c) 2017-2026 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/print.c
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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>

#ifndef GWINSZ_IN_SYS_IOCTL
#include <termios.h>
#endif

#include <unistd.h>

#include "pspg.h"
#include "unicode.h"

#include <ctype.h>
#include <limits.h>

#ifndef A_ITALIC
#define A_ITALIC	A_DIM
#endif

static inline void
wrepeatspace(WINDOW *win, int n)
{
	int		i;

	for (i = 0; i < n; i++)
		waddch(win, ' ');
}

/*
 * Flush data to window. When row is a decoration, then
 * replace ascii decoration by special terminal decoration.
 */
static void
flush_bytes(WINDOW *win,
		    char *rowstr,
		    int bytes,
		    int offsetx,
		    bool is_top_deco,
		    bool is_head_deco,
		    bool is_bottom_deco,
			DataDesc *desc,
			Options *opts)
{
	if ((is_top_deco || is_head_deco || is_bottom_deco) &&
		desc->linestyle == 'a' && opts->force_uniborder)
	{
		while (bytes > 0)
		{
			int		column_format = desc->headline_transl[offsetx];

			if (column_format == 'd' && *rowstr == '-')
			{
				waddch(win, ACS_HLINE);
				rowstr += 1;
				bytes -= 1;
				offsetx += 1;
			}
			else if (column_format == 'L' && (*rowstr == '+' || *rowstr == '|'))
			{
				if (is_head_deco)
					waddch(win, ACS_LTEE);
				else if (is_top_deco)
					waddch(win, ACS_ULCORNER);
				else /* bottom row */
					waddch(win, ACS_LLCORNER);

				rowstr += 1;
				bytes -= 1;
				offsetx += 1;
			}
			else if (column_format == 'I' && *rowstr == '+')
			{
				if (is_head_deco)
					waddch(win, ACS_PLUS);
				else if (is_top_deco)
					waddch(win, ACS_TTEE);
				else /* bottom row */
					waddch(win, ACS_BTEE);

				rowstr += 1;
				bytes -= 1;
				offsetx += 1;
			}
			else if (column_format == 'R' && (*rowstr == '+' || *rowstr == '|'))
			{
				if (is_head_deco)
					waddch(win, ACS_RTEE);
				else if (is_top_deco)
					waddch(win, ACS_URCORNER);
				else /* bottom row */
					waddch(win, ACS_LRCORNER);

				rowstr += 1;
				bytes -= 1;
				offsetx += 1;
			}
			else
			{
				int len = charlen(rowstr);

				waddnstr(win, rowstr, len);
				offsetx += utf_dsplen(rowstr);
				rowstr +=len;
				bytes -= len;
			}
		}
	}
	else
	{
		/*
		 * waddnstr is working with utf8 on ncursesw
		 */
		waddnstr(win, rowstr, bytes);
	}
}

static void
print_column_names(WINDOW *win,
				   int srcx,						/* offset to displayed data */
				   int vcursor_xmin,				/* xmin in display coordinates */
				   int vcursor_xmax,				/* xmax in display coordinates */
				   int selected_xmin,
				   int selected_xmax,
				   DataDesc *desc,
				   Options *opts,
				   Theme *t)
{
	char   *headline_ptr = desc->headline_transl;
	char   *headline_end_ptr = headline_ptr + desc->headline_char_size;
	char   *namesline = desc->namesline;
	char   *ptr = namesline;
	int		maxy, maxx;
	int		cy, cx;
	int		pos = 0;
	int		bytes;
	int		chars;

	attr_t		active_attr = 0;
	attr_t		new_attr;
	int		i;

	getyx(win, cy, cx);
	getmaxyx(win, maxy, maxx);

	(void) cx;
	(void) maxy;

	/* skip left invisible chars */
	while (pos < srcx)
	{
		bytes = charlen(ptr);
		chars = dsplen(ptr);

		if (pos + chars > srcx)
		{
			wrepeatspace(win, pos + chars - srcx);

			pos += chars;
			ptr += bytes;
			headline_ptr += chars;

			break;
		}

		pos += chars;
		ptr += bytes;
		headline_ptr += chars;
	}

	/* position starts from zero again to be comparable with maxx */
	pos -= srcx;

	if (selected_xmin != INT_MIN)
	{
		selected_xmin -= srcx;
		selected_xmax -= srcx;

		if (selected_xmin < 0)
			selected_xmin = 0;
	}

	/* for each visible column (position) and defined colum */
	while (pos < maxx && headline_ptr < headline_end_ptr)
	{
		char	column_format = *headline_ptr;
		bool	is_cursor = vcursor_xmin <= pos && pos <= vcursor_xmax;
		bool	is_in_range = false;

		is_in_range = selected_xmin != INT_MIN && pos != -1 &&
						  pos >= selected_xmin && pos <= selected_xmax;

		bytes = charlen(ptr);
		chars = dsplen(ptr);

		if (is_in_range)
			new_attr = is_cursor ? t->selection_cursor_attr : t->selection_attr;
		else if (is_cursor)
			new_attr = column_format == 'd' ? t->cursor_data_attr : t->cursor_line_attr;
		else
			new_attr = column_format == 'd' ? t->data_attr : t->line_attr;

		if (active_attr != new_attr)
		{
			/* disable current style */
			wattroff(win, active_attr);

			/* active new style */
			active_attr = new_attr;
			wattron(win, active_attr);
		}

		if (column_format != 'd')
		{
			if (desc->linestyle == 'a' && opts->force_uniborder)
				waddch(win, ACS_VLINE);
			else
				waddnstr(win, ptr, bytes);
		}
		else
			/* clean background of colum names */
			wrepeatspace(win, chars);

		headline_ptr += chars;
		ptr += bytes;
		pos += chars;
	}

	wclrtoeol(win);

	wattroff(win, active_attr);

	/* check all column names and print that are visible */
	for (i = 0; i < desc->columns; i++)
	{
		CRange *col = &desc->cranges[i];

		char   *colname;
		int		colname_size;
		int		colname_width;
		int		col_val_xmin;
		int		col_val_xmax;
		int		visible_col_val_xmin;
		int		visible_col_val_xmax;
		int		visible_col_val_width;
		int		border_width;
		int		offset;
		int		startx;
		bool	is_cursor;
		bool	is_in_range;

		if (col->xmax <= srcx)
			continue;

		if (srcx + maxx <= col->xmin)
			continue;

		colname = desc->namesline + col->name_offset;
		colname_size = col->name_size;
		colname_width = col->name_width;

		col_val_xmin = col->xmin;
		if (desc->headline_transl[col_val_xmin] == 'I' ||
			desc->headline_transl[col_val_xmin] == 'L')
		{
			col_val_xmin += 1;
		}

		col_val_xmax = col->xmax;
		if (desc->headline_transl[col_val_xmax] == 'R')
		{
			col_val_xmax -= 1;
		}

		visible_col_val_xmin = col_val_xmin < srcx ? srcx : col_val_xmin;
		visible_col_val_xmax = col_val_xmax > srcx + maxx ? srcx + maxx : col_val_xmax;
		visible_col_val_width = visible_col_val_xmax - visible_col_val_xmin + 1;

		border_width = desc->border_type != 0 ? 1 : 0;

		if ((colname_width + 2 * border_width) <= visible_col_val_width)
		{
			/* When the label can be placed inside visible space in column */
			offset = (visible_col_val_width - colname_width) / 2;
			border_width = 0;
		}
		else
		{
			int		visible_colname_width = visible_col_val_width - border_width;
			int		char_bytes;
			int		char_width;

			if (col_val_xmax < srcx + maxx)
			{
				/* when end of label is visible, skip n chars from begin */
				while (*colname)
				{
					char_bytes = charlen(colname);
					char_width = dsplen(colname);

					if (colname_width < visible_colname_width)
						break;

					colname_width -= char_width;
					colname += char_bytes;
					colname_size -= char_bytes;
				}

				border_width = 0;
			}
			else
			{
				char   *str = colname;

				colname_width = 0;
				colname_size = 0;

				/* only first n chars */
				while (*str)
				{
					char_bytes = charlen(colname);
					char_width = dsplen(colname);

					if (colname_width + char_width > visible_colname_width)
						break;

					colname_width += char_width;
					str += char_bytes;
					colname_size += char_bytes;
				}
			}

			offset = 0;
		}

		startx = visible_col_val_xmin - srcx;

		is_cursor = vcursor_xmin <= startx && startx <= vcursor_xmax;
		is_in_range = selected_xmin != INT_MIN &&
						  startx >= selected_xmin && startx <= selected_xmax;

		if (is_in_range)
			new_attr = is_cursor ? t->selection_cursor_attr : t->selection_attr;
		else
			new_attr = is_cursor ? t->cursor_data_attr : t->data_attr;

		wattron(win, new_attr);

		mvwaddnstr(win, cy, visible_col_val_xmin - srcx + offset + border_width, colname, colname_size);

		wattroff(win, new_attr);
	}
}

LineInfo *
set_line_info(Options *opts,
			  ScrDesc *scrdesc,
			  DataDesc *desc,
			  LineBufferMark *lbm,
			  char *rowstr)
{
	LineInfo   *linfo = NULL;

	if (*scrdesc->searchterm == '\0' || !lbm || !rowstr || !lbm->lb)
		return linfo;

	if (!lbm->lb->lineinfo)
	{
		LineBuffer *lb = lbm->lb;
		int		i;

		lb->lineinfo = smalloc(LINEBUFFER_LINES * sizeof(LineInfo));

		for (i = 0; i < LINEBUFFER_LINES; i++)
			lb->lineinfo[i].mask = LINEINFO_UNKNOWN;
	}

	linfo = &lbm->lb->lineinfo[lbm->lb_rowno];

	if (linfo->mask & LINEINFO_UNKNOWN)
	{
		const char *str = rowstr;

		linfo->mask ^= LINEINFO_UNKNOWN;
		linfo->mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);

		/* apply row selection filtr */
		if (scrdesc->search_rows > 0)
		{
			int		rowno = lbm->lineno - desc->first_data_row;

			if (rowno < scrdesc->search_first_row ||
				rowno > scrdesc->search_first_row + scrdesc->search_rows - 1)
				return linfo;
		}

		while (str != NULL)
		{
			/*
			 * When we would to ignore case or lower case (in this case, we know, so
			 * pattern has not any upper char, then we have to use slower case insensitive
			 * searching.
			 */
			if (opts->ignore_case || (opts->ignore_lower_case && !scrdesc->has_upperchr))
			{

				if (use_utf8)
					str = utf8_nstrstr(str, scrdesc->searchterm);
				else
					str = nstrstr(str, scrdesc->searchterm);
			}
			else if (opts->ignore_lower_case && scrdesc->has_upperchr)
			{
				if (use_utf8)
					str = utf8_nstrstr_ignore_lower_case(str, scrdesc->searchterm);
				else
					str = nstrstr_ignore_lower_case(str, scrdesc->searchterm);
			}
			else
				/* we can use case sensitive searching (binary comparation) */
				str = strstr(str, scrdesc->searchterm);

			if (str != NULL)
			{
				/* apply column selection filtr */
				if (scrdesc->search_columns > 0)
				{
					int		bytes = str - rowstr;
					int		pos;

					pos = use_utf8 ? utf_string_dsplen(rowstr, bytes) : bytes;

					if (pos < scrdesc->search_first_column)
					{
						str += charlen(str);
						continue;
					}

					if (pos > scrdesc->search_first_column + scrdesc->search_columns - 1)
						return linfo;
				}

				if (linfo->mask & LINEINFO_FOUNDSTR)
				{
					/* When we detect multi occurrence, then stop searching */
					linfo->mask |= LINEINFO_FOUNDSTR_MULTI;
					break;
				}
				else
				{
					linfo->mask |= LINEINFO_FOUNDSTR;

					if (use_utf8)
						linfo->start_char = utf_string_dsplen(rowstr, str - rowstr);
					else
						linfo->start_char = str - rowstr;
				}

				str += scrdesc->searchterm_size;
			}
		}
	}

	return linfo;
}

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

/*
 * Two examples how to print wide char correctly
 */
static void
pspg_mvwadd_wchar(WINDOW *win, int y, int x, wchar_t *wchr, attr_t attr)
{
	cchar_t		cchr;

	/*
	 * Note: I misunderstand to the function setcchar originally. I passed there
	 * pointer to wide char, but it really expects pointer to wide char string
	 * ended zero. This issue was detected by address sanitizer
	 */
	setcchar(&cchr, wchr, attr, PAIR_NUMBER(attr), NULL);
	mvwadd_wch(win, y, x, &cchr);
}

static void
mvwadd_wchar(WINDOW *win, int y, int x, wchar_t wchr)
{
	mvwaddnwstr(win, y, x, &wchr, 1);
}

#endif

/*
 * Draw scrollbar to related window.
 */
static void
draw_scrollbar_win(WINDOW *win,
				   Theme *t,
				   ScrDesc *scrdesc,
				   Options *opts)
{
	int		i;

	werase(win);

	wattron(win, t->scrollbar_attr);

	for (i = 0; i < scrdesc->scrollbar_maxy; i++)
		waddch(win, ACS_CKBOARD);

	wattroff(win, t->scrollbar_attr);

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	if (t->scrollbar_use_arrows)
	{
		if (!use_utf8 || opts->force_ascii_art)
		{
			wattron(win, t->scrollbar_arrow_attr);

			mvwaddch(win, 0, 0, ACS_UARROW);
			mvwaddch(win, scrdesc->scrollbar_maxy - 1, 0, ACS_DARROW);

			wattroff(win, t->scrollbar_arrow_attr);
		}
		else
		{
			/* 🠕 🠗 */
			wattron(win, t->scrollbar_arrow_attr);

			mvwadd_wchar(win, 0, 0,  L'\x1F815');
			mvwadd_wchar(win, scrdesc->scrollbar_maxy - 1, 0, L'\x1F817');

			wattroff(win, t->scrollbar_arrow_attr);
		}
	}
	else
	{
		/* ▲ ▼ */
		pspg_mvwadd_wchar(win, 0, 0, L"\x25b2", t->scrollbar_arrow_attr);
		pspg_mvwadd_wchar(win, scrdesc->scrollbar_maxy - 1, 0, L"\x25bc", t->scrollbar_arrow_attr);
	}

#else

	wattron(win, t->scrollbar_arrow_attr);

	mvwaddch(win, 0, 0, ACS_UARROW);
	mvwaddch(win, scrdesc->scrollbar_maxy - 1, 0, ACS_DARROW);

	wattroff(win, t->scrollbar_arrow_attr);

#endif

	wattron(win, scrdesc->scrollbar_mode ? t->scrollbar_active_slider_attr : t->scrollbar_slider_attr);

	if (!t->scrollbar_slider_symbol)
	{
		/* draw slider */
		for (i = 0; i < scrdesc->slider_size; i++)
			mvwaddch(win, scrdesc->slider_min_y + i, 0, ' ');
	}
	else
		mvwaddch(win, scrdesc->slider_min_y, 0, t->scrollbar_slider_symbol);

	wattroff(win, scrdesc->scrollbar_mode ? t->scrollbar_active_slider_attr : t->scrollbar_slider_attr);
}

/*
 * Return true when pos is over some searched patterns specified by
 * positions cache or lineinfo position. This function can be called
 * only when the row is pattern row (linfo is valid).
 */
static bool
is_in_searched_pattern(int pos,
					   ScrDesc *scrdesc,
					   LineInfo *linfo,
					   int positions[100][2],
					   int npositions)
{
	if (linfo->mask & LINEINFO_FOUNDSTR_MULTI)
	{
		int		i;

		for (i = 0; i < npositions; i++)
		{
			if (pos >= positions[i][0] && pos < positions[i][1])
				return true;
		}
	}
	else
	{
		if (pos >= linfo->start_char &&
			 pos < linfo->start_char + scrdesc->searchterm_char_size)
			return true;
	}

	return false;
}

typedef struct
{
	int		start_pos;
	int		end_pos;
	int		typ;
} SpecialWord;

static bool
is_upper_char(char *chr)
{
	if (use_utf8)
		return utf8_isupper(chr);

	return isupper(*chr);
}

static bool
is_ascii_alnum(char chr)
{
	if (use_utf8 && (chr & 0x80))
		return false;

	return isalnum(chr);
}

static bool
is_ascii_alpha(char chr)
{
	if (use_utf8 && (chr & 0x80))
		return false;

	return isalpha(chr);
}

/*
 * Try to identify words in line that should be highlighted
 *
 */
static int
parse_line(char *line, SpecialWord *words, int maxwords)
{
	int		nwords = 0;
	int		pos = 0;
	bool	first_nonspace = true;
	char   *first_char = line;

	/*
	 * When text starts on line start and first char is upper
	 * char, then mark this text until double colon.
	 *
	 * like "Usage:"
	 *
	 * Exceptions: sentences (ending by . or contains \ )
	 */
	if ((is_upper_char(line) || strncmp(line, "psql", 4) == 0) &&
		!strchr(line, '.') && !strchr(line, '\\'))
	{
		char   *aux_line = line;

		words[0].start_pos = 0;
		words[0].typ = 3;

		while (*line != '\0' && *line != ':')
		{
			pos += dsplen(line);
			line += charlen(line);
		}

		if (!is_upper_char(aux_line + 1))
		{
			words[0].end_pos = pos - 1;
			words[0].typ = 3;
			nwords = 1;
			first_nonspace = false;
		}
		else
		{
			line = aux_line;
			pos = 0;
		}
	}

	while (*line)
	{
		while (*line == ' ')
		{
			line += 1;
			pos += 1;
		}

		/* psql's backslash commands */
		if (*line == '\\')
		{
			words[nwords].start_pos = pos;
			words[nwords].typ = 1;

			while (*line != ' ' && *line != '\0')
			{
				pos += dsplen(line);
				line += charlen(line);
			}
		}
		/* psql's shell options */
		else if (*line == '-')
		{
			/* when dash is inside world, then it is not an option */
			if (line > first_char && is_ascii_alnum(line[-1]))
			{
				line += 1;
				pos += 1;

				continue;
			}

			words[nwords].start_pos = pos;
			words[nwords].typ = 1;

			while (*line == '-')
			{
				line += 1;
				pos += 1;
			}

			if (pos - words[nwords].start_pos > 2)
				continue;

			if (!(is_ascii_alnum(*line) || *line == '?' || *line == '!'))
				continue;

			while (is_ascii_alnum(*line) || *line == '-' || *line == '?' || *line == '!')
			{
				line += 1;
				pos += 1;
			}

			words[nwords].typ = 1;
			goto fin;
		}
		/* psql's variables and \pset variables */
		else if (is_ascii_alpha(*line))
		{
			bool	only_upper = true;
			char   *start = line;

			words[nwords].start_pos = pos;

			while (is_ascii_alnum(*line) || *line == '_')
			{
				if (islower(*line))
					only_upper = false;

				line += 1;
				pos += 1;
			}

			if (!only_upper)
			{
				if (first_nonspace && words[nwords].start_pos == 2 && *line == '\0')
				{
					words[nwords].typ = 2;
					goto fin;
				}
				else if (isupper(*start) && *line == ':')
				{
					words[nwords].typ = 3;
					goto fin;
				}

				continue;
			}

			if (strncmp(start, "SQL", pos - words[nwords].start_pos) == 0)
				continue;

			if (*line == ':')
				continue;

			if ((pos - words[nwords].start_pos) == 1)
				continue;

			words[nwords].typ = 2;
		}
		else
		{
			pos += dsplen(line);
			line += charlen(line);
			first_nonspace = false;
			continue;
		}

fin:

		words[nwords].end_pos = pos - 1;

		if (++nwords == maxwords)
			return nwords;
	}

	return nwords;
}

void
window_fill(int window_identifier,
			int srcy,
			int srcx,						/* offset to displayed data */
			int cursor_row,					/* row of row cursor */
			int vcursor_xmin,				/* xmin in display coordinates */
			int vcursor_xmax,				/* xmax in display coordinates */
			int selected_xmin,
			int selected_xmax,
			DataDesc *desc,
			ScrDesc *scrdesc,
			Options *opts)
{
	int			maxy, maxx;
	int			row;
	LineBufferIter lbi;
	LineBufferMark lbm;
	attr_t		active_attr;
	attr_t		pattern_fix;
	int			srcy_bak = srcy;
	char		*free_row;
	WINDOW		*win;
	Theme		*t;
	SpecialWord specwords[30];
	int			nspecwords;

	bool		is_footer = window_identifier == WINDOW_FOOTER;
	bool		is_fix_rows = window_identifier == WINDOW_LUC || window_identifier == WINDOW_FIX_ROWS;
	bool		is_rownum = window_identifier == WINDOW_ROWNUM;
	bool		is_rownum_luc = window_identifier == WINDOW_ROWNUM_LUC;
	bool		is_fix_rows_only = window_identifier == WINDOW_FIX_ROWS;
	bool		is_scrollbar = window_identifier == WINDOW_VSCROLLBAR;
	bool		is_selectable = window_identifier == WINDOW_ROWS ||
								window_identifier == WINDOW_LUC ||
								window_identifier == WINDOW_FIX_COLS ||
								window_identifier == WINDOW_FIX_ROWS ||
								window_identifier == WINDOW_FOOTER;
	bool		is_text = window_identifier == WINDOW_FOOTER && desc->headline_transl == NULL;
	int			odd_theme_identifier = -1;

	win = scrdesc->wins[window_identifier];
	t = &scrdesc->themes[window_identifier];

	pattern_fix = t->found_str_attr & A_UNDERLINE;

	if (has_odd_themedef && opts->highlight_odd_rec)
	{
		if (window_identifier == WINDOW_FIX_COLS)
			odd_theme_identifier = WINDOW_FIX_COLS_ODD;
		else if (window_identifier == WINDOW_ROWS)
			odd_theme_identifier = WINDOW_ROWS_ODD;
		else if (window_identifier == WINDOW_ROWNUM)
			odd_theme_identifier = WINDOW_ROWNUM_ODD;
		else
			odd_theme_identifier = -1;

		if (odd_theme_identifier != -1)
			multilines_detection(desc);
	}

	/* when we want to detect expanded records titles */
	if (desc->is_expanded_mode)
	{
		scrdesc->first_rec_title_y = -1;
		scrdesc->last_rec_title_y = -1;
	}

	/* fast leaving */
	if (win == NULL)
		return;

	if (is_rownum_luc)
	{
		/* just clean */
		werase(win);
		return;
	}

	if (is_scrollbar)
	{
		draw_scrollbar_win(win, t, scrdesc, opts);
		return;
	}

	init_lbi_ddesc(&lbi, desc, srcy);

	row = 0;

	getmaxyx(win, maxy, maxx);

	while (row < maxy )
	{
		char	   *rowstr = NULL;
		bool		line_is_valid = false;
		LineInfo   *lineinfo = NULL;
		bool		is_bookmark_row = false;
		bool		is_cursor_row = false;
		bool		is_pattern_row = false;
		char		buffer[10];
		int			positions[100][2];
		int			npositions = 0;
		int			rowno = row + srcy_bak + 1 - desc->first_data_row;
		int			lineno;

		is_cursor_row = (!opts->no_cursor && row == cursor_row);

		(void) lbi_set_mark_next(&lbi, &lbm);

		line_is_valid = lbm_get_line(&lbm, &rowstr, &lineinfo, &lineno);

		if (odd_theme_identifier != -1 && lineinfo)
		{
			int			recno = lineno - lineinfo->recno_offset;

			if (recno % 2 == 1)
				t = &scrdesc->themes[odd_theme_identifier];
			else
				t = &scrdesc->themes[window_identifier];
		}

		/* when rownum is printed, don't process original text */
		if (is_rownum && line_is_valid)
		{
			snprintf(buffer, sizeof(buffer), "%*d ", maxx - 1, rowno);
			rowstr = buffer;
		}

		is_bookmark_row = (lineinfo != NULL && (lineinfo->mask & LINEINFO_BOOKMARK) != 0) ? true : false;

		if (!is_fix_rows && *scrdesc->searchterm != '\0' && !opts->no_highlight_search)
			lineinfo = set_line_info(opts, scrdesc, desc, &lbm, rowstr);

		is_pattern_row = (lineinfo != NULL && (lineinfo->mask & LINEINFO_FOUNDSTR) != 0) ? true : false;

		/* prepare position cache, when first occurrence is visible */
		if (lineinfo != NULL && (lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0 &&
			  srcx + maxx > lineinfo->start_char &&
			  *scrdesc->searchterm != '\0')
		{
			const char *str = rowstr;

			while (str != NULL && npositions < 100)
			{
				str = pspg_search(opts, scrdesc, str);

				if (str != NULL)
				{
					int		position = use_utf8 ? utf_string_dsplen(rowstr, str - rowstr) : (int) (str - rowstr);

					/* apply column selection filtr */
					if (scrdesc->search_columns > 0)
					{
						if (position < scrdesc->search_first_column)
						{
							str += charlen(str);
							continue;
						}

						if (position > scrdesc->search_first_column + scrdesc->search_columns - 1)
							break;
					}

					positions[npositions][0] = position;
					positions[npositions][1] = positions[npositions][0] + scrdesc->searchterm_char_size;

					/* don't search more if we are over visible part */
					if (positions[npositions][1] > srcx + maxx)
					{
						npositions += 1;
						break;
					}

					str += scrdesc->searchterm_size;
					npositions += 1;
				}
			}
		}

		active_attr = 0;

		if (is_bookmark_row)
		{
			if (!is_footer && !is_rownum)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_data_attr;
		}
		/* would not to show pattern colors in rownum window */
		else if (is_pattern_row && !is_rownum)
		{
			if (!is_footer)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_line_attr : t->pattern_line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_data_attr : t->pattern_data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_data_attr : t->pattern_data_attr;
		}
		else
		{
			if (!is_footer && !is_rownum)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_line_attr : t->line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_data_attr : t->data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_data_attr : t->data_attr;
		}

		wattron(win, active_attr);

		wmove(win, row++, 0);

		if (is_rownum)
		{
			waddstr(win, rowstr);
			wattroff(win, active_attr);
			continue;
		}

		if (rowstr != NULL)
		{
			int			i = 0;
			int			effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool		fix_line_attr_style;
			bool		is_expand_head;
			int			ei_min, ei_max;
			int			left_spaces;							/* aux left spaces */
			int			saved_pos;

			bool		is_top_deco;
			bool		is_head_deco;
			bool		is_bottom_deco;

			int			trailing_spaces = 0;
			bool		is_found_row = false;
			bool		is_in_range = false;

			int			bytes;
			char	   *ptr;
			bool		is_selected_rows;
			bool		is_selected_row;
			bool		is_selected_columns;
			bool		is_empty_row = *rowstr == '\0';

			if (is_text)
			{
				nspecwords = parse_line(rowstr, specwords, 30);

				/*
				 * When input document is non tabular format, then border_top_row,
				 * border_head_row, bottom_row can be badly identified, and these
				 * variables can hold some garbage values ( `--` SQL comments
				 * can be identified as border etc)
				 */
				is_top_deco = false;
				is_head_deco = false;
				is_bottom_deco = false;
			}
			else
			{
				nspecwords = 0;

				is_top_deco = effective_row == desc->border_top_row;
				is_head_deco = effective_row == desc->border_head_row;
				is_bottom_deco = effective_row == desc->border_bottom_row;
			}

			is_found_row = scrdesc->found && scrdesc->found_row == effective_row;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
				if (is_expand_head)
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
					is_top_deco = effective_row == desc->border_top_row;
					is_head_deco = effective_row == desc->border_head_row;
					is_bottom_deco = effective_row == desc->border_bottom_row;

					fix_line_attr_style = is_top_deco || is_head_deco || is_bottom_deco;
				}
				else
					fix_line_attr_style = false;

				is_expand_head = false;
			}

			/*
			 * To ensure visible column names how it can be possible, we should to
			 * print column names directly (not like input document row.
			 */
			if (is_fix_rows_only && rowstr == desc->namesline )
			{
				int		loc_selected_xmin = INT_MIN;
				int		loc_selected_xmax = INT_MIN;

				/* mark columns names only when columns are selected */
				if (selected_xmin != INT_MIN && scrdesc->selected_first_row == -1)
				{
					loc_selected_xmin = selected_xmin;
					loc_selected_xmax = selected_xmax;
				}

				print_column_names(win, srcx,
								   vcursor_xmin, vcursor_xmax,
								   loc_selected_xmin, loc_selected_xmax,
								   desc, opts, t);
				continue;
			}

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;

			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= dsplen(rowstr);
					rowstr += charlen(rowstr);
					if (i < 0)
						left_spaces = -i;
				}
				else
					break;
			}

			/* Fix too hungry cutting when some multichar char is removed */
			if (left_spaces > 0)
			{
				char   *p;
				int		aux_left_spaces = left_spaces;

				free_row = smalloc(left_spaces + strlen(rowstr) + 1);

				p = free_row;
				while (aux_left_spaces-- > 0)
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
			i = 0;
			saved_pos = srcx;

			is_selected_rows = is_selectable && scrdesc->selected_first_row != -1;
			is_selected_row = rowno >= scrdesc->selected_first_row + 1 &&
							  rowno < scrdesc->selected_first_row + 1 + scrdesc->selected_rows;

			is_selected_columns = is_selectable && selected_xmin != INT_MIN;

			/*
			 * workaround for empty rows in text mode - because row is
			 * empty, the is_in_range is not initialized correcty.
			 */
			is_in_range = is_text && is_empty_row && is_selected_row;

			/* find length of maxx characters */
			if (*ptr != '\0')
			{

				while (i < maxx)
				{
					bool		is_cursor;
					bool		is_cross_cursor = false;
					bool		is_vertical_cursor = false;
					int			pos = (i != -1) ? srcx + i : -1;
					bool		skip_char = false;
					int			specword_typ = 0;

					if (nspecwords > 0)
					{
						int		j;

						for (j = 0; j < nspecwords; j++)
						{
							if (pos >= specwords[j].start_pos && pos <= specwords[j].end_pos)
							{
								specword_typ = specwords[j].typ;
								break;
							}
						}
					}

					is_in_range = false;

					if (is_selected_rows)
					{
						if (is_selected_row)
						{
							if (selected_xmin != INT_MIN && pos != -1)
							{
								if (pos >= selected_xmin && pos <= selected_xmax)
									is_in_range = true;
							}
							else
								is_in_range = true;
						}
					}
					else if (is_selected_columns && pos != -1)
					{
						if (pos >= selected_xmin && pos <= selected_xmax)
							is_in_range = true;
					}

					if (i != -1 && vcursor_xmin <= i && i <= vcursor_xmax)
					{
						is_cross_cursor = is_cursor_row;
						is_cursor = !is_cursor_row && !is_pattern_row;
						is_vertical_cursor = true;
					}
					else
					{
						is_cross_cursor = false;
						is_vertical_cursor = false;
						is_cursor = is_cursor_row;
					}

					if (is_expand_head && !is_pattern_row && !is_bookmark_row)
					{
						attr_t		new_attr;

						if (is_cursor)
							new_attr = pos >= ei_min && pos <= ei_max ? t->cursor_expi_attr : t->cursor_line_attr;
						else
							new_attr = pos >= ei_min && pos <= ei_max ? t->expi_attr : t->line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
								saved_pos = pos;
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
						attr_t	new_attr = active_attr;
						bool	print_acs_vline = false;
						char	column_format;

						column_format = desc->headline_transl != NULL && pos >= 0 ? desc->headline_transl[pos] : ' ';

						if (opts->force_uniborder && desc->linestyle == 'a')
						{
							if (*(rowstr + left_spaces + bytes) == '|' &&
									(column_format == 'L' || column_format == 'R' || column_format == 'I'))
							{
								print_acs_vline = true;
							}
						}

						if (is_in_range)
						{
							new_attr = is_cursor ? t->selection_cursor_attr : t->selection_attr;
							if (is_pattern_row && !is_cursor &&
									is_in_searched_pattern(pos, scrdesc, lineinfo, positions, npositions))
								new_attr = new_attr ^ A_REVERSE;
						}
						else if (is_cross_cursor)
						{
							new_attr = column_format == 'd' ? t->cross_cursor_attr : t->cross_cursor_line_attr;
						}
						else if (is_bookmark_row)
						{
							if (!is_cursor_row )
								new_attr = column_format == 'd' ? t->bookmark_data_attr : t->bookmark_line_attr;
							else
								new_attr = t->cursor_bookmark_attr;
						}
						else if (is_pattern_row && !is_cursor)
						{
							if (is_footer)
								new_attr = t->pattern_data_attr;
							else if (is_vertical_cursor)
								new_attr = column_format == 'd' ? t->pattern_vertical_cursor_attr : t->pattern_vertical_cursor_line_attr;
							else if (pos < desc->headline_char_size)
								new_attr = column_format == 'd' ? t->pattern_data_attr : t->pattern_line_attr;

							if (new_attr == t->pattern_data_attr || new_attr == t->pattern_vertical_cursor_attr)
							{
								if (is_in_searched_pattern(pos, scrdesc, lineinfo, positions, npositions))
									new_attr = t->found_str_attr;
							}
						}
						else if (is_footer)
							new_attr = is_cursor ? t->cursor_data_attr : t->data_attr;
						else if (pos < desc->headline_char_size)
						{
							if (is_cursor )
								new_attr = column_format == 'd' ? t->cursor_data_attr : t->cursor_line_attr;
							else
								new_attr = column_format == 'd' ? t->data_attr : t->line_attr;
						}

						if (specword_typ == 1 || specword_typ == 2)
							new_attr |= A_BOLD;
						else if (specword_typ == 3)
							new_attr |= A_ITALIC | A_UNDERLINE;

						if (is_cursor || is_cross_cursor)
						{
							if (is_found_row && pos >= scrdesc->found_start_x &&
									pos < scrdesc->found_start_x + scrdesc->searchterm_char_size)
								new_attr = new_attr ^ ( A_REVERSE | pattern_fix );
							else if (is_pattern_row)
							{
								if (is_in_searched_pattern(pos, scrdesc, lineinfo, positions, npositions))
									new_attr = t->cursor_pattern_attr;
							}
						}

						if (print_acs_vline && bytes > 0)
						{
							waddnstr(win, rowstr, bytes);
							rowstr += bytes;
							bytes = 0;
							saved_pos = pos;
						}

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
								saved_pos = pos;
							}

							/* disable current style */
							wattroff(win, active_attr);

							/* active new style */
							active_attr = new_attr;
							wattron(win, active_attr);
						}

						if (print_acs_vline)
						{
							waddch(win, ACS_VLINE);
							bytes = 0;
							rowstr += 1;

							/*
							 * because we printed subst char already and we updated rowstr,
							 * we don't would to increase bytes variable later.
							 */
							skip_char = true;
						}
					}
					else
					{
						if (!is_footer)
						{
							attr_t		new_attr;

							if (is_in_range)
								new_attr = is_cursor ? t->selection_cursor_attr : t->selection_attr;

							else if (is_cross_cursor)
								new_attr = t->cross_cursor_line_attr;

							else if (is_cursor)
								new_attr = t->cursor_line_attr;

							else
								new_attr = t->line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									flush_bytes(win,
												rowstr,
												bytes,
												saved_pos,
												is_top_deco,
												is_head_deco,
												is_bottom_deco,
												desc,
					  							opts);


									rowstr += bytes;
									bytes = 0;
									saved_pos = pos;
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
						int dlen = dsplen(ptr);
						int len  = charlen(ptr);

						i = (dlen != -1 && i != -1) ? i + dlen : -1;
						ptr += len;

						if (!skip_char)
							bytes += len;
					}
					else
					{

						/*
						 * psql reduces trailing spaces when border is 0 or 1. These spaces
						 * should be printed after content.
						 */
						if (is_vertical_cursor && i != -1)
						{
							int ts1 = maxx - i + 1;
							int ts2 = vcursor_xmax - i + 1;

							trailing_spaces = ts1 < ts2 ? ts1 : ts2;
						}

						break;
					}
				} /* end while */
			}

			if (bytes > 0)
				flush_bytes(win,
							rowstr,
							bytes,
							saved_pos,
							is_top_deco,
							is_head_deco,
							is_bottom_deco,
							desc,
							opts);

			/* print trailing spaces necessary for correct vertical cursor */
			if (trailing_spaces > 0)
			{
				wrepeatspace(win, trailing_spaces);
				i += trailing_spaces;
			}

			/* When we don't position of last char, we can (for cursor draw) use 0 */
			i = i != -1 ? i : 0;

			/* draw cursor or bookmark line to screen end of line */
			if (i < maxx)
			{
				attr_t		attr = 0;

				/* clean other chars on line */
				wclrtoeol(win);

				if (is_in_range && is_cursor_row)
					attr = t->selection_cursor_attr;
				else if (is_in_range && !is_cursor_row)
					attr = t->selection_attr;
				else if (is_cursor_row && !is_bookmark_row)
					attr = t->cursor_data_attr;
				else if (!is_cursor_row && is_bookmark_row)
					attr = t->bookmark_data_attr;
				else if (is_cursor_row && is_bookmark_row)
					attr = t->cursor_bookmark_attr;
				else if (!is_cursor_row && is_pattern_row)
					attr = t->pattern_data_attr;

				if (attr)
					mvwchgat(win, row - 1, i, -1, attr, PAIR_NUMBER(attr), 0);
			}

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

	(void) attr;

	return "";

#else

	static char result[20];
	int		pairno;
	short int fc, bc;

	pairno = PAIR_NUMBER(attr);
	ansi_colors(pairno, &fc, &bc);

	if ((attr & A_BOLD) != 0)
	{
		snprintf(result, 20, "\033[1;%d;%dm", fc, bc);
	}
	else
		snprintf(result, 20, "\033[0;%d;%dm", fc, bc);

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
	LineBufferIter lbi;
	attr_t		active_attr;
	int			srcy_bak = srcy;

	init_lbi_ddesc(&lbi, desc, srcy);

	row = 0;

	if (offsety)
		/* \033 is \e */
		printf("\033[%dB", offsety);

	while (row < maxy)
	{
		char	   *rowstr = NULL;

		(void) lbi_get_line_next(&lbi, &rowstr, NULL, NULL);

		active_attr = line_attr;
		printf("%s", ansi_attr(active_attr));

		row += 1;

		if (rowstr != NULL)
		{
			int			i;
			int			effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool		fix_line_attr_style;
			bool		is_expand_head;
			int			ei_min, ei_max;
			int			left_spaces;
			char	   *free_row;
			int			bytes;
			char	   *ptr;

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
				printf("\033[%dC", offsetx);

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;

			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= dsplen(rowstr);
					rowstr += charlen(rowstr);
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
				if (!free_row)
				{
					fprintf(stderr, "out of memory");
					exit(1);
				}

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
					int		pos = srcx + i;

					if (is_expand_head)
					{
						attr_t	new_attr;

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
						if (pos < desc->headline_char_size)
						{
							attr_t	new_attr;

							new_attr = desc->headline_transl[pos] == 'd' ? data_attr : line_attr;

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
					}

					if (*ptr != '\0' && *ptr != '\n')
					{
						int len  = charlen(ptr);

						i += dsplen(ptr);
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
					printf("\033[K");
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

void
draw_data(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
		  int first_data_row, int first_row, int cursor_col,
		  int footer_cursor_col, int fix_rows_offset)
{
	struct winsize size;

	if (ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size) >= 0)
	{
		int		i;
		int			expected_rows;

		expected_rows = min_int(size.ws_row - 1 - scrdesc->top_bar_rows,
								desc->last_row + 1);

		for (i = 0; i < expected_rows; i++)
			printf("\033D");

		/* Go wit cursor to up */
		printf("\033[%dA", expected_rows);

		/* now, the screen can be different, due prompt */
		scrdesc->rows_rows = min_int(scrdesc->rows_rows,
									 expected_rows - scrdesc->fix_rows_rows);

		/*
		 * Save cursor - Attention, there are a Fedora29 bug, and it doesn't work
		 */
		printf("\0337");

		if (scrdesc->fix_cols_cols > 0)
		{
			draw_rectange(scrdesc->fix_rows_rows, 0,
						  scrdesc->rows_rows, scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}

		if (scrdesc->fix_rows_rows > 0 )
		{
			/* Go to saved position */
			printf("\0338\0337");

			draw_rectange(0, scrdesc->fix_cols_cols,
						  scrdesc->fix_rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
		{
			/* Go to saved position */
			printf("\0338\0337");

			draw_rectange(0, 0,
						  scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}

		if (scrdesc->rows_rows > 0)
		{
			/* Go to saved position */
			printf("\0338\0337");

			draw_rectange(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  scrdesc->rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  opts->theme == 2 ? A_BOLD : 0,
						  opts->theme == 2 && (desc->headline_transl == NULL) ? A_BOLD : 0,
						  COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (w_footer(scrdesc) != NULL)
		{
			/* Go to saved position */
			printf("\0338\0337");

			draw_rectange(scrdesc->fix_rows_rows + scrdesc->rows_rows, 0,
						  scrdesc->footer_rows, scrdesc->maxx,
						  first_data_row + first_row + scrdesc->rows_rows - fix_rows_offset, footer_cursor_col,
						  desc,
						  COLOR_PAIR(9), 0, 0, true);
		}

		/* reset */
		printf("\033[0m\r");
	}
}
