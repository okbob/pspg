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


#include "pspg.h"
#include "commands.h"

#include <errno.h>
#include <string.h>

/*
 * Returns true, when the operation was successfull
 */
bool
export_data(DataDesc *desc,
			ScrDesc *scrdesc,
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
	bool	print_header = true;
	bool	print_footer = true;
	bool	print_border = true;
	int		min_row = desc->first_data_row;
	int		max_row = desc->last_data_row;

	current_state->errstr = NULL;

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
				 rn == desc->border_head_row ||
				 rn == desc->border_bottom_row))
				continue;
			if (!print_header && rn < desc->fixed_rows)
				continue;
			if (!print_footer && desc->footer_row != -1 && rn >= desc->footer_row)
				continue;
		}

		errno = 0;

		if (format == CLIPBOARD_FORMAT_TEXT)
			fprintf(fp, "%s\n", rowstr);

		if (errno != 0)
		{
			format_error("%s", strerror(errno));
			log_row("Cannot write (%s)", current_state->errstr);

			return false;
		}
	}

	return true;
}
