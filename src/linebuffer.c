/*-------------------------------------------------------------------------
 *
 * linebuffer.c
 *	  a routines for iteration over stored lines
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/linebuffer.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"

#include <limits.h>
#include <stdlib.h>

/*
 * Initialize line buffer iterator
 */
inline void
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
inline void
init_lbi_ddesc(LineBufferIter *lbi,
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
inline void
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
 * Sets mark to line buffer specified by position. When false,
 * when position is not valid.
 */
bool
ddesc_set_mark(LineBufferMark *lbm, DataDesc *desc, int pos)
{
	lbm->lb = NULL;
	lbm->lineno = pos;

	if (desc->order_map)
	{
		if (pos >= 0 && pos < desc->order_map_items)
		{
			lbm->lb = desc->order_map[pos].lnb;
			lbm->lb_rowno = desc->order_map[pos].lnb_row;
			lbm->lineno = pos;

			return true;
		}
	}
	else
	{
		LineBuffer *lb = &desc->rows;

		while (lb && pos >= LINEBUFFER_LINES)
		{
			lb = lb->next;
			pos -= LINEBUFFER_LINES;
		}

		if (lb && pos < lb->nrows)
		{
			lbm->lb = lb;
			lbm->lb_rowno = pos;

			return true;
		}
	}

	return false;
}

void
lbm_xor_mask(LineBufferMark *lbm, char mask)
{
	if (!lbm->lb->lineinfo)
	{
		int		i;

		/* smalloc returns zero fill memory already */
		lbm->lb->lineinfo = smalloc(LINEBUFFER_LINES * sizeof(LineInfo));

		for (i = 0; i < LINEBUFFER_LINES; i++)
			lbm->lb->lineinfo[i].recno_offset = SHRT_MIN;
	}

	lbm->lb->lineinfo[lbm->lb_rowno].mask ^= mask;
}

void
lbm_recno_offset(LineBufferMark *lbm, short int recno_offset)
{
	if (!lbm->lb->lineinfo)
	{
		int		i;

		/* smalloc returns zero fill memory already */
		lbm->lb->lineinfo = smalloc(LINEBUFFER_LINES * sizeof(LineInfo));

		for (i = 0; i < LINEBUFFER_LINES; i++)
			lbm->lb->lineinfo[i].recno_offset = SHRT_MIN;
	}

	lbm->lb->lineinfo[lbm->lb_rowno].recno_offset = recno_offset;
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
		if (line)
			*line = lb->rows[rowno];

		if (linfo)
			*linfo = lb->lineinfo ? &lb->lineinfo[rowno] : NULL;

		return true;
	}

	if (line)
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
inline bool
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
inline bool
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
 * Returns true, when returns valid line from line buffer.
 * Decreases position in linebuffer.
 */
inline bool
lbi_get_line_prev(LineBufferIter *lbi,
				  char **line,
				  LineInfo **linfo,
				  int *lineno)
{
	bool result;

	result = lbi_get_line(lbi, line, linfo, lineno);

	(void) lbi_prev(lbi);

	return result;
}

/*
 * Move to prev line in line buffer. Returns false, when there
 * is not valid line in buffer.
 */
bool
lbi_prev(LineBufferIter *lbi)
{
	if (lbi->order_map)
	{
		if (lbi->lineno > 0)
		{
			MappedLine *mpl;

			lbi->lineno -= 1;

			mpl = &lbi->order_map[lbi->lineno];

			lbi->current_lb = mpl->lnb;
			lbi->current_lb_rowno = mpl->lnb_row;

			return true;
		}
		else
			lbi->lineno = -1;
	}
	else
	{
		if (lbi->current_lb)
		{
			lbi->lineno -= 1;

			lbi->current_lb_rowno -= 1;
			if (lbi->current_lb_rowno >= 0)
				return true;

			if (lbi->current_lb->prev)
			{
				lbi->current_lb = lbi->current_lb->prev;
				lbi->current_lb_rowno = LINEBUFFER_LINES - 1;

				return true;
			}
		}
	}

	lbi->current_lb = NULL;
	lbi->current_lb_rowno = 0;

	return false;
}

/*
 * Move on next line in line buffer. Returns false, when there
 * is not valid line in buffer.
 */
bool
lbi_next(LineBufferIter *lbi)
{
	if (lbi->order_map)
	{
		if (lbi->lineno + 1 < lbi->order_map_items)
		{
			MappedLine *mpl;

			lbi->lineno += 1;

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
init_slbi_ddesc(SimpleLineBufferIter *slbi, DataDesc *desc)
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

		/*
		 * one line should be available every time. The possibility
		 * is checked before
		 */
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

/*
 * Free all lines stored in line buffer. An argument is data desc,
 * because first chunk of line buffer is owned by data desc.
 */
void
lb_free(DataDesc *desc)
{
	LineBuffer   *lb = &desc->rows;
	LineBuffer   *next;
	int		i;

	while (lb)
	{
		for (i = 0; i < lb->nrows; i++)
			free(lb->rows[i]);

		free(lb->lineinfo);
		next = lb->next;

		if (lb != &desc->rows)
			free(lb);

		lb = next;
	}
}

/*
 * Print all lines to stream
 */
void
lb_print_all_ddesc(DataDesc *desc, FILE *f)
{
	SimpleLineBufferIter slbi, *_slbi;

	_slbi = init_slbi_ddesc(&slbi, desc);

	while (_slbi)
	{
		char	   *line;
		int			res;

		_slbi = slbi_get_line_next(_slbi, &line, NULL);

		res = fprintf(f, "%s\n", line);
		if (res < 0)
			break;
	}
}

const char *
getline_ddesc(DataDesc *desc, int pos)
{
	LineBufferIter lbi;
	char	   *result;

	init_lbi_ddesc(&lbi, desc, pos);
	if (lbi_get_line(&lbi, &result, NULL, NULL))
		return result;

	return NULL;
}
