/*-------------------------------------------------------------------------
 *
 * pretty-csv.c
 *	  import and formatting csv documents
 *
 * Portions Copyright (c) 2017-2019 Pavel Stehule
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
	int		nfields;
	char   *fields[];
} RowType;

typedef struct _rowBucketType
{
	int			nrows;
	RowType	   *rows[1000];
	bool		multilines[1000];
	bool		allocated;
	struct _rowBucketType *next_bucket;
} RowBucketType;

typedef struct
{
	char	   *buffer;
	int			processed;
	int			used;
	int			size;
	int			maxfields;
	int			starts[1024];		/* start of first char of column (in bytes) */
	int			sizes[1024];		/* lenght of chars of column (in bytes) */
	int			widths[1024];		/* display width of column */
	char		multilines[1024];	/* true, when column has some multiline chars */
} LinebufType;

typedef struct
{
	int			border;
	char		linestyle;
	char		separator;
} ConfigType;

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

static void *
smalloc(int size, char *debugstr)
{
	char *result;

	result = malloc(size);
	if (!result)
	{
		if (debugstr)
			fprintf(stderr, "out of memory while %s\n", debugstr);
		else
			fprintf(stderr, "out of memory\n");
		exit(1);
	}

	return result;
}

/*
 * Add new row to LineBuffer
 */
static void
pb_flush_line(PrintbufType *printbuf)
{
	char	   *line;

	if (printbuf->linebuf->nrows == 1000)
	{
		LineBuffer *nb = smalloc(sizeof(LineBuffer), "serialize csv output");

		memset(nb, 0, sizeof(LineBuffer));

		printbuf->linebuf->next = nb;
		nb->prev = printbuf->linebuf;
		printbuf->linebuf = nb;
	}

	line = smalloc(printbuf->used + 1, "serialize csv output");
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
pb_write(PrintbufType *printbuf, char *str, int size)
{
	if (printbuf->free < size)
	{
		printbuf->size += 10 * 1024;
		printbuf->free += 10 * 1024;

		printbuf->buffer = realloc(printbuf->buffer, printbuf->size);
		if (!printbuf->buffer)
		{
			fprintf(stderr, "out of memory while serialize csv output\n");
			exit(1);
		}
	}

	memcpy(printbuf->buffer + printbuf->used, str, size);
	printbuf->used += size;
	printbuf->free -= size;
}

static void
pb_write_repeat(PrintbufType *printbuf, int n,  char *str, int size)
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
		{
			fprintf(stderr, "out of memory while serialize csv output\n");
			exit(1);
		}
	}

	while (n--)
	{
		memcpy(printbuf->buffer + printbuf->used, str, size);
		printbuf->used += size;
		printbuf->free -= size;
	}
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
		{
			fprintf(stderr, "out of memory while serialize csv output\n");
			exit(1);
		}
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
		{
			fprintf(stderr, "out of memory while serialize csv output\n");
			exit(1);
		}
	}

	while (n--)
		printbuf->buffer[printbuf->used++] = c;

	printbuf->free -= n;
}

