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
#include <stdint.h>

#include "pspg.h"
#include "commands.h"
#include "unicode.h"

/*
 * Returns first and last nospace chars. These chars should be vertical decoration.
 */
static void
getdeco(char *str, char **ldeco, char **rdeco, int *ldecolen, int *rdecolen)
{
	char	   *start = str;
	char	   *stop = NULL;

	while (*str)
	{
		if (*str != ' ')
			stop = str;

		str += utf8charlen(*str);
	}

	*ldeco = start;
	*ldecolen = utf8charlen(*start);

	if (stop)
	{
		*rdeco = stop;
		*rdecolen = utf8charlen(*stop);
	}
	else
	{
		*rdeco = "x";
		*rdecolen = 1;
	}
}

/*
 * Return substring. When trim is true, then spaces from returned value are trimmed.
 */
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
 * Ensure correct formatting of CSV value. Can returns
 * malloc ed string when value should be quoted.
 */
static char *
csv_format(char *str, int *slen, bool force8bit)
{
	char   *ptr = str;
	char   *result;
	bool	needs_quoting = false;
	int		_slen;

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

		size = force8bit ? 1 : utf8charlen(*ptr);

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
		int		size = force8bit ? 1 : utf8charlen(*ptr);

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

/*
 * Ensure correct format for SQL identifier
 */
static char *
quote_sql_identifier(char *str, int *slen, bool force8bit)
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
			int		size = force8bit ? 1 : utf8charlen(*ptr);

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
		int		size = force8bit ? 1 : utf8charlen(*ptr);

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
				  bool force8bit,
				  bool empty_string_is_null)
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

	if (!force8bit &&
		*slen == 3 && strncmp(str, "\342\210\205", 3) == 0)
	{
		*slen = 4;
		return sstrdup("NULL");
	}

	while (*ptr && _slen > 0)
	{
		int		size = force8bit ? 1 : utf8charlen(*ptr);

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
		int		size = force8bit ? 1 : utf8charlen(*ptr);

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
	bool	save_column_names = false;

	int		min_row = desc->first_data_row;
	int		max_row = desc->last_data_row;
	int		xmin = -1;
	int		xmax = -1;
	int		colnum = 0;

	char  **columns = NULL;
	char   *quoted_table_name = NULL;

	current_state->errstr = NULL;

	if (cmd == cmd_CopyLineExtended &&
		!DSV_FORMAT_TYPE(format))
	{
		format = CLIPBOARD_FORMAT_CSV;
	}

	if (cmd == cmd_CopyLineExtended ||
		INSERT_FORMAT_TYPE(format))
	{
		if (INSERT_FORMAT_TYPE(format))
		{
			char   *str = table_name;
			int		slen = strlen(table_name);

			str = quote_sql_identifier(str, &slen, opts->force8bit);
			quoted_table_name = sstrndup(str, slen);

			if (str != table_name)
				free(str);
		}

		columns = smalloc(sizeof(char *) * desc->columns);
		memset(columns, 0, sizeof(char *) * desc->columns);
		save_column_names = true;
	}

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

	if (cmd == cmd_CopyMarkedLines ||
		cmd == cmd_CopySearchedLines)
	{
		print_footer = false;
	}

	if (format != CLIPBOARD_FORMAT_TEXT)
	{
		print_border = false;
		print_footer = false;
		print_header_line = false;
		print_vertical_border = false;
	}

	if (save_column_names)
	{
		print_header = true;
	}

	for (rn = 0; rn <= desc->last_row; rn++)
	{
		LineInfo *linfo;
		char   *ldeco = NULL, *rdeco = NULL;
		int		ldecolen = 0, rdecolen = 0;
		bool	save_column_name = false;

		colnum = 0;

		if (desc->order_map)
		{
			MappedLine *mp = &desc->order_map[rn];
			lnb = mp->lnb;
			lbrn = mp->lnb_row;

			rowstr = lnb->rows[lbrn];
			linfo = lnb->lineinfo ? &lnb->lineinfo[lbrn] : NULL;
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
			if (cmd == cmd_CopySearchedLines)
			{
				/* force lineinfo setting */
				linfo = set_line_info(opts, scrdesc, lnb, lbrn, rowstr);

				if (!linfo || ((linfo->mask & LINEINFO_FOUNDSTR) == 0))
					continue;
			}
		}
		else
		{
			if (save_column_names)
				save_column_name = true;

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
		else
		{
			char   *headline_transl = desc->headline_transl;
			bool	is_first = true;

			while (rowstr && *rowstr && *headline_transl)
			{
				char   *fieldstr;
				int		fieldstrlen;

				if (xmin != -1)
				{
					fieldstr = rowstr;
					fieldstrlen = rowstrlen;
					rowstr = NULL;
					rowstrlen = 0;
				}
				else
				{
					int		ignored_spaces = 0;

					fieldstr = rowstr;
					fieldstrlen = 0;

					while (rowstr && *rowstr && *headline_transl)
					{
						char	hlc = *headline_transl;
						int		size;
						int		width;

						if (opts->force8bit)
						{
							size = 1;
							width = 1;
						}
						else
						{
							size = utf8charlen(*rowstr);
							width = utf_dsplen(rowstr);
						}

						headline_transl += width;

						if (hlc == 'L')
						{
							rowstr += size;
							fieldstr = rowstr;
						}
						else if (hlc == 'd')
						{
							if (*rowstr == ' ')
								ignored_spaces += 1;
							else
							{
								fieldstrlen += size + ignored_spaces;
								ignored_spaces = 0;
							}

							rowstr += size;
						}
						else
						{
							rowstr += size;
							break;
						}
					}

					while (*fieldstr == ' ' && fieldstrlen > 0)
					{
						fieldstr += 1;
						fieldstrlen -= 1;
					}
				}

				if (save_column_name)
				{
					char   *str = NULL;

					if (DSV_FORMAT_TYPE(format))
					{
						str = csv_format(fieldstr, &fieldstrlen, opts->force8bit);
					}
					else if (INSERT_FORMAT_TYPE(format))
					{
						str = quote_sql_identifier(fieldstr, &fieldstrlen, opts->force8bit);
					}

					columns[colnum++] = sstrndup(str, fieldstrlen);
					if (str != fieldstr)
						free(str);
				}
				else if (DSV_FORMAT_TYPE(format))
				{
					int		saved_errno;
					char   *outstr = csv_format(fieldstr, &fieldstrlen, opts->force8bit);

					if (cmd == cmd_CopyLineExtended)
					{
						if (is_first)
							is_first = false;
						else
							fputc('\n', fp);

						fputs(columns[colnum++], fp);

						if (format == CLIPBOARD_FORMAT_CSV)
							fputc(',', fp);
						else if (format == CLIPBOARD_FORMAT_TSVC)
							fputc('\t', fp);
					}
					else
					{
						if (is_first)
							is_first = false;
						else
						{
							if (format == CLIPBOARD_FORMAT_CSV)
								fputc(',', fp);
							else if (format == CLIPBOARD_FORMAT_TSVC)
								fputc('\t', fp);
						}
					}

					fwrite(outstr, fieldstrlen, 1, fp);
					saved_errno = errno;

					if (outstr != fieldstr)
						free(outstr);

					errno = saved_errno;
				}
				else if (INSERT_FORMAT_TYPE(format))
				{
					if (is_first)
					{
						int		i;
						char   *outstr;

						fputs("INSERT INTO ", fp);

						fputs(quoted_table_name, fp);

						fputc('(', fp);

						if (format == CLIPBOARD_FORMAT_INSERT)
						{
							for (i = 0; i < desc->columns; i++)
							{
								if (i > 0)
									fputs(", ", fp);
								fputs(columns[i], fp);
							}

							fputs(") VALUES(", fp);
						}
						else
						{
							int		indent_spaces;

							if (opts->force8bit)
								indent_spaces = strlen(quoted_table_name) + 1 + 12;
							else
								indent_spaces = utf_string_dsplen(quoted_table_name, SIZE_MAX) + 1 + 12;

							for (i = 0; i < desc->columns; i++)
							{
								if (i > 0)
									fprintf(fp, "%*.s", indent_spaces, "");

								fputs(columns[i], fp);

								if (i < desc->columns - 1)
									fprintf(fp, ",\t\t -- %d.\n", i + 1);
								else
								{
									fprintf(fp, ")\t\t -- %d.\n", i + 1);
									fputs("   VALUES(", fp);
								}
							}
						}

						outstr = quote_sql_literal(fieldstr,
												   &fieldstrlen,
												   opts->force8bit,
												   opts->empty_string_is_null);

						fwrite(outstr, fieldstrlen, 1, fp);

						colnum += 1;

						if (outstr != fieldstr)
							free(outstr);

						if (format == CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS)
						{
							if (desc->columns > 1)
							{
								fprintf(fp, ",\t\t -- %d. %s\n", 1, columns[0]);
							}
							else
							{
								fprintf(fp, ");\t\t -- %d. %s\n", 1, columns[0]);
							}
						}
						else
						{
							if (desc->columns == 1)
								fputs(");\n", fp);
						}

						is_first = false;
					}
					else
					{
						char   *outstr;

						outstr = quote_sql_literal(fieldstr,
												   &fieldstrlen,
												   opts->force8bit,
												   opts->empty_string_is_null);

						if (format == CLIPBOARD_FORMAT_INSERT)
						{
							fputs(", ", fp);
						}
						else
							fputs("          ", fp);

						fwrite(outstr, fieldstrlen, 1, fp);
						if (outstr != fieldstr)
							free(outstr);

						colnum += 1;

						if (format == CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS)
						{
							if (colnum < desc->columns)
							{
								fprintf(fp, ",\t\t -- %d. %s\n", colnum, columns[colnum - 1]);
							}
							else
							{
								fprintf(fp, ");\t\t -- %d. %s\n", colnum, columns[colnum - 1]);
							}
						}
						else
						{
							if (colnum >= desc->columns)
								fputs(");\n", fp);
						}
					}
				}
			}

			/* end of line */
			if (!save_column_name &&
				DSV_FORMAT_TYPE(format))
			{
				fputc('\n', fp);
			}
		}

		if (errno != 0)
		{
			format_error("%s", strerror(errno));
			log_row("Cannot write (%s)", current_state->errstr);

			if (columns)
			{
				int		i;

				for (i = 0; i < desc->columns; i++)
					free(columns[i]);

				free(columns);
			}

			if (quoted_table_name)
				free(quoted_table_name);

			return false;
		}
	}

	if (columns)
	{
		int		i;

		for (i = 0; i < desc->columns; i++)
			free(columns[i]);

		free(columns);
	}

	if (quoted_table_name)
		free(quoted_table_name);

	return true;
}
