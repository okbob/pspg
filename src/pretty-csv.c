/*-------------------------------------------------------------------------
 *
 * pretty-csv.c
 *	  import and formatting csv and tsv documents
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pretty-csv.c
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>
#include <libgen.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pspg.h"
#include "unicode.h"

#ifndef offsetof
#define offsetof(type, field)	((long) &((type *)0)->field)
#endif							/* offsetof */

typedef struct
{
	char	   *buffer;
	int			processed;
	int			used;
	int			size;
	int			maxfields;
	int			starts[1024];		/* start of first char of column (in bytes) */
	int			sizes[1024];		/* lenght of chars of column (in bytes) */
	long int	digits[1024];		/* number of digits, used for format detection */
	long int	tsizes[1024];		/* size of column in bytes, used for format detection */
	int			firstdigit[1024];	/* rows where first char is digit */
	size_t		widths[1024];			/* column's display width */
	bool		multilines[1024];		/* true if column has multiline row */
	bool		hidden[1024];
} LinebufType;

typedef struct
{
	char	   *buffer;
	int			used;
	int			size;
	int			free;
	LineBuffer *linebuf;
	bool		force8bit;
	int			flushed_rows;		/* number of flushed rows */
	int			maxbytes;
	bool		printed_headline;
} PrintbufType;

typedef struct
{
	int			border;
	char		linestyle;
	bool		double_header;
	char		header_mode;
	bool		ignore_short_rows;
} PrintConfigType;

/*
 * Add new row to LineBuffer
 */
static void
pb_flush_line(PrintbufType *printbuf)
{
	char	   *line;

	if (printbuf->linebuf->nrows == 1000)
	{
		LineBuffer *nb = smalloc2(sizeof(LineBuffer), "serialize csv output");

		memset(nb, 0, sizeof(LineBuffer));

		printbuf->linebuf->next = nb;
		nb->prev = printbuf->linebuf;
		printbuf->linebuf = nb;
	}

	line = smalloc2(printbuf->used + 1, "serialize csv output");
	memcpy(line, printbuf->buffer, printbuf->used);
	line[printbuf->used] = '\0';

	printbuf->linebuf->rows[printbuf->linebuf->nrows++] = line;

	if (printbuf->used > printbuf->maxbytes)
		printbuf->maxbytes = printbuf->used;

	printbuf->used = 0;
	printbuf->free = printbuf->size;

	printbuf->flushed_rows += 1;
}

static void
pb_write(PrintbufType *printbuf, const char *str, int size)
{
	if (printbuf->free < size)
	{
		printbuf->size += 10 * 1024;
		printbuf->free += 10 * 1024;

		printbuf->buffer = realloc(printbuf->buffer, printbuf->size);
		if (!printbuf->buffer)
			leave("out of memory while serialize csv output");
	}

	memcpy(printbuf->buffer + printbuf->used, str, size);
	printbuf->used += size;
	printbuf->free -= size;
}

static void
pb_writes(PrintbufType *printbuf, const char *str)
{
	pb_write(printbuf, str, strlen(str));
}


static void
pb_write_repeat(PrintbufType *printbuf, int n, const char *str, int size)
{
	bool	need_realloc = false;

	while (printbuf->free < size * n)
	{
		printbuf->size += 10 * 1024;
		printbuf->free += 10 * 1024;

		need_realloc = true;
	}

	if (need_realloc)
	{
		printbuf->buffer = realloc(printbuf->buffer, printbuf->size);
		if (!printbuf->buffer)
			leave("out of memory while serialize csv output");
	}

	while (n--)
	{
		memcpy(printbuf->buffer + printbuf->used, str, size);
		printbuf->used += size;
		printbuf->free -= size;
	}
}

static void
pb_writes_repeat(PrintbufType *printbuf, int n,  const char *str)
{
	pb_write_repeat(printbuf, n, str, strlen(str));
}


static void
pb_putc(PrintbufType *printbuf, char c)
{
	if (printbuf->free < 1)
	{
		printbuf->size += 10 * 1024;
		printbuf->free += 10 * 1024;

		printbuf->buffer = realloc(printbuf->buffer, printbuf->size);
		if (!printbuf->buffer)
			leave("out of memory while serialize csv output");
	}

	printbuf->free -= 1;
	printbuf->buffer[printbuf->used++] = c;
}

static void
pb_puts(PrintbufType *printbuf, char *str)
{
	pb_write(printbuf, str, strlen(str));
}

static void
pb_putc_repeat(PrintbufType *printbuf, int n, int c)
{
	bool	need_realloc = false;

	while (printbuf->free < n)
	{
		printbuf->size += 10 * 1024;
		printbuf->free += 10 * 1024;

		need_realloc = true;
	}

	if (need_realloc)
	{
		printbuf->buffer = realloc(printbuf->buffer, printbuf->size);
		if (!printbuf->buffer)
			leave("out of memory while serialize csv output");
	}

	memset(printbuf->buffer + printbuf->used, c, n);
	printbuf->used += n;
	printbuf->free -= n;

	printbuf->free -= n;
}

