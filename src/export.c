/*-------------------------------------------------------------------------
 *
 * export.c
 *	  a routines for exporting data
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/export.c
 *
 *-------------------------------------------------------------------------
 */

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>

#include "pspg.h"
#include "commands.h"
#include "unicode.h"

/*
 * Ensure correct formatting of CSV value. Can returns
 * malloc ed string when value should be quoted.
 */
static char *
csv_format(char *str, int *slen,
		   bool empty_string_is_null,
		   char *nullstr, int nullstrlen)
{
	char   *ptr = str;
	char   *result;
	bool	needs_quoting = false;
	int		_slen;

	/* Detect NULL symbol ∅ */
	if (nullstrlen > 0 &&
		*slen == nullstrlen &&
		strncmp(str, nullstr, nullstrlen) == 0)
	{
		*slen = 0;
		return NULL;
	}


	if (use_utf8 &&
		*slen == 3 && strncmp(str, "\342\210\205", 3) == 0)
	{
		*slen = 0;
		return NULL;
	}

	if (*slen == 0)
	{
		if (empty_string_is_null)
			return NULL;

		*slen = 2;
		return sstrdup("\"\"");
	}

	_slen = *slen;
	while (_slen > 0)
	{
		int		size;

		if (*ptr == '"' || *ptr == ',' ||
			*ptr == '\t' || *ptr == '\r' || *ptr == '\n')
		{
			needs_quoting = true;
			break;
		}

		size = charlen(ptr);

		ptr += size;
		_slen -= size;
	}

	if (!needs_quoting)
		return str;

	result = ptr = smalloc2(*slen * 2 + 2 + 1,
							"CSV format output buffer allocation");

	*ptr++ = '"';

	_slen = *slen;
	*slen = 1;
	while (_slen > 0)
	{
		int		size = charlen(ptr);

		if (*str == '"')
		{
			*ptr++ = '"';
			*slen += 1;
		}

		_slen -= size;
		*slen += size;

		while (size--)
			*ptr++ = *str++;
	}

	*ptr++ = '"';
	*ptr = '\0';
	*slen += 1;

	return result;
}

/*
 * Ensure correct format for SQL identifier
 */
static char *
quote_sql_identifier(char *str, int *slen)
{
	bool	needs_quoting = false;
	char   *ptr, *result;
	int		_slen;

	/* it is quoted already? */
	if (*str == '"')
		return str;

	if (!str || *str == '\0' || *slen == 0)
		return str;

	if (*str != ' ' && (*str < 'a' || *str > 'z'))
		needs_quoting = true;
	else
	{
		ptr = str;
		_slen = *slen;

		while (*ptr && _slen > 0)
		{
			int		size = charlen(ptr);

			if (!((*ptr >= 'a' && *ptr <= 'z') ||
				(*ptr >= '0' && *ptr <= '9') ||
				 *ptr == '_'))
			{
				needs_quoting = true;
				break;
			}

			_slen -= size;
			ptr += size;
		}
	}

	if (!needs_quoting)
		return str;

	result = ptr = smalloc2(*slen * 2 + 2 + 1,
							"SQL identifier output buffer allocation");

	*ptr++ = '"';

	_slen = *slen;
	*slen = 1;

	while (_slen > 0)
	{
		int		size = charlen(ptr);

		if (*str == '"')
			*ptr++ = '"';

		_slen -= size;
		*slen += size;

		while (size--)
			*ptr++ = *str++;
	}

	*ptr++ = '"';
	*ptr = '\0';
	*slen += 1;

	return result;
}

