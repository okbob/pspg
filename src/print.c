/*-------------------------------------------------------------------------
 *
 * print.c
 *	  visualisation loaded data
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
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
				int len = opts->force8bit ? 1 : utf8charlen(*rowstr);

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
	int		extra_space = desc->border_type == 0 ? 0 : 1;
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
		bytes = utf8charlen(*ptr);
		chars = utf_dsplen(ptr);

		if (pos + chars > srcx)
		{
			wprintw(win, "%*s", pos + chars - srcx, "");

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

	/* for each visible column (position) and defined colum */
	while (pos < maxx && headline_ptr < headline_end_ptr)
	{
		char	column_format = *headline_ptr;
		bool	is_cursor = vcursor_xmin <= pos && pos <= vcursor_xmax;

		bytes = utf8charlen(*ptr);
		chars = utf_dsplen(ptr);

		if (is_cursor )
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
				wprintw(win, "%.*s", bytes, ptr);
		}
		else
			wprintw(win, "%*s", chars, "");

		headline_ptr += chars;
		ptr += bytes;
		pos += chars;
	}

	wattroff(win, active_attr);

	/* check all column names and print that are visible */
	for (i = 0; i < desc->columns; i++)
	{
		CRange *col = &desc->cranges[i];
		int		reduced_xmin = col->xmin - srcx;
		int		reduced_xmax = col->xmax - srcx;
		int		visible_xmin;
		int		visible_xmax;
		int		visible_width;
		int		x = -1;
		int		skipbytes = 0;
		char   *colname;
		int		colname_size;
		int		colname_width;

		/* skip invisible columns */
		if (reduced_xmax < 0)
			continue;

		/* leave when no other column can be visible */
		if (reduced_xmin > maxx)
			break;

		colname = desc->namesline + col->name_offset;
		colname_size = col->name_size;
		colname_width = col->name_width;

		visible_xmin = reduced_xmin > 1 ? reduced_xmin : 1;
		visible_xmax = reduced_xmax < maxx ? reduced_xmax : maxx;
		visible_width = visible_xmax - visible_xmin + extra_space;

		if (reduced_xmin >= 1 && reduced_xmax <= maxx)
		{
			/* center content */
			x = (visible_width - colname_width - 1) / 2 + visible_xmin + 1;
		}
		else if (reduced_xmin >= 1)
		{
			/*
			 * display first visible chars from column name when all chars
			 * from column name cannot be displayed, or center content.
			 */
			if (visible_width <= colname_width + 1 + extra_space)
			{
				char   *ptr = colname;
				int		x2;

				/* size of visible chars will be calculated */
				colname_size = 0;

				x = visible_xmin + 1 + extra_space;
				x2 = x;

				while (x2 <= maxx)
				{
					bytes = utf8charlen(*ptr);
					chars = utf_dsplen(ptr);

					if (x2 + chars > maxx)
						break;

					colname_size += bytes;
					x2 += chars;
					ptr += bytes;
				}
			}
			else
				x = (visible_width - colname_width - 1) / 2 + visible_xmin + 1;
		}
		else if (reduced_xmin < 1)
		{
			/* display last visible chars or center content */
			if (visible_width <= col->name_width + extra_space)
			{
				char   *ptr = colname;
				int		reduce_width = colname_width + extra_space - visible_width;

				/* there is not a space from left */
				x = visible_xmin - 1;

				if (reduce_width < colname_width)
				{
					while (reduce_width > 0)
					{
						bytes = utf8charlen(*ptr);
						chars = utf_dsplen(ptr);

						skipbytes += bytes;
						reduce_width -= chars;
						ptr += bytes;
					}
				}
				else
					/* there is not any content to print */
					x = -1;

				if (reduce_width < 0 && x != -1)
					x += abs(reduce_width);
			}
			else
				x = (visible_width - col->name_width - 1) / 2 + visible_xmin;
		}

		new_attr = (vcursor_xmin <= x && x <= vcursor_xmax) ? t->cursor_data_attr : t->data_attr;

		if (x != -1)
		{
			wattron(win, new_attr);
			mvwprintw(win, cy, x, "%.*s", colname_size - skipbytes, colname + skipbytes);
			wattroff(win, new_attr);
		}
	}
}