static void
pb_print_vertical_header(PrintbufType *printbuf, PrintDataDesc *pdesc, PrintConfigType *pconfig, char pos)
{
	int		border = pconfig->border;
	bool	double_header = pconfig->double_header;
	char	linestyle = pconfig->linestyle;

	const char *lhchr;				/* left header char */
	const char *mhchr;				/* middle header char */
	const char *rhchr;				/* right header char */
	const char *hhchr;				/* horizont header char */

	int		i;

	/* leave fast when there is nothing to work */
	if ((border == 0 || border == 1) && (pos != 'm'))
		return;

	if (linestyle == 'a')
	{
		if (pos == 'm' &&double_header)
		{
			lhchr = ":";
			mhchr = ":";
			rhchr = ":";
			hhchr = "=";
		}
		else
		{
			lhchr = "+";
			mhchr = "+";
			rhchr = "+";
			hhchr = "-";
		}
	}
	else
	{
		/* linestyle = 'u' */
		if (pos == 'm')
		{
			if (double_header)
			{
				lhchr = "\342\225\236";		/* ╞ */
				mhchr = "\342\225\252";		/* ╪ */
				rhchr = "\342\225\241";		/* ╡ */
				hhchr = "\342\225\220";		/* ═ */
			}
			else
			{
				lhchr = "\342\224\234";		/* ├ */
				mhchr = "\342\224\274";		/* ┼ */
				rhchr = "\342\224\244";		/* ┤ */
				hhchr = "\342\224\200";		/* ─ */
			}
		}
		else if (pos == 't')
		{
			lhchr = "\342\224\214";		/* ┌ */
			mhchr = "\342\224\254";		/* ┬ */
			rhchr = "\342\224\220";		/* ┐ */
			hhchr = "\342\224\200";		/* ─ */
		}
		else
		{
			/* pos == 'b' */
			lhchr = "\342\224\224";		/* └ */
			mhchr = "\342\224\264";		/* ┴ */
			rhchr = "\342\224\230";		/* ┘ */
			hhchr = "\342\224\200";		/* ─ */
		}
	}

	if (border == 2)
	{
		pb_writes(printbuf, lhchr);
		pb_writes(printbuf, hhchr);
	}
	else if (border == 1)
	{
		pb_writes(printbuf, hhchr);
	}

	for (i = 0; i < pdesc->nfields; i++)
	{
		if (i > 0)
		{
			if (border == 0)
			{
				pb_write(printbuf, " ", 1);
			}
			else
			{
				pb_writes(printbuf, hhchr);
				pb_writes(printbuf, mhchr);
				pb_writes(printbuf, hhchr);
			}
		}

		pb_writes_repeat(printbuf, pdesc->widths[i], hhchr);
	}

	if (border == 2)
	{
		pb_writes(printbuf, hhchr);
		pb_writes(printbuf, rhchr);
	}
	else if (border == 1)
	{
		pb_writes(printbuf, hhchr);
	}
	else if (border == 0 && pdesc->multilines[pdesc->nfields - 1])
	{
			pb_write(printbuf, " ", 1);
	}

	pb_flush_line(printbuf);
}

/*
 * Header detection - simple heuristic, when first row has all text fields
 * and second rows has any numeric field, then csv has header.
 */
static bool
is_header(RowBucketType *rb)
{
	RowType	   *row;
	int		i;

	if (rb->nrows < 2)
		return false;

	row = rb->rows[0];

	for (i = 0; i < row->nfields; i++)
	{
		if (row->fields[i][0] == '\0')
			return false;
		if (isdigit((row->fields[i])[0]))
			return false;
	}

	row = rb->rows[1];

	for (i = 0; i < row->nfields; i++)
	{
		if (row->fields[i][0] == '\0')
			return true;
		if (isdigit((row->fields[i])[0]))
			return true;
	}

	return false;
}

static char *
pb_put_line(char *str, bool multiline, PrintbufType *printbuf)
{
	char   *nextline = NULL;

	if (multiline)
	{
		char   *ptr = str;
		int		size = 0;
		int		chrl;

		while (*ptr)
		{
			if (*ptr == '\n')
			{
				nextline = ptr + 1;
				break;
			}

			chrl = printbuf->force8bit ? 1 : utf8charlen(*ptr);
			size += chrl;
			ptr += chrl;
		}

		pb_write(printbuf, str, size);
	}
	else
		pb_puts(printbuf, str);

	return nextline;
}

/*
 * Print formatted data loaded inside RowBuckets
 */