static char *
quote_sql_literal(char *str,
				  int *slen,
				  bool empty_string_is_null,
				  char *nullstr,
				  int nullstrlen)
{
	char   *ptr = str;
	char   *result;
	int		_slen = *slen;
	bool	has_dot = false;

	bool	needs_quoting = false;

	if (*slen == 0)
	{
		if (empty_string_is_null)
		{
			*slen = 4;
			return sstrdup("NULL");
		}
		else
		{
			*slen = 2;
			return sstrdup("''");
		}
	}

	if (*slen == 4 &&
		(strncmp(str, "NULL", *slen) == 0 ||
		 strncmp(str, "null", *slen) == 0))
		return str;

	if (use_utf8 &&
		*slen == 3 && strncmp(str, "\342\210\205", 3) == 0)
	{
		*slen = 4;
		return sstrdup("NULL");
	}

	if (nullstrlen > 0 &&
		*slen == nullstrlen &&
		strncmp(str, nullstr, *slen) == 0)
	{
		*slen = 4;
		return sstrdup("NULL");

	}

	while (*ptr && _slen > 0)
	{
		int		size = charlen(ptr);

		if (*ptr == '.')
		{
			if (has_dot)
			{
				needs_quoting = true;
				break;
			}
			else
				has_dot = true;
		}
		else if (!(*ptr >= '0' && *ptr <= '9'))
		{
			needs_quoting = true;
			break;
		}

		ptr += size;
		_slen -= size;
	}

	if (!needs_quoting)
		return str;

	result = ptr = smalloc2(*slen * 2 + 2 + 1,
							"SQL literal output buffer allocation");

	*ptr++ = '\'';

	_slen = *slen;
	*slen = 1;

	while (_slen > 0)
	{
		int		size = charlen(ptr);

		if (*str == '\'')
			*ptr++ = '\'';

		_slen -= size;
		*slen += size;

		while (size--)
			*ptr++ = *str++;
	}

	*ptr++ = '\'';
	*ptr = '\0';
	*slen += 1;

	return result;
}

/*
 * Iterator over data string with format specified by headline
 */
typedef struct
{
	char	   *row;
	char	   *headline;
	int			xpos;
} FmtLineIter;

static char *
next_char(FmtLineIter *iter,
		  char *typ,
		  int *size,
		  int *width,
		  int *xpos)
{
	char   *result = iter->row;

	if (!iter->row || !iter->headline)
		return NULL;

	if (*iter->row == '\0' || *iter->headline == '\n')
		return NULL;

	*typ = *(iter->headline);
	*xpos = iter->xpos;

	*size = charlen(result);
	*width = dsplen(result);

	iter->row += *size;
	iter->headline += *width;
	iter->xpos += *width;

	return result;
}

typedef struct
{
	FILE	   *fp;
	ClipboardFormat format;
	int			xmin;
	int			xmax;
	bool		copy_line_extended;
	char	   *table_name;
	int			columns;

	bool		empty_string_is_null;

	char	   *nullstr;
	int			nullstrlen;

	int			colno;
	char	  **colnames;
	ExtStr	   *lines;
	char		linestyle;

	int			nlines;			/* for debug purposes */
} ExportState;

/*
 * Export one segment of format (decoration or field) to output file.
 */