void
window_fill(int window_identifier,
			int srcy,
			int srcx,						/* offset to displayed data */
			int cursor_row,					/* row of row cursor */
			int vcursor_xmin,				/* xmin in display coordinates */
			int vcursor_xmax,				/* xmax in display coordinates */
			DataDesc *desc,
			ScrDesc *scrdesc,
			Options *opts)
{
	int			maxy, maxx;
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	attr_t		pattern_fix;
	int			srcy_bak = srcy;
	char		*free_row;
	WINDOW		*win;
	Theme		*t;
	bool		is_footer = window_identifier == WINDOW_FOOTER;
	bool		is_fix_rows = window_identifier == WINDOW_LUC || window_identifier == WINDOW_FIX_ROWS;
	bool		is_rownum = window_identifier == WINDOW_ROWNUM;
	bool		is_rownum_luc = window_identifier == WINDOW_ROWNUM_LUC;
	bool		is_fix_rows_only = window_identifier == WINDOW_FIX_ROWS;

	win = scrdesc->wins[window_identifier];
	t = &scrdesc->themes[window_identifier];

	pattern_fix = t->found_str_attr & A_UNDERLINE;

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
		char	   *rowstr = NULL;
		bool		line_is_valid = false;
		LineInfo   *lineinfo = NULL;
		bool		is_bookmark_row = false;
		bool		is_cursor_row = false;
		bool		is_found_row = false;
		bool		is_pattern_row = false;
		char		buffer[10];
		int			positions[100][2];
		int			npositions = 0;

		is_cursor_row = (!opts->no_cursor && row == cursor_row);

		if (desc->order_map)
		{
			int		rowno = row + srcy_bak;

			if (rowno <= desc->last_row)
			{
				MappedLine *mp = &desc->order_map[rowno];

				lnb = mp->lnb;
				lnb_row = mp->lnb_row;
				rowstr = lnb->rows[lnb_row];
				lineinfo = lnb->lineinfo ? &lnb->lineinfo[lnb_row] : NULL;

				line_is_valid = true;
			}
		}
		else
		{
			if (lnb_row == 1000)
			{
				lnb = lnb ? lnb->next : NULL;
				lnb_row = 0;
			}

			if (lnb != NULL && lnb_row < lnb->nrows)
			{
				rowstr = lnb->rows[lnb_row];
				if (lnb->lineinfo != NULL)
					lineinfo = &lnb->lineinfo[lnb_row];
				else
					lineinfo = NULL;
				lnb_row += 1;

				line_is_valid = true;
			}
		}

		/* when rownum is printed, don't process original text */
		if (is_rownum && line_is_valid)
		{
			int rowno = row + srcy_bak + 1 - desc->first_data_row;

			snprintf(buffer, sizeof(buffer), "%*d ", maxx - 1, rowno);
			rowstr = buffer;
		}

		is_bookmark_row = (lineinfo != NULL && (lineinfo->mask & LINEINFO_BOOKMARK) != 0) ? true : false;

		if (!is_fix_rows && *scrdesc->searchterm != '\0' && lnb != NULL &&  rowstr != NULL
					  && !opts->no_highlight_search)
		{
			if (lineinfo == NULL)
			{
				int		i;

				lnb->lineinfo = malloc(1000 * sizeof(LineInfo));
				if (lnb->lineinfo == NULL)
					leave("out of memory");

				memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));

				for (i = 0; i < lnb->nrows; i++)
					lnb->lineinfo[i].mask = LINEINFO_UNKNOWN;

				lineinfo = &lnb->lineinfo[lnb_row - 1];
			}

			if (lineinfo->mask & LINEINFO_UNKNOWN)
			{
				const char *str = rowstr;

				lineinfo->mask ^= LINEINFO_UNKNOWN;
				lineinfo->mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);

				while (str != NULL)
				{
					/*
					 * When we would to ignore case or lower case (in this case, we know, so
					 * pattern has not any upper char, then we have to use slower case insensitive
					 * searching.
					 */
					if (opts->ignore_case || (opts->ignore_lower_case && !scrdesc->has_upperchr))
					{
						if (opts->force8bit)
							str = nstrstr(str, scrdesc->searchterm);
						else
							str = utf8_nstrstr(str, scrdesc->searchterm);
					}
					else if (opts->ignore_lower_case && scrdesc->has_upperchr)
					{
						if (opts->force8bit)
							str = nstrstr_ignore_lower_case(str, scrdesc->searchterm);
						else
							str = utf8_nstrstr_ignore_lower_case(str, scrdesc->searchterm);
					}
					else
						/* we can use case sensitive searching (binary comparation) */
						str = strstr(str, scrdesc->searchterm);

					if (str != NULL)
					{
						if (lineinfo->mask & LINEINFO_FOUNDSTR)
						{
							/* When we detect multi occurrence, then stop searching */
							lineinfo->mask |= LINEINFO_FOUNDSTR_MULTI;
							break;
						}
						else
						{
							lineinfo->mask |= LINEINFO_FOUNDSTR;
							if (opts->force8bit)
								lineinfo->start_char = str - rowstr;
							else
								lineinfo->start_char = utf8len_start_stop(rowstr, str);
						}

						str += scrdesc->searchterm_size;
					}
				}
			}
		}

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
					positions[npositions][0] = opts->force8bit ? (size_t) (str - rowstr) : utf8len_start_stop(rowstr, str);
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
			int		i = 0;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;
			int		left_spaces;							/* aux left spaces */
			int		saved_pos;

			bool	is_top_deco = false;
			bool	is_head_deco = false;
			bool	is_bottom_deco = false;
			int		trailing_spaces = 0;

			is_top_deco = effective_row == desc->border_top_row;
			is_head_deco = effective_row == desc->border_head_row;
			is_bottom_deco = effective_row == desc->border_bottom_row;

			is_found_row = scrdesc->found && scrdesc->found_row == effective_row;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(opts, rowstr, &ei_min, &ei_max);
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
				print_column_names(win, srcx, vcursor_xmin, vcursor_xmax, desc, opts, t);
				continue;
			}

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;
			if (opts->force8bit)
			{
				while(i > 0)
				{
					if (*rowstr != '\0' && *rowstr != '\n')
					{
						i -= 1;
						rowstr += 1;
						if (i < 0)
							left_spaces = -i;
					}
					else
						break;
				}
			}
			else
			{
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

			/* find length of maxx characters */
			if (*ptr != '\0')
			{
				while (i < maxx)
				{
					bool	is_cursor;
					bool	is_cross_cursor = false;
					bool	is_vertical_cursor = false;
					int		pos = (i != -1) ? srcx + i : -1;
					bool	skip_char = false;

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

						if (is_cross_cursor)
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

							if ((new_attr == t->pattern_data_attr || new_attr == t->pattern_vertical_cursor_attr) && pos >= lineinfo->start_char)
							{
								if ((lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0)
								{
									int		j;

									for (j = 0; j < npositions; j++)
									{
										if (pos >= positions[j][0] && pos < positions[j][1])
										{
											new_attr = t->found_str_attr;
											break;
										}
									}
								}
								else
								{
									if (pos < lineinfo->start_char + scrdesc->searchterm_char_size)
										new_attr = t->found_str_attr;
								}
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

						if (is_cursor || is_cross_cursor)
						{
							if (is_found_row && pos >= scrdesc->found_start_x &&
									pos < scrdesc->found_start_x + scrdesc->searchterm_char_size)
								new_attr = new_attr ^ ( A_REVERSE | pattern_fix );
							else if (is_pattern_row && pos >= lineinfo->start_char)
							{
								if ((lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0)
								{
									int		j;

									for (j = 0; j < npositions; j++)
									{
										if (pos >= positions[j][0] && pos < positions[j][1])
										{
											new_attr = t->cursor_pattern_attr;
											break;
										}
									}
								}
								else
								{
									if (pos < lineinfo->start_char + scrdesc->searchterm_char_size)
										new_attr = t->cursor_pattern_attr;
								}
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

							if (is_cross_cursor)
							{
								new_attr = t->cross_cursor_line_attr;
							}
							else if (is_cursor)
							{
								new_attr = t->cursor_line_attr;
							}
							else
							{
								new_attr = t->line_attr;
							}

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
						if (opts->force8bit)
						{
							i += 1;
							ptr += 1;

							if (!skip_char)
								bytes += 1;
						}
						else
						{
							int dsplen = utf_dsplen(ptr);
							int len  = utf8charlen(*ptr);

							i = dsplen != -1 && i != -1 ? i + dsplen : -1;
							ptr += len;

							if (!skip_char)
								bytes += len;
						}
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
				wprintw(win, "%*s", trailing_spaces, "");
				i += trailing_spaces;
			}

			/* When we don't possition of last char, we can (for cursor draw) use 0 */
			i = i != -1 ? i : 0;

			/* clean other chars on line */
			if (i < maxx)
				wclrtoeol(win);

			/* draw cursor or bookmark line to screen end of line */
			if (i < maxx)
			{
				if (is_cursor_row && !is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, t->cursor_data_attr, PAIR_NUMBER(t->cursor_data_attr), 0);
				else if (!is_cursor_row && is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, t->bookmark_data_attr, PAIR_NUMBER(t->bookmark_data_attr), 0);
				else if (is_cursor_row && is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, t->cursor_bookmark_attr, PAIR_NUMBER(t->cursor_bookmark_attr), 0);
				else if (!is_cursor_row && is_pattern_row)
					mvwchgat(win, row - 1, i, -1, t->pattern_data_attr, PAIR_NUMBER(t->pattern_data_attr), 0);
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
			Options *opts,
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
		/* \033 is \e */
		printf("\033[%dB", offsety);

	while (row < maxy)
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr = NULL;

		if (desc->order_map)
		{
			int		rowno = row + srcy_bak;

			if (rowno <= desc->last_row)
			{
				MappedLine *mp = &desc->order_map[rowno];

				lnb = mp->lnb;
				lnb_row = mp->lnb_row;
				rowstr = lnb->rows[lnb_row];
			}
		}
		else
		{
			if (lnb_row == 1000)
			{
				lnb = lnb ? lnb->next : NULL;
				lnb_row = 0;
			}

			if (lnb != NULL && lnb_row < lnb->nrows)
			{
				rowstr = lnb->rows[lnb_row];
				lnb_row += 1;
			}
		}

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
				is_expand_head = is_expanded_header(opts, rowstr, &ei_min, &ei_max);
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
			if (opts->force8bit)
			{
				while(i > 0)
				{
					if (*rowstr != '\0' && *rowstr != '\n')
					{
						i -= 1;
						rowstr += 1;
						if (i < 0)
							left_spaces = -i;
					}
					else
						break;
				}
			}
			else
			{
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
						int len  = opts->force8bit ? 1 : utf8charlen(*ptr);
						i += opts->force8bit ? 1 : utf_dsplen(ptr);
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
	int		i;

	if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
	{
		int		expected_rows = min_int(size.ws_row - 1 - scrdesc->top_bar_rows,
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
						  opts, desc,
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
						  opts, desc,
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
						  opts, desc,
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
						  opts, desc,
						  opts->theme == 2 ? 0 | A_BOLD : 0,
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
						  opts, desc,
						  COLOR_PAIR(9), 0, 0, true);
		}

		/* reset */
		printf("\033[0m\r");
	}
}