static void
pb_print_rowbuckets(PrintbufType *printbuf,
				   RowBucketType *rb,
				   PrintConfigType *pconfig,
				   PrintDataDesc *pdesc,
				   char *title)
{
	bool	is_last_column_multiline = pdesc->multilines[pdesc->nfields - 1];
	int		last_column_num = pdesc->nfields - 1;
	int		printed_rows = 0;
	char	linestyle = pconfig->linestyle;
	int		border = pconfig->border;
	char	buffer[20];

	printbuf->printed_headline = false;
	printbuf->flushed_rows = 0;
	printbuf->maxbytes = 0;

	if (title)
	{
		pb_puts(printbuf, title);
		pb_flush_line(printbuf);
	}

	pb_print_vertical_header(printbuf, pdesc, pconfig, 't');

	while (rb)
	{
		int		i;

		for (i = 0; i < rb->nrows; i++)
		{
			int		j;
			bool	isheader = false;
			RowType	   *row;
			bool	more_lines = true;
			bool	multiline = rb->multilines[i];
			char	   *fields[1024];
			int			multiline_lineno;

			/* skip broken rows */
			if (pconfig->ignore_short_rows && rb->rows[i]->nfields != pdesc->nfields_all)
				continue;

			multiline_lineno = 0;
			row = rb->rows[i];

			while (more_lines)
			{
				more_lines = false;

				if (border == 2)
				{
					if (linestyle == 'a')
						pb_write(printbuf, "| ", 2);
					else
						pb_write(printbuf, "\342\224\202 ", 4);
				}
				else if (border == 1)
					pb_write(printbuf, " ", 1);

				isheader = printed_rows == 0 ? pdesc->has_header : false;

				for (j = 0; j < pdesc->nfields; j++)
				{
					int		width;
					int		spaces;
					char   *field;
					bool	_more_lines = false;

					if (j > 0)
					{
						if (border != 0)
						{
							if (linestyle == 'a')
								pb_write(printbuf, "| ", 2);
							else
								pb_write(printbuf, "\342\224\202 ", 4);
						}
					}

					if (pdesc->columns_map[j] < row->nfields)
					{
						if (multiline_lineno == 0)
						{
							field = row->fields[pdesc->columns_map[j]];
							fields[j] = NULL;
						}
						else
							field = fields[j];
					}
					else
						field = NULL;

					if (field && *field != '\0')
					{
						bool	left_align = pdesc->types[j] != 'd';

						if (printbuf->force8bit)
						{
							if (multiline)
							{
								char   *ptr = field;
								width = 0;

								while (*ptr)
								{
									if (*ptr++ == '\n')
									{
										more_lines |= true;
										break;
									}
									width += 1;
								}
							}
							else
								width = strlen(field);
						}
						else
						{
							if (multiline)
							{
								width = utf_string_dsplen_multiline(field, SIZE_MAX, &_more_lines, true, NULL, NULL);
								more_lines |= _more_lines;
							}
							else
								width = utf_string_dsplen(field, SIZE_MAX);
						}

						spaces = pdesc->widths[j] - width;

						/*
						 * The display width can be canculated badly when labels or
						 * displayed string has some special or invisible chars. Here
						 * is simple ugly fix - the number of spaces cannot be negative.
						 */
						if (spaces < 0)
							spaces = 0;

						/* left spaces */
						if (isheader)
							pb_putc_repeat(printbuf, spaces / 2, ' ');
						else if (!left_align)
							pb_putc_repeat(printbuf, spaces, ' ');

						if (multiline)
							fields[j] = pb_put_line(field, multiline, printbuf);
						else
							(void) pb_put_line(field, multiline, printbuf);

						/* right spaces */
						if (isheader)
							pb_putc_repeat(printbuf, spaces - (spaces / 2), ' ');
						else if (left_align)
							pb_putc_repeat(printbuf, spaces, ' ');
					}
					else
						pb_putc_repeat(printbuf, pdesc->widths[j], ' ');

					if (_more_lines)
					{
						if (linestyle == 'a')
							pb_putc(printbuf, '+');
						else
							pb_write(printbuf, "\342\206\265", 3);
					}
					else
					{
						if (border != 0 || j < last_column_num || is_last_column_multiline)
							pb_putc(printbuf, ' ');
					}
				}

				if (border == 2)
				{
					if (linestyle == 'a')
						pb_write(printbuf, "|", 2);
					else
						pb_write(printbuf, "\342\224\202", 3);
				}

				pb_flush_line(printbuf);

				if (isheader)
				{
					pb_print_vertical_header(printbuf, pdesc, pconfig, 'm');
					printbuf->printed_headline = true;
				}

				printed_rows += 1;
				multiline_lineno += 1;
			}
		}

		rb = rb->next_bucket;
	}

	pb_print_vertical_header(printbuf, pdesc, pconfig, 'b');

	snprintf(buffer, 20, "(%d rows)", printed_rows - (printbuf->printed_headline ? 1 : 0));
	pb_puts(printbuf, buffer);
	pb_flush_line(printbuf);
}

/*
 * Try to detect column type and prepare all data necessary for printing
 */