static bool
process_item(ExportState *expstate,
			 char typ, char *field, int size,
			 int xpos, bool is_colname,
			 bool has_continue_mark,
			 bool has_continue_mark2)
{
	if (typ == 'd')
	{
		/* Ignore items outer to selected range */
		if (expstate->xmin != -1 &&
			  (xpos <= expstate->xmin || expstate->xmax <= xpos))
		return true;

		if (has_continue_mark)
		{
			ExtStr *estr = &expstate->lines[expstate->colno];

			if (!estr->data)
				InitExtStr(estr);

			ExtStrAppendLine(estr, field, size,
							 expstate->linestyle,
							 has_continue_mark,
							 has_continue_mark2);

			expstate->colno += 1;

			return true;
		}
		else if (expstate->lines)
		{
			ExtStr *estr = &expstate->lines[expstate->colno];

			if (estr->len > 0)
			{
				ExtStrAppendLine(estr, field, size,
								 expstate->linestyle,
								 has_continue_mark,
								 has_continue_mark2);

				size = ExtStrTrimEnd(estr,
									 expstate->format == CLIPBOARD_FORMAT_TSVC);

				field = estr->data;

				/* Reset doesn't release memory */
				ResetExtStr(estr);
			}
		}
	}

	if (INSERT_FORMAT_TYPE(expstate->format))
	{
		errno = 0;

		if (typ == 'N' && !is_colname && !has_continue_mark)
		{
			expstate->nlines += 1;

			if (expstate->format == CLIPBOARD_FORMAT_INSERT)
				fputs(");\n", expstate->fp);
			else
			{
				fprintf(expstate->fp, ");\t\t -- %d. %s\n",
					expstate->colno, expstate->colnames[expstate->colno - 1]);
			}
		}
		else if (typ == 'd')
		{
			char   *_field;

			if (is_colname)
			{
				if (!expstate->colnames)
				{
					expstate->colnames = smalloc(sizeof(char *) * expstate->columns);
					memset(expstate->colnames, 0, sizeof(char *) * expstate->columns);
				}

				field = trim_str(field, &size);
				_field = quote_sql_identifier(field, &size);

				if (!_field)
					expstate->colnames[expstate->colno] = sstrdup("");
				else if (_field != field)
					expstate->colnames[expstate->colno] = _field;
				else
					expstate->colnames[expstate->colno] = sstrndup(field, size);

				expstate->colno += 1;
			}
			else
			{
				if (expstate->colno == 0)
				{
					errno = 0;

					fputs("INSERT INTO ", expstate->fp);
					fputs(expstate->table_name, expstate->fp);

					if (expstate->colnames)
					{
						fputc('(', expstate->fp);

						if (expstate->format == CLIPBOARD_FORMAT_INSERT)
						{
							int		i;
							bool	is_first = true;

							for (i = 0; i < expstate->columns; i++)
							{
								if (expstate->colnames[i])
								{
									if (!is_first)
										fputs(", ", expstate->fp);
									else
										is_first = false;

									fputs(expstate->colnames[i], expstate->fp);
								}
								else
									break;
							}

							fputc(')', expstate->fp);
						}
						else
						{
							int			indent_spaces;
							int			columns = 0;
							int			loc_colno = 0;
							int			i;

							if (use_utf8)
								indent_spaces = utf_string_dsplen(expstate->table_name, INT_MAX) + 1 + 12;
							else
								indent_spaces = strlen(expstate->table_name) + 1 + 12;

							for (i = 0; i < expstate->columns; i++)
							{
								if (expstate->colnames[i])
									columns += 1;
								else
									break;
							}

							for (i = 0; i < expstate->columns; i++)
							{
								if (expstate->colnames[i])
								{
									if (loc_colno > 0)
										fprintf(expstate->fp, "%*.s", indent_spaces, "");

									fputs(expstate->colnames[i], expstate->fp);

									if (loc_colno < columns - 1)
										fprintf(expstate->fp, ",\t\t -- %d.\n", loc_colno + 1);
									else
										fprintf(expstate->fp, ")\t\t -- %d.\n", loc_colno + 1);

									loc_colno += 1;
								}
								else
									break;
							}
						}
					}

					if (expstate->format == CLIPBOARD_FORMAT_INSERT)
						fputs(" VALUES(", expstate->fp);
					else
						fputs("   VALUES(", expstate->fp);
				}

				if (expstate->format == CLIPBOARD_FORMAT_INSERT)
				{
					if (expstate->colno > 0)
						fputs(", ", expstate->fp);
				}
				else
				{
					if (expstate->colno > 0)
					{
						fprintf(expstate->fp, ",\t\t -- %d. %s\n",
									expstate->colno, expstate->colnames[expstate->colno - 1]);
						fputs("          ", expstate->fp);
					}
				}

				field = trim_str(field, &size);
				_field = quote_sql_literal(field,
										   &size,
										   expstate->empty_string_is_null,
										   expstate->nullstr,
										   expstate->nullstrlen);

				fwrite(_field, size, 1, expstate->fp);

				expstate->colno += 1;

				if (_field != field)
					free(_field);
			}
		}
	}

	/*
	 * Export in formatted text format
	 */
	else if (expstate->format == CLIPBOARD_FORMAT_TEXT)
	{
		errno = 0;

		if (typ != 'N')
		{
			if (typ == 'I' || typ == 'd')
			{
				/* Ignore items outer to selected range */
				if (expstate->xmin != -1 &&
					(xpos <= expstate->xmin ||
					 expstate->xmax <= xpos))
				return true;
			}

			fwrite(field, size, 1, expstate->fp);
		}
		else
		{
			fputc('\n', expstate->fp);
			expstate->nlines += 1;
		}

	}

	else if (expstate->format == CLIPBOARD_FORMAT_PIPE_SEPARATED)
	{
		errno = 0;

		if (!is_colname)
		{
			if (typ != 'N')
			{
				if (typ == 'I' || typ == 'd')
				{
					/* Ignore items outer to selected range */
					if (expstate->xmin != -1 &&
						(xpos <= expstate->xmin ||
						 expstate->xmax <= xpos))
					return true;

					if (typ == 'd')
					{
						field = trim_str(field, &size);
						fwrite(field, size, 1, expstate->fp);
					}
					else
						fputs(" | ", expstate->fp);
				}
			}
			else
			{
				fputc('\n', expstate->fp);
				expstate->nlines += 1;
			}
		}
	}

	/*
	 * Export in CSV or TSV format
	 */
	else if (DSV_FORMAT_TYPE(expstate->format))
	{
		if (expstate->format == CLIPBOARD_FORMAT_SQL_VALUES && is_colname)
			return true;

		if (typ == 'N' &&
			!expstate->copy_line_extended && 
			!has_continue_mark)
		{
			errno = 0;
			fputc('\n', expstate->fp);
			expstate->nlines += 1;

		}
		else
		{
			if (typ == 'd')
			{
				int saved_errno = 0;
				char   *_field;

				field = trim_str(field, &size);

				if (expstate->format == CLIPBOARD_FORMAT_SQL_VALUES)
					_field = quote_sql_literal(field,
											   &size,
											   expstate->empty_string_is_null,
											   expstate->nullstr,
											   expstate->nullstrlen);
				else

					_field = csv_format(field, &size,
										expstate->empty_string_is_null,
										expstate->nullstr,
										expstate->nullstrlen);

				if (expstate->copy_line_extended && is_colname)
				{

					if (!expstate->colnames)
					{
						expstate->colnames = smalloc(sizeof(char *) * expstate->columns);
						memset(expstate->colnames, 0, sizeof(char *) * expstate->columns);
					}

					if (!_field)
						expstate->colnames[expstate->colno] = sstrdup("");
					else if (_field != field)
						expstate->colnames[expstate->colno] = _field;
					else
						expstate->colnames[expstate->colno] = sstrndup(field, size);

					expstate->colno += 1;

					return true;
				}

				errno = 0;

				if (!expstate->copy_line_extended)
				{
					if (expstate->colno > 0)
					{

						if (expstate->format == CLIPBOARD_FORMAT_CSV ||
							expstate->format == CLIPBOARD_FORMAT_SQL_VALUES)
							fputc(',', expstate->fp);
						else if (expstate->format == CLIPBOARD_FORMAT_TSVC)
							fputc('\t', expstate->fp);
					}

					if (_field && !errno)
					{
						errno = 0;
						fwrite(_field, size, 1, expstate->fp);
					}
				}
				else
					fprintf(expstate->fp, "%s,%.*s\n",
								  expstate->colnames[expstate->colno],
								  size, _field);

				saved_errno = errno;

				expstate->colno += 1;

				if (_field != field)
					free(_field);

				errno = saved_errno;
			}
		}
	}

	if (errno != 0)
	{
		current_state->_errno = errno;
		format_error("%s", strerror(errno));
		log_row("Cannot write (%s)", current_state->errstr);

		return false;
	}

	return true;
}

