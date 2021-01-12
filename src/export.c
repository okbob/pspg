/*-------------------------------------------------------------------------
 *
 * export.c
 *	  a routines for exporting data
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/export.c
 *
 *-------------------------------------------------------------------------
 */


#include <errno.h>
#include <string.h>

#include "pspg.h"
#include "commands.h"
#include "unicode.h"

static void
getdeco(char *str, char **ldeco, char **rdeco, int *ldecolen, int *rdecolen)
{
	char	   *start = NULL;
	char	   *stop = NULL;

	while (*str)
	{
		if (!start)
			start = str;

		if (*str != ' ')
			stop = str;

		str += utf8charlen(*str);
	}

	*ldeco = start;
	*ldecolen = utf8charlen(*start);
	*rdeco = stop;
	*rdecolen = utf8charlen(*stop);
}


static char *
pos_substr(char *str, int xmin, int xmax, int *substrlen, bool force8bit, bool trim)
{
	char   *substr = NULL;

	*substrlen = 0;

	if (force8bit)
	{
		int		len = strlen(str);

		if (len > xmin)
		{
			*substrlen = xmax - xmin - 1;
			len = len - xmin - 1;

			if (len < *substrlen)
				*substrlen = len;

			substr = str + xmin + 1;

			if (trim && *substrlen > 1)
			{
				char *ptr = substr + *substrlen - 1;

				while (ptr > substr && *ptr == ' ')
				{
					ptr--;
					*substrlen -= 1;
				}
			}
		}
	}
	else
	{
		int		pos = 0;
		char   *first_ending_space = NULL;

		while (*str)
		{
			int		charlen = utf8charlen(*str);

			if (pos > xmin)
			{
				if (!substr)
					substr = str;
			}

			if (*str == ' ')
			{
				if (!first_ending_space)
					first_ending_space = str;
			}
			else
			{
				if (first_ending_space)
					first_ending_space = NULL;
			}

			charlen = utf8charlen(*str);
			pos += utf_dsplen(str);
			str += charlen;

			if (pos >= xmax)
				break;
		}

		if (trim && first_ending_space)
			*substrlen = first_ending_space - substr;
		else
			*substrlen = str - substr;
	}

	if (trim)
	{
		while (*substr == ' ')
		{
			*substrlen -= 1;
			substr++;
		}
	}

	return substr;
}

/*
 * Returns true, when the operation was successfull
 */
