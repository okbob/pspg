/*-------------------------------------------------------------------------
 *
 * linebuffer.c
 *	  a routines for iteration over stored lines
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/linebuffer.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"


/*
 * Initialize line buffer iterator
 */
void
init_lbi(LineBufferIter *lbi,
		 LineBuffer *lb,
		 MappedLine *order_map,
		 int order_map_items,
		 int init_pos)
{
	lbi->start_lb = lb;

	lbi->order_map = order_map;
	lbi->order_map_items = order_map_items;

	lbi_set_lineno(lbi, init_pos);
}

/*
 * Common case - initialize line buffer iterator
 * for stored data.
 */
void
init_lbi_datadesc(LineBufferIter *lbi,
				  DataDesc *desc,
				  int init_pos)
{
	init_lbi(lbi,
			 &desc->rows,
			 desc->order_map,
			 desc->order_map_items,
			 init_pos);
}

/*
 * Set iterator to absolute position in line buffer
 */
bool
lbi_set_lineno(LineBufferIter *lbi, int pos)
{
	lbi->lineno = pos;

	if (lbi->order_map)
	{
		if (lbi->order_map_items > pos)
		{
			MappedLine *mpl = &lbi->order_map[pos];

			lbi->current_lb = mpl->lnb;
			lbi->current_lb_rowno = mpl->lnb_row;

			return true;
		}

		/* set max lineno */
		lbi->lineno = lbi->order_map_items;
	}
	else
	{
		int		lineno_offset = 0;

		lbi->current_lb = lbi->start_lb;

		while (lbi->current_lb && pos >= LINEBUFFER_LINES)
		{
			pos -= LINEBUFFER_LINES;
			lineno_offset += lbi->current_lb->nrows;

			lbi->current_lb = lbi->current_lb->next;
		}

		if (lbi->current_lb)
		{
			if (pos < lbi->current_lb->nrows)
			{
				lbi->current_lb_rowno = pos;

				return true;
			}
			else
				lbi->lineno = lineno_offset + lbi->current_lb->nrows;
		}
		else
			lbi->lineno = lineno_offset;
	}

	lbi->current_lb = NULL;
	lbi->current_lb_rowno = 0;

	return false;
}

/*
 * Initialize line buffer mark to current position in
 * line buffer.
 */
void
lbi_set_mark(LineBufferIter *lbi, LineBufferMark *lbm)
{
	lbm->lb = lbi->current_lb;
	lbm->lb_rowno = lbi->current_lb_rowno;
	lbm->lineno = lbi->lineno;
}

/*
 * Initialize line buffer mark to current position in line
 * buffer. Increase current position in line buffer. Returns
 * true if line buffer mark is valid.
 */
bool
lbi_set_mark_next(LineBufferIter *lbi, LineBufferMark *lbm)
{
	lbi_set_mark(lbi, lbm);
	(void) lbi_next(lbi);

	return lbm->lb && lbm->lb_rowno < lbm->lb->nrows;
}

/*
 * Working horse of lbm_get_line and lbi_get_line routines
 */
static bool
lb_get_line(LineBuffer *lb,
			int rowno,
			int lineno, 
			char **line,
			LineInfo **linfo,
			int	*linenoptr)
{
	if (linenoptr)
		*linenoptr = lineno;

	if (lb && rowno >= 0 && rowno < lb->nrows)
	{
		*line = lb->rows[rowno];

		if (linfo)
			*linfo = lb->lineinfo ? &lb->lineinfo[rowno] : NULL;

		return true;
	}

	*line = NULL;

	if (linfo)
		*linfo = NULL;

	return false;
}

/*
 * Returns line related to line buffer mark
 */
bool
lbm_get_line(LineBufferMark *lbm,
			 char **line,
			 LineInfo **linfo,
			 int *lineno)
{
	return lb_get_line(lbm->lb,
					   lbm->lb_rowno,
					   lbm->lineno,
					   line,
					   linfo,
					   lineno);
}

/*
 * Returns true, when returns valid line from line buffer.
 */
bool
lbi_get_line(LineBufferIter *lbi,
			  char **line,
			  LineInfo **linfo,
			  int *lineno)
{
	return lb_get_line(lbi->current_lb,
					   lbi->current_lb_rowno,
					   lbi->lineno,
					   line,
					   linfo,
					   lineno);
}

/*
 * Returns true, when returns valid line from line buffer.
 * Increments position in linebuffer.
 */
bool
lbi_get_line_next(LineBufferIter *lbi,
				  char **line,
				  LineInfo **linfo,
				  int *lineno)
{
	bool result;

	result = lbi_get_line(lbi, line, linfo, lineno);

	(void) lbi_next(lbi);

	return result;
}

/*
 * Move on next line in line buffer. Returns false, when there
 * are not valid line in buffer.
 */
bool
lbi_next(LineBufferIter *lbi)
{
	if (lbi->order_map)
	{
		if (lbi->lineno + 1 < lbi->order_map_items)
		{
			MappedLine *mpl;

			lbi->lineno +=1;

			mpl = &lbi->order_map[lbi->lineno];

			lbi->current_lb = mpl->lnb;
			lbi->current_lb_rowno = mpl->lnb_row;

			return true;
		}
		else
			lbi->lineno = lbi->order_map_items;
	}
	else
	{
		if (lbi->current_lb)
		{
			/*
			 * Previous row must be valid, so we can increase
			 * lineno without creating gap after last line lineno.
			 */
			lbi->lineno += 1;

			lbi->current_lb_rowno += 1;
			if (lbi->current_lb_rowno < lbi->current_lb->nrows)
				return true;

			if (lbi->current_lb->next)
			{
				lbi->current_lb = lbi->current_lb->next;
				lbi->current_lb_rowno = 0;

				return true;
			}
		}
	}

	lbi->current_lb = NULL;
	lbi->current_lb_rowno = 0;

	return false;
}

/*
 * Simple line buffer iterator allows just only forward
 * scan.
 */
SimpleLineBufferIter *
init_slbi_datadesc(SimpleLineBufferIter *slbi, DataDesc *desc)
{
	slbi->lb = &desc->rows;
	slbi->lb_rowno = 0;

	if (slbi->lb->nrows > 0)
		return slbi;
	else
		return NULL;

}

SimpleLineBufferIter *
slbi_get_line_next(SimpleLineBufferIter *slbi,
				   char **line,
				   LineInfo **linfo)
{
	if (slbi)
	{
		LineBuffer *lb = slbi->lb;

		/* one line should be every time. The possibility is checked before */

		if (linfo)
			*linfo = lb->lineinfo ? &lb->lineinfo[slbi->lb_rowno] : NULL;

		if (line)
			*line = lb->rows[slbi->lb_rowno];

		slbi->lb_rowno += 1;

		/* check an possibility of next read */
		if (slbi->lb_rowno < lb->nrows)
			return slbi;

		if (lb->next)
		{
			slbi->lb = lb->next;
			slbi->lb_rowno = 0;

			/* should not be possible */
			if (slbi->lb->nrows == 0)
				return NULL;
		}
		else
			return NULL;
	}
	else
		*line = NULL;

	return slbi;
}