/*
 * Exports data to defined stream in requested format.
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
			char *table_name,
			PspgCommand cmd,
			ClipboardFormat format)
{
	LineBufferIter	lbi;
	LineBufferMark	lbm;

	int		rn;
	char   *rowstr;
	bool	print_header = true;
	bool	print_footer = true;
	bool	print_border = true;
	bool	print_header_line = true;
	bool	save_column_names = false;
	bool	has_selection;

	int		debug_read_rows = 0;
	int		debug_processed_rows = 0;

	int		min_row = desc->first_data_row;
	int		max_row = desc->last_row;

	bool	prev_continuation_mark = false;

	bool	isok = true;

	ExportState expstate;

	expstate.format = format;
	expstate.fp = fp;
	expstate.empty_string_is_null = opts->empty_string_is_null;
	expstate.nullstr = opts->nullstr;
	expstate.nullstrlen = opts->nullstr ? strlen(opts->nullstr) : 0;
	expstate.xmin = -1;
	expstate.xmax = -1;
	expstate.table_name = NULL;
	expstate.colnames = NULL;
	expstate.lines = NULL;
	expstate.columns = desc->columns;
	expstate.copy_line_extended = (cmd == cmd_CopyLineExtended);
	expstate.linestyle = desc->linestyle;
	expstate.nlines = 0;

	current_state->errstr = NULL;

	has_selection =
		((scrdesc->selected_first_row != -1 && scrdesc->selected_rows > 0 ) ||
		 (scrdesc->selected_first_column != -1 && scrdesc->selected_columns > 0));

	if (cmd == cmd_CopyLineExtended && !DSV_FORMAT_TYPE(format))
		format = CLIPBOARD_FORMAT_CSV;

	if (cmd == cmd_CopyLineExtended ||
		INSERT_FORMAT_TYPE(format))
	{
		if (INSERT_FORMAT_TYPE(format))
		{
			int		slen = strlen(table_name);

			expstate.table_name = quote_sql_identifier(table_name, &slen);
		}

		save_column_names = true;
	}

	if (cmd == cmd_CopyLine ||
		cmd == cmd_CopyLineExtended ||
		(cmd == cmd_Copy && !opts->no_cursor && !has_selection))
	{
		min_row = max_row = cursor_row + desc->first_data_row;
		print_footer = false;
	}

	if ((cmd == cmd_Copy && opts->vertical_cursor) ||
		cmd == cmd_CopyColumn)
	{
		expstate.xmin = desc->cranges[cursor_column - 1].xmin;
		expstate.xmax = desc->cranges[cursor_column - 1].xmax;

		print_footer = false;
	}

	/* copy value from cross of vertical and horizontal cursor */
	if (cmd == cmd_Copy && !opts->no_cursor && opts->vertical_cursor)
	{
		print_header = false;
		print_header_line = false;
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

	if (cmd == cmd_CopyMarkedLines || cmd == cmd_CopySearchedLines)
		print_footer = false;

	if ((cmd == cmd_Copy && has_selection) ||
		cmd == cmd_CopySelected)
	{
		if (scrdesc->selected_first_row != -1)
		{
			min_row = scrdesc->selected_first_row + desc->first_data_row;
			max_row = min_row + scrdesc->selected_rows - 1;
		}

		if (scrdesc->selected_first_column != -1 && scrdesc->selected_columns > 0)
		{
			expstate.xmin = scrdesc->selected_first_column;
			expstate.xmax = expstate.xmin + scrdesc->selected_columns - 1;
		}

		if (min_row > desc->first_data_row || max_row < desc->last_data_row)
			print_footer = false;
	}

	if (format != CLIPBOARD_FORMAT_TEXT)
	{
		print_border = false;
		print_footer = false;
		print_header_line = false;
	}

	if (save_column_names)
		print_header = true;

	/*
	 * only when we export data in raw format and we export complete result, then
	 * we don't need to know multilines. Copy searched or marked lines doesn't support 
	 * multiline grouping too.
	 */
	if (!((format == CLIPBOARD_FORMAT_TEXT && cmd == cmd_CopyAllLines) ||
		  cmd == cmd_CopySearchedLines || cmd == cmd_CopyMarkedLines))
	{
		multilines_detection(desc);
		if (desc->has_multilines)
		{
			bool	prevline_continuation_mark = false;
			int		multiline_first_rn = 0;
			int	   *multiline_map = NULL;

			multiline_map = smalloc(desc->total_rows * sizeof(int));

			init_lbi_ddesc(&lbi, desc, 0);

			while (lbi_set_mark_next(&lbi, &lbm))
			{
				LineInfo *linfo;
				bool		continuation_mark;

				(void) lbm_get_line(&lbm, &rowstr, &linfo, &rn);

				continuation_mark = linfo && linfo->mask & LINEINFO_CONTINUATION;

				if (!prevline_continuation_mark && continuation_mark)
				{
					multiline_first_rn = rn;
					multiline_map[rn] = rn;
				}
				else
					multiline_map[rn] = prevline_continuation_mark ? multiline_first_rn : 0;

				prevline_continuation_mark = continuation_mark;
			}

			/* check min_row and max_row against multiline_map */
			if (min_row != desc->first_data_row)
			{
				int			new_min_row = multiline_map[min_row];

				if (new_min_row != 0)
				min_row = new_min_row;
			}

			if (max_row != desc->last_row)
			{
				int			first_rn = multiline_map[max_row];

				if (first_rn !=0)
				{
					int			i;

					for (i = max_row; i <= desc->last_row; i++)
					{
						if (multiline_map[i] == first_rn)
							max_row = i;
						else
							break;
					}
				}
			}

			free(multiline_map);

			expstate.lines = smalloc(desc->columns * sizeof(ExtStr));
		}
	}

	log_row("export: desc->first_data_row: %d, desc->last_data_row: %d",
			desc->first_data_row, desc->last_data_row);
	log_row("export: min_row: %d, max_row: %d", min_row, max_row);

	init_lbi_ddesc(&lbi, desc, 0);

	while (lbi_set_mark_next(&lbi, &lbm))
	{
		LineInfo *linfo;

		int		size, width;
		int		field_size;
		int		field_xpos;
		int		xpos;
		char   *field, *ptr, typ;
		FmtLineIter iter;
		bool	is_colname = false;
		bool	continuation_mark = false;

		(void) lbm_get_line(&lbm, &rowstr, &linfo, &rn);

		debug_read_rows += 1;

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
			if (cmd == cmd_CopySearchedLines)
			{
				/* force lineinfo setting */
				linfo = set_line_info(opts, scrdesc, desc, &lbm, rowstr);

				if (!linfo || ((linfo->mask & LINEINFO_FOUNDSTR) == 0))
					continue;
			}
		}
		else
		{
			is_colname = (rn != desc->border_top_row &&
						  rn != desc->border_bottom_row &&
						  rn != desc->border_head_row &&
						  rn <= desc->fixed_rows);

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

		iter.headline = desc->headline_transl;
		iter.row = rowstr;
		iter.xpos = 0;

		field = NULL; field_size = 0; field_xpos = -1;

		expstate.colno = 0;

		/* for text format we have not concate lines of multiline field */
		if (format != CLIPBOARD_FORMAT_TEXT)
			continuation_mark = linfo && linfo->mask & LINEINFO_CONTINUATION;

		debug_processed_rows += 1;

		/*
		 * line parser - separates fields on line
		 */
		while ((ptr = next_char(&iter, &typ, &size, &width, &xpos)))
		{
			if (typ == 'd')
			{
				if (!field)
					field = ptr;

				field_size += size;
				field_xpos = xpos;
				continue;
			}

			if (field)
			{
				isok = process_item(&expstate, 'd',
									field, field_size, field_xpos,
									is_colname,
									continuation_mark,
									prev_continuation_mark);
				if (!isok)
					goto exit_export;

				field = NULL; field_size = 0; field_xpos = -1;
			}

			isok = process_item(&expstate, typ,
								ptr, size, xpos,
								is_colname,
								continuation_mark,
								prev_continuation_mark);

			if (!isok)
				goto exit_export;
		}

		if (field)
		{
			isok = process_item(&expstate, 'd',
								field, field_size, field_xpos,
								is_colname,
								continuation_mark,
								prev_continuation_mark);
			if (!isok)
				goto exit_export;
		}

		isok = process_item(&expstate, 'N',
							NULL, 0, -1, is_colname,
							continuation_mark,
							prev_continuation_mark);
		if (!isok)
			goto exit_export;

		prev_continuation_mark = continuation_mark;
	}

exit_export:

	log_row("export: read rows: %d, procesed rows: %d", debug_read_rows, debug_processed_rows);

	if (expstate.colnames)
	{
		int		i;

		for (i = 0; i < expstate.columns; i++)
			free(expstate.colnames[i]);

		free(expstate.colnames);
	}

	if (expstate.table_name && expstate.table_name != table_name)
		free(expstate.table_name);

	if (expstate.lines)
	{
		int		i;

		for (i = 0; i < expstate.columns; i++)
			free(expstate.lines[i].data);

		free(expstate.lines);
	}

	log_row("exported %d rows with result %d", expstate.nlines, isok);

	return isok;
}