static void
pb_print_vertical_header(PrintbufType *printbuf, LinebufType *linebuf, ConfigType *config, char pos)
{
	int		i;
	int		border = config->border;

	if (config->linestyle == 'a')
	{
		if ((border == 0 || border == 1) && (pos != 'm'))
			return;

		if (border == 2)
			pb_write(printbuf, "+-", 2);
		else if (border == 1)
			pb_write(printbuf, "-", 2);

		for (i = 0; i < linebuf->maxfields; i++)
		{
			if (i > 0)
			{
				if (border == 0)
					pb_write(printbuf, " ", 1);
				else
					pb_write(printbuf, "-+-", 3);
			}

			pb_putc_repeat(printbuf, linebuf->widths[i], '-');
		}

		if (border == 2)
			pb_write(printbuf, "-+", 2);
		else if (border == 1)
			pb_write(printbuf, "-", 1);
		else if (border == 0 && linebuf->multilines[linebuf->maxfields - 1])
			pb_write(printbuf, " ", 1);
	}
	else if (config->linestyle == 'u')
	{
		if ((border == 0 || border == 1) && (pos != 'm'))
			return;

		if (border == 2)
		{
			if (pos == 't')
				pb_write(printbuf, "\342\224\214", 3);
			else if (pos == 'm')
				pb_write(printbuf, "\342\224\234", 3);
			else
				pb_write(printbuf, "\342\224\224", 3);

			pb_write(printbuf, "\342\224\200", 3);
		}
		else if (border == 1)
			pb_write(printbuf, "\342\224\200", 3);

		for (i = 0; i < linebuf->maxfields; i++)
		{
			if (i > 0)
			{
				if (border == 0)
					pb_write(printbuf, " ", 1);
				else
				{
					pb_write(printbuf, "\342\224\200", 3);
					if (pos == 't')
						pb_write(printbuf, "\342\224\254", 3);
					else if (pos == 'm')
						pb_write(printbuf, "\342\224\274", 3);
					else
						pb_write(printbuf, "\342\224\264", 3);

					pb_write(printbuf, "\342\224\200", 3);
				}
			}

			pb_write_repeat(printbuf, linebuf->widths[i], "\342\224\200", 3);
		}

		if (border == 2)
		{
			pb_write(printbuf, "\342\224\200", 3);
			if (pos == 't')
				pb_write(printbuf, "\342\224\220", 3);
			else if (pos == 'm')
				pb_write(printbuf, "\342\224\244", 3);
			else
				pb_write(printbuf, "\342\224\230", 3);
		}
		else if (border == 1)
			pb_write(printbuf, "\342\224\200", 3);
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
pb_print_csv(PrintbufType *printbuf,
			 RowBucketType *rb,
			 LinebufType *linebuf,
			 ConfigType *config,
			 char *title)
{
	bool	last_multiline_column = linebuf->multilines[linebuf->maxfields - 1];
	int		last_column = linebuf->maxfields - 1;
	int		printed_rows = 0;
	char	linestyle = config->linestyle;
	int		border = config->border;
	char	buffer[20];
	RowBucketType   *root_rb = rb;

	printbuf->printed_headline = false;
	printbuf->flushed_rows = 0;
	printbuf->maxbytes = 0;

	if (title)
	{
		pb_puts(printbuf, title);
		pb_flush_line(printbuf);
	}

	pb_print_vertical_header(printbuf, linebuf, config, 't');

	while (rb)
	{
		int		i;

		for (i = 0; i < rb->nrows; i++)
		{
			int		j;
			bool	isheader = false;
			RowType	   *row;
			bool	free_row;
			bool	more_lines = true;
			bool	multiline = rb->multilines[i];

			/*
			 * For multilines we can modify pointers so do copy now
			 */
			if (multiline)
			{
				RowType	   *source = rb->rows[i];
				int			size;

				size = offsetof(RowType, fields) + (source->nfields * sizeof(char*));

				row = smalloc(size, "RowType");
				memcpy(row, source, size);

				free_row = true;
			}
			else
			{
				row = rb->rows[i];
				free_row = false;
			}

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

				isheader = printed_rows == 0 ? is_header(root_rb) : false;

				for (j = 0; j < row->nfields; j++)
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

					field = row->fields[j];

					if (field && *field != '\0')
					{
						bool	_isdigit = isdigit(field[0]);

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
								width = utf_string_dsplen_multiline(field, SIZE_MAX, &_more_lines, true);
								more_lines |= _more_lines;
							}
							else
								width = utf_string_dsplen(field, SIZE_MAX);
						}

						spaces = linebuf->widths[j] - width;

						/* left spaces */
						if (isheader)
							pb_putc_repeat(printbuf, spaces / 2, ' ');
						else if (_isdigit)
							pb_putc_repeat(printbuf, spaces, ' ');

						if (multiline)
							row->fields[j] = pb_put_line(row->fields[j], multiline, printbuf);
						else
							(void) pb_put_line(row->fields[j], multiline, printbuf);

						/* right spaces */
						if (isheader)
							pb_putc_repeat(printbuf, spaces - (spaces / 2), ' ');
						else if (!_isdigit)
							pb_putc_repeat(printbuf, spaces, ' ');
					}
					else
						pb_putc_repeat(printbuf, linebuf->widths[j], ' ');

					if (_more_lines)
					{
						if (linestyle == 'a')
							pb_putc(printbuf, '+');
						else
							pb_write(printbuf, "\342\206\265", 3);
					}
					else
					{
						if (border != 0 || j < last_column || last_multiline_column)
							pb_putc(printbuf, ' ');
					}
				}

				for (j = row->nfields; j < linebuf->maxfields; j++)
				{
					bool	addspace;

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

					addspace = border != 0 || j < last_column || last_multiline_column;

					pb_putc_repeat(printbuf, linebuf->widths[j] + (addspace ? 1 : 0), ' ');
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
					pb_print_vertical_header(printbuf, linebuf, config, 'm');
					printbuf->printed_headline = true;
				}

				printed_rows += 1;
			}

			if (free_row)
				free(row);
		}

		rb = rb->next_bucket;
	}

	pb_print_vertical_header(printbuf, linebuf, config, 'b');

	snprintf(buffer, 20, "(%d rows)", linebuf->processed - (printbuf->printed_headline ? 1 : 0));
	pb_puts(printbuf, buffer);
	pb_flush_line(printbuf);
}