static void
prepare_pdesc(RowBucketType *rb, LinebufType *linebuf, PrintDataDesc *pdesc, PrintConfigType *pconfig)
{
	int				i;

	pdesc->nfields_all = linebuf->maxfields;
	pdesc->nfields = 0;

	/* copy data from linebuf */
	for (i = 0; i < linebuf->maxfields; i++)
	{
		if (!linebuf->hidden[i])
		{
			pdesc->widths[pdesc->nfields] = linebuf->widths[i];
			pdesc->multilines[pdesc->nfields] = linebuf->multilines[i];
			pdesc->columns_map[pdesc->nfields++] = i;
		}
	}

	if (pconfig->header_mode == 'a')
		pdesc->has_header = is_header(rb);
	else
		pdesc->has_header = pconfig->header_mode == '+';

	/* try to detect types from numbers of digits */
	for (i = 0; i < pdesc->nfields; i++)
	{
		if ((linebuf->tsizes[pdesc->columns_map[i]] == 0 && linebuf->digits[pdesc->columns_map[i]] > 0) ||
			(linebuf->firstdigit[pdesc->columns_map[i]] > 0 && linebuf->processed - 1 == 1))
			pdesc->types[i] = 'd';
		else if ((((double) linebuf->firstdigit[pdesc->columns_map[i]] / (double) (linebuf->processed - 1)) > 0.8)
			&& (((double) linebuf->digits[pdesc->columns_map[i]] / (double) linebuf->tsizes[pdesc->columns_map[i]]) > 0.5))
			pdesc->types[i] = 'd';
		else
			pdesc->types[i] = 'a';
	}
}

/*
 * Save append one char to linebuffer
 */
inline static void
append_char(LinebufType *linebuf, char c)
{
	if (linebuf->used >= linebuf->size)
	{
		linebuf->size += linebuf->size < (10 * 1024) ? linebuf->size  : (10 * 1024);
		linebuf->buffer = realloc(linebuf->buffer, linebuf->size);

		if (!linebuf->buffer)
			leave("out of memory while read csv or tsv data");
	}

	linebuf->buffer[linebuf->used++] = c;
}

/*
 * Save string to linebuffer
 */
inline static void
append_str(LinebufType *linebuf, char *str)
{
	int		l = strlen(str);

	if (linebuf->used + l >= linebuf->size)
	{
		linebuf->size += linebuf->size < (10 * 1024) ? linebuf->size  : (10 * 1024);
		linebuf->buffer = realloc(linebuf->buffer, linebuf->size);

		if (!linebuf->buffer)
			leave("out of memory while read csv or tsv data");
	}

	while (*str)
		linebuf->buffer[linebuf->used++] = *str++;
}


/*
 * Ensure dynamicaly allocated structure is valid every time.
 */
static inline RowBucketType *
prepare_RowBucket(RowBucketType *rb)
{
	/* move row from linebuf to rowbucket */
	if (rb->nrows >= 1000)
	{
		RowBucketType *new = smalloc2(sizeof(RowBucketType), "import csv data");

		new->nrows = 0;
		new->allocated = true;
		new->next_bucket = NULL;

		rb->next_bucket = new;
		rb = new;
	}

	return rb;
}

/*
 * Calculate width of columns
 */
static void
postprocess_fields(int nfields,
				   RowType *row,
				   LinebufType *linebuf,
				   bool force8bit,
				   bool ignore_short_rows,
				   bool reduced_sizes,			/* the doesn't calculate ending zero */
				   bool *is_multiline_row)
{
	bool		malformed;
	size_t		width;
	int		i;

	if (ignore_short_rows)
		malformed = linebuf->maxfields > 0 && nfields != linebuf->maxfields;
	else
		malformed = false;

	*is_multiline_row = false;

	for (i = 0; i < nfields; i++)
	{
		long int	digits = 0;
		long int	total = 0;
		bool		multiline;

		/* don't calculate width for hidden columns */
		if (linebuf->hidden[i])
			continue;

		if (force8bit)
		{
			size_t		cw = 0;
			char	    *ptr = row->fields[i];

			width = 0;

			while (*ptr)
			{
				if (isdigit(*ptr))
					digits += 1;
				else if (*ptr != '-' && *ptr != ' ' && *ptr != ':')
					total += 1;

				if (*ptr++ == '\n')
				{
					multiline = true;
					width = cw > width ? cw : width;
					cw = 0;
				}
				else
					cw++;
			}

			width = cw > width ? cw : width;
		}
		else
			width = utf_string_dsplen_multiline(row->fields[i],
												linebuf->sizes[i] - (reduced_sizes ? 0 : 1),
												&multiline,
												false,
												&digits,
												&total);

		/* skip first possible header row */
		if (linebuf->processed > 0)
		{
			linebuf->tsizes[i] += total;
			linebuf->digits[i] += digits;

			if (isdigit(*row->fields[i]))
				linebuf->firstdigit[i]++;
		}

		if (!malformed)
		{
			if (width > linebuf->widths[i])
				linebuf->widths[i] = width;

			*is_multiline_row |= multiline;
			linebuf->multilines[i] |= multiline;
		}
	}

	if (nfields > linebuf->maxfields)
		linebuf->maxfields = nfields;

	if (!malformed)
		linebuf->processed += 1;
}

/*
 * Appends fields to rows without complete set of fields.
 * New fields holds null str.
 */