bool
export_data(Options *opts,
			ScrDesc *scrdesc,
			DataDesc *desc,
			int cursor_row,
			int cursor_column,
			FILE *fp,
			int rows,
			double percent,
			PspgCommand cmd,
			ClipboardFormat format)
{
	int		rn;
	int		lbrn = 0;
	LineBuffer *lnb = &desc->rows;
	char   *rowstr;
	int		rowstrlen;
	bool	print_header = true;
	bool	print_footer = true;
	bool	print_border = true;
	bool	print_header_line = true;
	bool	print_vertical_border = false;

	int		min_row = desc->first_data_row;
	int		max_row = desc->last_data_row;
	int		xmin = -1;
	int		xmax = -1;

	current_state->errstr = NULL;

	if (cmd == cmd_CopyLine ||
		cmd == cmd_CopyLineExtended ||
		(cmd == cmd_Copy && !opts->no_cursor))
	{
		min_row = max_row = cursor_row + desc->first_data_row;
		print_footer = false;
	}

	if ((cmd == cmd_Copy && opts->vertical_cursor) ||
		cmd == cmd_CopyColumn)
	{
		xmin = desc->cranges[cursor_column - 1].xmin;
		xmax = desc->cranges[cursor_column - 1].xmax;

		print_footer = false;
		print_vertical_border = desc->border_type == 2;
	}

	/* copy value from cross of vertical and horizontal cursor */
	if (cmd == cmd_Copy && !opts->no_cursor && opts->vertical_cursor)
	{
		print_header = false;
		print_header_line = false;
		print_vertical_border = false;
		print_border = false;
	}

	if (cmd == cmd_CopyTopLines ||
		cmd == cmd_CopyBottomLines)
	{
		int		skip_data_rows;

		if (rows < 0 || percent < 0.0)
		{
			format_error("arguments (\"rows\" or \"percent\") of function export_data are negative");
			return false;
		}

		if (percent != 0.0)
			rows = (double) (desc->last_data_row - desc->first_data_row + 1) * (percent / 100.0);

		if (cmd == cmd_CopyBottomLines)
			skip_data_rows = desc->last_data_row - desc->first_data_row + 1 - rows;
		else
			skip_data_rows = 0;

		min_row += skip_data_rows;
		max_row = desc->first_data_row + rows - 1 + skip_data_rows;

		print_footer = false;
	}

	if (format != CLIPBOARD_FORMAT_TEXT)
		print_border = false;

	for (rn = 0; rn <= desc->last_row; rn++)
	{
		LineInfo *linfo;
		char   *ldeco = NULL, *rdeco = NULL;
		int		ldecolen = 0, rdecolen = 0;

		if (desc->order_map)
		{
			MappedLine *mp = &desc->order_map[rn];
			LineBuffer *auxlnb = mp->lnb;
			int		auxlbrn = mp->lnb_row;

			rowstr = auxlnb->rows[auxlbrn];
			linfo = auxlnb->lineinfo ? &auxlnb->lineinfo[auxlbrn] : NULL;
		}
		else
		{
			if (lbrn >= lnb->nrows)
			{
				lnb = lnb->next;
				lbrn = 0;
			}

			if (!lnb)
			{
				log_row("there is not buffered row of %d line", rn);
				leave("internal data error - missing data");
			}

			linfo = lnb->lineinfo ? &lnb->lineinfo[lbrn] : NULL;
			rowstr = lnb->rows[lbrn++];
		}

		/* reduce rows from export */
		if (rn >= desc->first_data_row && rn <= desc->last_data_row)
		{
			if (rn < min_row || rn > max_row)
				continue;

			if (cmd == cmd_CopyMarkedLines)
			{
				if (!linfo || ((linfo->mask & LINEINFO_BOOKMARK) == 0))
					continue;
			}
			else if (cmd == cmd_CopyLine)
			{
				if (rn - desc->first_data_row != cursor_row)
					continue;
			}

		//	if (cmd == cmd_CopySearchedLines)
		//	{
		//		if (!linfo || ((linfo->mask & LINEINFO_FOUNDSTR) == 0))
		//			continue;
		//	}
		}
		else
		{
			if (!print_border &&
				(rn == desc->border_top_row ||
				 rn == desc->border_bottom_row))
				continue;
			if (!print_header_line &&
				rn == desc->border_head_row)
				continue;
			if (!print_header && rn < desc->fixed_rows)
				continue;
			if (!print_footer && desc->footer_row != -1 && rn >= desc->footer_row)
				continue;
		}

		rowstrlen = -1;

		if (xmin != -1)
		{
			if (print_vertical_border)
			{
				getdeco(rowstr,
						&ldeco, &rdeco,
						&ldecolen, &rdecolen);

				rowstr = pos_substr(rowstr, xmin, xmax, &rowstrlen, opts->force8bit, false);
			}
			else
				rowstr = pos_substr(rowstr, xmin, xmax, &rowstrlen, opts->force8bit, true);
		}

		errno = 0;

		if (format == CLIPBOARD_FORMAT_TEXT)
		{
			if (xmin != -1)
			{
				if (print_vertical_border)
					fprintf(fp, "%.*s%.*s%.*s\n", ldecolen, ldeco,
												rowstrlen, rowstr ? rowstr : "",
												rdecolen, rdeco);
				else
					fprintf(fp, "%.*s\n", rowstrlen, rowstr ? rowstr : "");
			}
			else
				fprintf(fp, "%s\n", rowstr ? rowstr : "");
		}

		if (errno != 0)
		{
			format_error("%s", strerror(errno));
			log_row("Cannot write (%s)", current_state->errstr);

			return false;
		}
	}

	return true;
}