static void
read_csv(RowBucketType *rb,
		 LinebufType *linebuf,
		 ConfigType *config,
		 bool force8bit,
		 FILE *ifile)
{
	bool	skip_initial = true;
	bool	closed = false;
	int		first_nw = 0;
	int		last_nw = 0;
	int		pos = 0;
	int		nfields = 0;
	int		instr = false;			/* true when csv string is processed */
	int		c;

	c = fgetc(ifile);
	do
	{
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

			if (linebuf->used >= linebuf->size)
			{
				linebuf->size += linebuf->size < (10 * 1024) ? linebuf->size  : (10 * 1024);
				linebuf->buffer = realloc(linebuf->buffer, linebuf->size);

				/* for debug purposes */
				memset(linebuf->buffer + linebuf->used, 0, linebuf->size - linebuf->used);
			}

			if (c == '"')
			{
				if (instr)
				{
					int		c2 = fgetc(ifile);

					if (c2 == '"')
					{
						/* double double quotes */
						linebuf->buffer[linebuf->used++] = c;
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
					instr = true;
			}
			else
			{
				linebuf->buffer[linebuf->used++] = c;
				pos = pos + 1;
			}

			if (config->separator == -1 && !instr)
			{
				/*
				 * Automatic separator detection - now it is very simple, first win.
				 * Can be enhanced in future by more sofisticated mechanism.
				 */
				if (c == ',')
					config->separator = ',';
				else if (c == ';')
					config->separator = ';';
				else if (c == '|')
					config->separator = '|';
			}

			if (config->separator != -1 && c == config->separator && !instr)
			{
				if (nfields >= 1024)
				{
					fprintf(stderr, "too much columns");
					exit(1);
				}

				if (!skip_initial)
				{
					linebuf->sizes[nfields] = last_nw - first_nw;
					linebuf->starts[nfields++] = first_nw;
				}
				else
				{
					linebuf->sizes[nfields] = 0;
					linebuf->starts[nfields++] = -1;
				}

				skip_initial = true;
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

				/* read othe chars */
				for (i = 1; i < l; i++)
				{
					c = fgetc(ifile);
					if (c == EOF)
					{
						fprintf(stderr, "unexpected quit, broken unicode char\n");
						break;
					}

					linebuf->buffer[linebuf->used++] = c;
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

			if (!skip_initial)
			{
				linebuf->sizes[nfields] = last_nw - first_nw;
				linebuf->starts[nfields++] = first_nw;
			}
			else
			{
				linebuf->sizes[nfields] = 0;
				linebuf->starts[nfields++] = -1;
			}

			/* move row from linebuf to rowbucket */
			if (rb->nrows >= 1000)
			{
				RowBucketType *new = smalloc(sizeof(RowBucketType), "import csv data");

				new->nrows = 0;
				new->allocated = true;
				new->next_bucket = NULL;

				rb->next_bucket = new;
				rb = new;
			}

			if (!linebuf->used)
				goto next_row;

			data_size = 0;
			for (i = 0; i < nfields; i++)
				data_size += linebuf->sizes[i] + 1;

			locbuf = smalloc(data_size, "import csv data");
			memset(locbuf, 0, data_size);

			row = smalloc(offsetof(RowType, fields) + (nfields * sizeof(char*)), "import csv data");
			row->nfields = nfields;

			multiline = false;

			for (i = 0; i < nfields; i++)
			{
				int		width;
				bool	_multiline;

				row->fields[i] = locbuf;

				if (linebuf->sizes[i] > 0)
					memcpy(locbuf, linebuf->buffer + linebuf->starts[i], linebuf->sizes[i]);

				locbuf[linebuf->sizes[i]] = '\0';
				locbuf += linebuf->sizes[i] + 1;

				if (force8bit)
				{
					int		cw = 0;
					char   *ptr = row->fields[i];

					width = 0;

					while (*ptr)
					{
						if (*ptr++ == '\n')
						{
							_multiline = true;
							if (cw > width)
								width = cw;

							cw = 0;
						}
						else
							cw++;
					}
				}
				else
				{
					width = utf_string_dsplen_multiline(row->fields[i], linebuf->sizes[i], &_multiline, false);
				}

				if (width > linebuf->widths[i])
					linebuf->widths[i] = width;

				multiline |= _multiline;
				linebuf->multilines[i] |= _multiline;
			}

			if (nfields > linebuf->maxfields)
				linebuf->maxfields = nfields;

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

		c = fgetc(ifile);
	}
	while (!closed);
}

void
read_and_format_csv(FILE *fp, Options *opts, DataDesc *desc)
{
	LinebufType		linebuf;
	RowBucketType	rowbuckets, *rb;
	ConfigType		config;
	PrintbufType	printbuf;

	/* safe reset */
	desc->filename[0] = '\0';

	memset(desc, 0, sizeof(DataDesc));

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
	desc->cranges = NULL;
	desc->columns = 0;
	desc->footer_row = -1;
	desc->alt_footer_row = -1;
	desc->is_pgcli_fmt = false;
	desc->namesline = NULL;
	desc->order_map = NULL;
	desc->total_rows = 0;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	desc->rows.prev = NULL;

	memset(&linebuf, 0, sizeof(LinebufType));

	linebuf.buffer = malloc(10 * 1024);
	linebuf.used = 0;
	linebuf.size = 10 * 1024;

	config.separator = opts->csv_separator;

	config.linestyle = (opts->force_ascii_art || opts->force8bit) ? 'a' : 'u';
	config.border = opts->csv_border_type;

	read_csv(&rowbuckets, &linebuf, &config, opts->force8bit, fp);

	/* reuse allocated memory */
	printbuf.buffer = linebuf.buffer;
	printbuf.size = linebuf.size;
	printbuf.free = linebuf.size;
	printbuf.used = 0;
	printbuf.linebuf = &desc->rows;
	printbuf.force8bit = opts->force8bit;

	linebuf.buffer = NULL;		/* sanitize ptr */
	linebuf.size = 0;

	pb_print_csv(&printbuf, &rowbuckets, &linebuf, &config, NULL);

	desc->border_type = config.border;
	desc->linestyle = config.linestyle;
	desc->maxbytes = printbuf.maxbytes;

	if (printbuf.printed_headline)
	{
		int		headline_rowno;

		headline_rowno = config.border == 2 ? 2 : 1;

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

			if (config.border == 2)
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
		desc->cranges = smalloc(desc->columns * sizeof(CRange), "prepare metadata");
		memset(desc->cranges, 0, desc->columns * sizeof(CRange));
		desc->headline_transl = smalloc(desc->maxbytes + 3, "prepare metadata");

		ptr = desc->headline_transl;

		if (config.border == 1)
			*ptr++ = 'd';
		else if (config.border == 2)
		{
			*ptr++ = 'L';
			*ptr++ = 'd';
		}

		for (i = 0; i < linebuf.maxfields; i++)
		{
			int		width = linebuf.widths[i];

			desc->cranges[i].name_pos = -1;
			desc->cranges[i].name_size = -1;

			if (i > 0)
			{
				if (config.border > 0)
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

		if (config.border == 1)
			*ptr++ = 'd';
		else if (config.border == 2)
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

		if (config.border == 2)
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
}