static void
postprocess_rows(RowBucketType *rb,
				 LinebufType *linebuf,
				 bool force8bit,
				 char *nullstr)
{
	size_t		nullstr_size = strlen(nullstr);
	size_t		nullstr_width = force8bit ? strlen(nullstr) : (size_t) utf_string_dsplen(nullstr, strlen(nullstr));

	while (rb)
	{
		int		i;

		for (i = 0; i < rb->nrows; i++)
		{
			RowType *oldrow = rb->rows[i];

			if (linebuf->maxfields > 0 && linebuf->maxfields > oldrow->nfields)
			{
				int		j;
				int		newsize = 0;
				char   *locbuf;
				RowType *newrow;

				/* calculate rows size and check width for new columns */
				for (j = 0; j < linebuf->maxfields; j++)
				{
					if (j < oldrow->nfields)
					{
						char   *str = oldrow->fields[j];

						newsize += str ? strlen(str) + 1 : 0;
					}
					else
					{
						newsize += nullstr_size + 1;
						if (linebuf->widths[j] < nullstr_width)
							linebuf->widths[j] = nullstr_width;
					}
				}

				locbuf = smalloc2(newsize, "postprocess csv or tsv data");
				newrow = smalloc2(offsetof(RowType, fields) + (linebuf->maxfields * sizeof(char*)),
								 "postprocess csv or tsv data");
				newrow->nfields = linebuf->maxfields;

				for (j = 0; j < newrow->nfields; j++)
				{
					if (j < oldrow->nfields)
					{
						if (oldrow->fields[j])
						{
							strcpy(locbuf, oldrow->fields[j]);
							newrow->fields[j] = locbuf;
							locbuf += strlen(newrow->fields[j]) + 1;
						}
						else
							newrow->fields[j] = NULL;
					}
					else
					{
						strcpy(locbuf, nullstr);
						newrow->fields[j] = locbuf;
						locbuf += nullstr_size + 1;
					}
				}

				if (oldrow->nfields > 0)
					free(oldrow->fields[0]);

				free(oldrow);

				rb->rows[i] = newrow;
			}
		}

		rb = rb->next_bucket;
	}
}

static bool
mark_hidden_columns(LinebufType *linebuf,
					RowType *row,
					int nfields,
					Options *opts)
{
	char	   *names;
	char	   *endnames;
	char	   *ptr;
	int		i;
	bool	result = false;

	/* prepare list of hidden columns */
	names = sstrdup(opts->csv_skip_columns_like);

	endnames = names + strlen(names);
	ptr = names;
	while (*ptr)
	{
		/* space is separator between words */
		if (*ptr == ' ')
			*ptr = '\0';
		ptr += 1;
	}

	for (i = 0; i < nfields; i++)
	{
		ptr = names;
		while (ptr < endnames)
		{
			if (*ptr)
			{
				size_t		len = strlen(ptr);

				if (*ptr == '^')
				{
					if (strncmp(row->fields[i], ptr + 1, len - 1) == 0)
					{
						linebuf->hidden[i] = true;
						result = true;
					}
				}
				else if (ptr[len - 1] == '$')
				{
					size_t		len2 = strlen(row->fields[i]);

					if (len2 > (len - 1) &&
						strncmp(row->fields[i] + len2 - len + 1, ptr, len - 1) == 0)
					{
						linebuf->hidden[i] = true;
						result = true;
					}
				}
				else if (strstr(row->fields[i], ptr))
				{
					linebuf->hidden[i] = true;
					result = true;
				}
			}

			ptr += strlen(ptr) + 1;
		}
	}

	free(names);

	return result;
}

/*
 * Read tsv format from ifile
 */
static void
read_tsv(RowBucketType *rb,
		 LinebufType *linebuf,
		 bool force8bit,
		 FILE *ifile,
		 bool ignore_short_rows,
		 Options *opts)
{
	bool	closed = false;
	int		size = 0;
	int		nfields = 0;
	int		c;
	int		nullstr_size = strlen(opts->nullstr);

	c = fgetc(ifile);
	do
	{
		if (c == '\r')
			goto next_char;

		if (c != EOF && c != '\n')
		{
			bool	backslash = false;
			bool	translated = false;

			if (c == '\\')
			{
				backslash = true;

				c = fgetc(ifile);
				if (c != EOF)
				{
					/* NULL */
					if (c == 'N')
					{
						append_str(linebuf, opts->nullstr);
						size += nullstr_size;
						goto next_char;
					}
					else if (c ==  't')
					{
						c = '\t';
						translated = true;
					}
					else if (c == 'n')
					{
						c = '\n';
						translated = true;
					}
					else if (c == '\\')
					{
						translated = true;
					}
				}
			}

			if (c != EOF)
			{
				if (c == '\t' && !translated)
				{
					append_char(linebuf, '\0');
					linebuf->sizes[nfields++] = size + 1;
					size = 0;
				}
				else
				{
					if (backslash && !translated)
					{
						append_char(linebuf, '\\');
						size +=1;
					}

					append_char(linebuf, c);
					size += 1;
				}
			}
		}
		else
		{
			int		i;

			if (linebuf->used > 0)
			{
				char   *locbuf;
				RowType	   *row;
				bool	multiline = false;

				append_char(linebuf, '\0');
				linebuf->sizes[nfields++] = size + 1;

				rb = prepare_RowBucket(rb);

				locbuf = smalloc2(linebuf->used, "import tsv data");
				memcpy(locbuf, linebuf->buffer, linebuf->used);

				row = smalloc2(offsetof(RowType, fields) + (nfields * sizeof(char*)), "import csv data");
				row->nfields = nfields;

				for (i = 0; i < nfields; i++)
				{
					row->fields[i] = locbuf;
					locbuf += linebuf->sizes[i];
				}

				if (linebuf->processed == 0 && opts->csv_skip_columns_like)
					mark_hidden_columns(linebuf, row, nfields, opts);

				postprocess_fields(nfields,
								   row,
								   linebuf,
								   force8bit,
								   ignore_short_rows,
								   false,
								   &multiline);

				rb->multilines[rb->nrows] = multiline;
				rb->rows[rb->nrows++] = row;

				linebuf->processed += 1;
			}

			nfields = 0;
			linebuf->used = 0;
			size = 0;

			closed = c == EOF;
		}

next_char:
		c = fgetc(ifile);

	} while (!closed);

	/* append nullstr to missing columns */
	if (nullstr_size > 0 && !ignore_short_rows)
		postprocess_rows(rb, linebuf, force8bit, opts->nullstr);
}

static void
read_csv(RowBucketType *rb,
		 LinebufType *linebuf,
		 char sep,
		 bool force8bit,
		 FILE *ifile,
		 bool ignore_short_rows,
		 Options *opts)
{
	bool	skip_initial = true;
	bool	closed = false;
	bool	found_string = false;
	int		first_nw = 0;
	int		last_nw = 0;
	int		pos = 0;
	int		nfields = 0;
	int		instr = false;			/* true when csv string is processed */
	int		c;
	int		nullstr_size = strlen(opts->nullstr);

	c = fgetc(ifile);
	do
	{
		/* ignore ^M */
		if (c == '\r')
			goto next_char;

		if (c != EOF && (c != '\n' || instr))
		{
			int		l;

			if (skip_initial)
			{
				if (c == ' ')
					goto next_char;

				skip_initial = false;
				last_nw = first_nw;
			}

			if (c == '"')
			{
				if (instr)
				{
					int		c2 = fgetc(ifile);

					if (c2 == '"')
					{
						/* double double quotes */
						append_char(linebuf, c);
						pos = pos + 1;
					}
					else
					{
						/* start of end of string */
						ungetc(c2, ifile);
						instr = false;
					}
				}
				else
				{
					instr = true;
					found_string = true;
				}
			}
			else
			{
				append_char(linebuf, c);
				pos = pos + 1;
			}

			if (sep == -1 && !instr)
			{
				/*
				 * Automatic separator detection - now it is very simple, first win.
				 * Can be enhanced in future by more sofisticated mechanism.
				 */
				if (c == ',')
					sep = ',';
				else if (c == ';')
					sep = ';';
				else if (c == '|')
					sep = '|';
			}

			if (sep != -1 && c == sep && !instr)
			{
				if (nfields >= 1024)
					leave("too much columns");

				if (skip_initial)
					leave("internal error - unexpected value of variable: \"skip_initial\"");

				if (last_nw - first_nw > 0 || found_string || nullstr_size == 0)
				{
					linebuf->sizes[nfields] = last_nw - first_nw;
					linebuf->starts[nfields++] = first_nw;
				}
				else
				{
					/* append null string */
					linebuf->sizes[nfields] = nullstr_size;
					linebuf->starts[nfields++] = pos;

					append_str(linebuf, opts->nullstr);
					pos += nullstr_size;
				}

				skip_initial = true;
				found_string = false;
				first_nw = pos;
			}
			else if (instr || c != ' ')
			{
				last_nw = pos;
			}

			l = force8bit ? 1 : utf8charlen(c);
			if (l > 1)
			{
				int		i;

				/* read other chars */
				for (i = 1; i < l; i++)
				{
					c = fgetc(ifile);
					if (c == EOF)
					{
						log_row("unexpected quit, broken unicode char");
						break;
					}

					append_char(linebuf, c);
					pos = pos + 1;
				}
				last_nw = pos;
			}
		}
		else
		{
			char	   *locbuf;
			RowType	   *row;
			int			i;
			int			data_size;
			bool		multiline;

			if (c == '\n')
			{
				/* try to process \nEOF as one symbol */
				c = fgetc(ifile);
				if (c != EOF)
					ungetc(c, ifile);
			}

			if (!skip_initial && (last_nw - first_nw > 0 || found_string || nullstr_size == 0))
			{
				linebuf->sizes[nfields] = last_nw - first_nw;
				linebuf->starts[nfields++] = first_nw;
			}
			else if (nullstr_size > 0 &&
					  (nfields > 1 || (nfields == 0 && linebuf->maxfields == 1)
								   || (nfields == 0 && linebuf->processed == 0)))
			{
				/* append null string */
				linebuf->sizes[nfields] = nullstr_size;
				linebuf->starts[nfields++] = pos;

				append_str(linebuf, opts->nullstr);
				pos += nullstr_size;
			}
			else
			{
				linebuf->sizes[nfields] = 0;
				linebuf->starts[nfields++] = -1;
			}

			if (!linebuf->used)
				goto next_row;

			rb = prepare_RowBucket(rb);

			data_size = 0;
			for (i = 0; i < nfields; i++)
				if (!linebuf->hidden[i])
					data_size += linebuf->sizes[i] + 1;

			locbuf = smalloc2(data_size, "import csv data");
			memset(locbuf, 0, data_size);

			row = smalloc2(offsetof(RowType, fields) + (nfields * sizeof(char*)), "import csv data");
			row->nfields = nfields;

			multiline = false;

			for (i = 0; i < nfields; i++)
			{
				if (!linebuf->hidden[i])
				{
					row->fields[i] = locbuf;

					if (linebuf->sizes[i] > 0)
						memcpy(locbuf, linebuf->buffer + linebuf->starts[i], linebuf->sizes[i]);

					locbuf[linebuf->sizes[i]] = '\0';
					locbuf += linebuf->sizes[i] + 1;
				}
				else
					row->fields[i] = NULL;
			}

			if (linebuf->processed == 0 && opts->csv_skip_columns_like)
				mark_hidden_columns(linebuf, row, nfields, opts);

			postprocess_fields(nfields,
							   row,
							   linebuf,
							   force8bit,
							   ignore_short_rows,
							   true,
							   &multiline);

			rb->multilines[rb->nrows] = multiline;
			rb->rows[rb->nrows++] = row;

next_row:

			linebuf->used = 0;
			nfields = 0;

			linebuf->processed += 1;

			skip_initial = true;
			first_nw = 0;
			last_nw = 0;
			pos = 0;

			closed = c == EOF;
		}

next_char:

		if (!closed)
			c = fgetc(ifile);

	}
	while (!closed);

	/* append nullstr to missing columns */
	if (nullstr_size > 0 && !ignore_short_rows)
		postprocess_rows(rb, linebuf, force8bit, opts->nullstr);
}

/*
 * Read external unformatted data (csv or result of some query
 *
 */
bool
read_and_format(Options *opts, DataDesc *desc, StateData *state)
{
	LinebufType		linebuf;
	RowBucketType	rowbuckets, *rb;
	PrintConfigType	pconfig;
	PrintbufType	printbuf;
	PrintDataDesc	pdesc;

	memset(desc, 0, sizeof(DataDesc));

	state->errstr = NULL;
	state->_errno = 0;

	desc->title[0] = '\0';
	desc->title_rows = 0;
	desc->border_top_row = -1;
	desc->border_head_row = -1;
	desc->border_bottom_row = -1;
	desc->first_data_row = -1;
	desc->last_data_row = -1;
	desc->is_expanded_mode = false;
	desc->headline_transl = NULL;
	desc->cranges = NULL;
	desc->columns = 0;
	desc->footer_row = -1;
	desc->alt_footer_row = -1;
	desc->is_pgcli_fmt = false;
	desc->namesline = NULL;
	desc->order_map = NULL;
	desc->total_rows = 0;
	desc->multilines_already_tested = false;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	desc->rows.prev = NULL;

	memset(&linebuf, 0, sizeof(LinebufType));

	linebuf.buffer = malloc(10 * 1024);
	linebuf.used = 0;
	linebuf.size = 10 * 1024;

	pconfig.linestyle = (opts->force_ascii_art || opts->force8bit) ? 'a' : 'u';
	pconfig.border = opts->border_type;
	pconfig.double_header = opts->double_header;
	pconfig.header_mode = opts->csv_header;
	pconfig.ignore_short_rows = opts->ignore_short_rows;

	rowbuckets.allocated = false;

	if (opts->query)
	{
		if (!pg_exec_query(opts, &rowbuckets, &pdesc, &state->errstr))
			return false;
	}
	else if (opts->csv_format)
	{
		read_csv(&rowbuckets,
				 &linebuf,
				 opts->csv_separator,
				 opts->force8bit,
				 state->fp, opts->ignore_short_rows,
				 opts);

		prepare_pdesc(&rowbuckets, &linebuf, &pdesc, &pconfig);
	}
	else if (opts->tsv_format)
	{
		read_tsv(&rowbuckets,
				 &linebuf,
				 opts->force8bit,
				 state->fp,
				 opts->ignore_short_rows,
				 opts);

		prepare_pdesc(&rowbuckets, &linebuf, &pdesc, &pconfig);
	}

	/* reuse allocated memory */
	printbuf.buffer = linebuf.buffer;
	printbuf.size = linebuf.size;
	printbuf.free = linebuf.size;
	printbuf.used = 0;
	printbuf.linebuf = &desc->rows;
	printbuf.force8bit = opts->force8bit;

	/* init other printbuf fields */
	printbuf.printed_headline = false;
	printbuf.flushed_rows = 0;
	printbuf.maxbytes = 0;

	/* sanitize ptr */
	linebuf.buffer = NULL;
	linebuf.size = 0;

	pb_print_rowbuckets(&printbuf, &rowbuckets, &pconfig, &pdesc, NULL);

	desc->border_type = pconfig.border;
	desc->linestyle = pconfig.linestyle;
	desc->maxbytes = printbuf.maxbytes;

	if (printbuf.printed_headline)
	{
		int		headline_rowno;

		headline_rowno = pconfig.border == 2 ? 2 : 1;

		if (desc->rows.nrows > headline_rowno)
		{
			desc->namesline = desc->rows.rows[headline_rowno - 1];

			desc->border_head_row = headline_rowno;
			desc->headline = desc->rows.rows[headline_rowno];
			desc->headline_size = strlen(desc->headline);

			if (opts->force8bit)
				desc->headline_char_size = desc->headline_size;
			else
				desc->headline_char_size = desc->maxx = utf_string_dsplen(desc->headline, SIZE_MAX);

			desc->first_data_row = desc->border_head_row + 1;

			desc->maxy = printbuf.flushed_rows - 1;
			desc->total_rows = printbuf.flushed_rows;
			desc->last_row = desc->total_rows - 1;

			desc->footer_row = desc->last_row;
			desc->footer_rows = 1;

			if (pconfig.border == 2)
			{
				desc->border_top_row = 0;
				desc->last_data_row = desc->total_rows - 2 - 1;
				desc->border_bottom_row = desc->last_data_row + 1;
			}
			else
			{
				desc->border_top_row = -1;
				desc->border_bottom_row = -1;
				desc->last_data_row = desc->total_rows - 1 - 1;
			}
		}
	}
	else
	{
		char	*ptr;
		int		i;

		/*
		 * When we have not headline. We know structure, so we can
		 * "translate" headline here (generate translated headline).
		 */
		desc->columns = linebuf.maxfields;
		desc->cranges = smalloc2(desc->columns * sizeof(CRange), "prepare metadata");
		memset(desc->cranges, 0, desc->columns * sizeof(CRange));
		desc->headline_transl = smalloc2(desc->maxbytes + 3, "prepare metadata");

		ptr = desc->headline_transl;

		if (pconfig.border == 1)
			*ptr++ = 'd';
		else if (pconfig.border == 2)
		{
			*ptr++ = 'L';
			*ptr++ = 'd';
		}

		for (i = 0; i < linebuf.maxfields; i++)
		{
			int		width = linebuf.widths[i];

			desc->cranges[i].name_offset = -1;
			desc->cranges[i].name_size = -1;

			if (i > 0)
			{
				if (pconfig.border > 0)
				{
					*ptr++ = 'd';
					*ptr++ = 'I';
					*ptr++ = 'd';
				}
				else
					*ptr++ = 'I';
			}

			while (width--)
			{
				*ptr++ = 'd';
			}
		}

		if (pconfig.border == 1)
			*ptr++ = 'd';
		else if (pconfig.border == 2)
		{
			*ptr++ = 'd';
			*ptr++ = 'R';
		}

		*ptr = '\0';
		desc->headline_char_size = strlen(desc->headline_transl);

		desc->cranges[0].xmin = 0;
		ptr = desc->headline_transl;
		i = 0;

		while (*ptr)
		{
			if (*ptr++ == 'I')
			{
				desc->cranges[i].xmax = ptr - desc->headline_transl - 1;
				desc->cranges[++i].xmin = ptr - desc->headline_transl - 1;
			}
		}

		desc->cranges[i].xmax = desc->headline_char_size - 1;

		desc->maxy = printbuf.flushed_rows - 1;
		desc->total_rows = printbuf.flushed_rows;
		desc->last_row = desc->total_rows - 1;

		desc->footer_row = desc->last_row;
		desc->footer_rows = 1;

		if (pconfig.border == 2)
		{
			desc->first_data_row = 0;
			desc->border_top_row = 0;
			desc->border_head_row = 0;
			desc->last_data_row = desc->total_rows - 2 - 1;
			desc->border_bottom_row = desc->last_data_row + 1;
		}
		else
		{
			desc->first_data_row = 0;
			desc->border_top_row = -1;
			desc->border_head_row = -1;

			desc->border_bottom_row = -1;
			desc->last_data_row = desc->total_rows - 1 - 1;
		}
	}

	free(printbuf.buffer);

	/* release row buckets */
	rb = &rowbuckets;

	while (rb)
	{
		RowBucketType	*nextrb;
		int		i;

		for (i = 0; i < rb->nrows; i++)
		{
			RowType	   *r = rb->rows[i];

			/* only first field holds allocated string */
			if (r->nfields > 0)
				free(r->fields[0]);
			free(r);
		}

		nextrb = rb->next_bucket;
		if (rb->allocated)
			free(rb);
		rb = nextrb;
	}

	return true;
}
