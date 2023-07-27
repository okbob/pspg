/*-------------------------------------------------------------------------
 *
 * bscommands.c
 *	  a routines for implementation (parsing, execution) of backslash
 *	  commands
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/bscommands.c
 *
 *-------------------------------------------------------------------------
 */
#include "pspg.h"
#include "unicode.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	PspgCommand command;
	ClipboardFormat format;
	int		rows;
	double	percent;
	char   *nullstr;
	const char   *pipecmd;
} ExportedSpec;

typedef struct
{
	bool		backward;
	bool		selected;
	int			colno;
	char	   *pattern;
} SearchSpec;

/*
 * Returns pointer to first non token char. When token is valid, then
 * output n is higher than 0 and token pointer is non null.
 */
const char *
get_token(const char *instr, const char **token, int *n)
{
	*token = NULL;
	*n = 0;

	/* skip initial spaces */
	while (*instr == ' ')
		instr += 1;

	if (*instr == '\0')
		return NULL;

	if (isalpha(*instr))
	{
		*token = instr++;
		while (isalpha(*instr))
			instr += 1;

		*n = instr - *token;
	}

	return instr;
}

/*
 * Try to detect identifier in quotes or double quotes.
 */
static const char *
get_identifier(const char *instr,
			   const char **ident,
			   int *n,
			   bool allow_colnum)
{
	*ident = NULL;
	*n = 0;

	if (!instr)
		return NULL;

	/* skip initial spaces */
	while (*instr == ' ')
		instr += 1;

	if (*instr == '\0')
		return NULL;

	if (*instr == '\'' || *instr == '"')
	{
		char		ending_symbol = *instr;
		*ident = ++instr;

		while (*instr)
		{
			if (instr[0] == '\\' &&
				(instr[1] == '\'' || instr[1] == '"'))
			{
				instr += 2;
			}
			else if (*instr == ending_symbol)
			{
				*n = instr - *ident;
				instr += 1;
				break;
			}
			else
				instr += 1;
		}
	}
	else if (isalpha(*instr) || *instr == '_')
	{
		*ident = instr++;
		while (isalnum(*instr) || *instr == '_')
			instr += 1;

		*n = instr - *ident;
	}
	else if (allow_colnum && isdigit(*instr))
	{
		*ident = instr++;
		while (isdigit(*instr) || *instr == '_')
			instr += 1;

		*n = instr - *ident;
	}

	return *instr ? instr : NULL;
}


static const char *
parse_exported_spec(const char *instr,
					ExportedSpec *spec,
					bool *is_valid)
{
	const char   *token;
	int			n;

	spec->command = cmd_Copy;
	spec->format = CLIPBOARD_FORMAT_TEXT;
	spec->rows = 0;
	spec->percent = 0.0;
	spec->nullstr = NULL;
	spec->pipecmd = NULL;

	*is_valid = false;

	if (instr)
	{
		bool		format_is_specified_already = false;
		bool		range_is_specified_already = false;
		bool		null_is_specified_already = false;

		instr = get_token(instr, &token, &n);

		while (token)
		{
			bool		range_specified = false;
			bool		format_specified = false;
			bool		next_token_shouldbe_number = false;

			if (n > 20)
			{
				show_info_wait(" Syntax error (too long token)",
							   NULL, NULL, true, false, true);
				return NULL;
			}

			if (IS_TOKEN(token, n, "top"))
			{
				spec->command = cmd_CopyTopLines;
				range_specified = true;
				next_token_shouldbe_number = true;
			}
			else if (IS_TOKEN(token, n, "bottom"))
			{
				spec->command = cmd_CopyBottomLines;
				range_specified = true;
				next_token_shouldbe_number = true;
			}
			else if (IS_TOKEN(token, n, "all"))
			{
				spec->command = cmd_CopyAllLines;
				range_specified = true;
			}
			else if (IS_TOKEN(token, n, "sel") ||
					 IS_TOKEN(token, n, "selected"))
			{
				spec->command = cmd_CopySelected;
				range_specified = true;
			}
			else if (IS_TOKEN(token, n, "search") ||
					 IS_TOKEN(token, n, "searched"))
			{
				spec->command = cmd_CopySearchedLines;
				range_specified = true;
			}
			else if (IS_TOKEN(token, n, "mark") ||
					 IS_TOKEN(token, n, "marked"))
			{
				spec->command = cmd_CopyMarkedLines;
				range_specified = true;
			}
			else if (IS_TOKEN(token, n, "csv"))
			{
				spec->format = CLIPBOARD_FORMAT_CSV;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "tsvc"))
			{
				spec->format = CLIPBOARD_FORMAT_TSVC;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "sqlval") ||
					 IS_TOKEN(token, n, "sqlvalues"))
			{
				spec->format = CLIPBOARD_FORMAT_SQL_VALUES;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "text"))
			{
				spec->format = CLIPBOARD_FORMAT_TEXT;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "pipesep") ||
					 IS_TOKEN(token, n, "ps"))
			{
				spec->format = CLIPBOARD_FORMAT_PIPE_SEPARATED;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "insert"))
			{
				spec->format = CLIPBOARD_FORMAT_INSERT;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "cinsert"))
			{
				spec->format = CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS;
				format_specified = true;
			}
			else if (IS_TOKEN(token, n, "null") ||
					 IS_TOKEN(token, n, "nullstr"))
			{
				const char	*ident;
				int		ident_len;

				if (null_is_specified_already)
				{
					show_info_wait(" Syntax error (null is specified already)",
								   NULL, NULL, true, false, true);
					return NULL;
				}

				while (*instr == ' ')
					instr += 1;

				if (*instr != '"')
				{
					show_info_wait(" Syntax error (expected '\"')",
								    NULL, NULL, true, false, true);
					return NULL;
				}

				instr = get_identifier(instr, &ident, &ident_len, false);
				if (!ident)
				{
					show_info_wait(" Syntax error (expected closed quoted string)",
								   NULL, NULL, true, false, true);
					return NULL;
				}

				ident = trim_quoted_str(ident, &ident_len);

				if (ident_len > 0)
					spec->nullstr = sstrndup(ident, ident_len);
			}
			else
			{
				char buffer[255];

				snprintf(buffer, 255, " Syntax error (unknown token \"%.*s\")", n, token);

				show_info_wait(buffer, NULL, NULL, true, false, true);
				return NULL;
			}

			if (format_is_specified_already && format_specified)
			{
				show_info_wait(" Syntax error (format specification is redundant)",
							   NULL, NULL, true, false, true);
				return NULL;
			}

			if (range_is_specified_already && range_specified)
			{
				show_info_wait(" Syntax error (range specification is redundant)",
							   NULL, NULL, true, false, true);
				return NULL;
			}

			if (format_specified)
				format_is_specified_already = true;
			if (range_specified)
				range_is_specified_already = true;

			if (next_token_shouldbe_number)
			{
				char	   *endptr = NULL;

				errno = 0;

				if (instr)
					spec->percent = strtod(instr, &endptr);

				if (!instr || instr == endptr || errno != 0)
				{
					show_info_wait(" Syntax error (expected number)",
								   NULL, NULL, true, false, true);
					return NULL;
				}

				if (endptr && *endptr != '%')
				{
					spec->rows = (int) spec->percent;
					spec->percent = 0.0;
					endptr += 1;
				}

				instr = endptr;
			}

			instr = get_token(instr, &token, &n);
		}

		if (instr && *instr)
		{
			if (*instr == '|')
			{
				spec->pipecmd = ++instr;
				instr = NULL;
			}
			else if (*instr != '\\')
			{
				show_info_wait(" Syntax error (unexpected symbol)",
							   NULL, NULL, true, false, true);
				return NULL;
			}
		}
	}

	*is_valid = true;

	return instr;
}

/*
 * Returns count of column names with pattern string
 */
static int
substr_column_name_search(DataDesc *desc,
						  const char *pattern,
						  int len,
						  int first_colno,
						  int *colno)
{
	int		i;
	int		count = 0;

	*colno = -1;

	for (i = first_colno; i <= desc->columns; i++)
	{
		char	   *name = desc->namesline + desc->cranges[i - 1].name_offset;
		int			size = desc->cranges[i - 1].name_size;

		if (use_utf8)
		{
			if (utf8_nstrstr_with_sizes(name, size, pattern, len))
			{
				if (*colno == -1)
					*colno = i;

				count += 1;
			}
		}
		else
		{
			if (nstrstr_with_sizes(name, size, pattern, len))
			{
				if (*colno == -1)
					*colno = i;

				count += 1;
			}
		}
	}

	return count;
}

static const char *
parse_search_spec(DataDesc *desc,
				  const char *instr,
				  SearchSpec *spec,
				  bool *is_valid)
{
	spec->backward = false;
	spec->selected = false;
	spec->colno = 0;
	spec->pattern = NULL;

	*is_valid = false;

	if (instr)
	{
		bool		direction_is_specified_already = false;
		bool		range_is_specified_already = false;
		bool		pattern_is_specified_already = false;

		while (instr)
		{
			while (*instr == ' ')
				instr += 1;

			if (*instr == '"')
			{
				const char	   *ident;
				int			n;

				instr = get_identifier(instr, &ident, &n, false);
				if (!ident)
				{
					show_info_wait(" Syntax error (expected closed quoted string)",
								   NULL, NULL, true, false, true);
					return NULL;
				}

				spec->pattern = sstrndup(ident, n);

				if (pattern_is_specified_already)
				{
					show_info_wait(" Syntax error (pattern is specified already)",
								   NULL, NULL, true, false, true);
					return NULL;
				}

				pattern_is_specified_already = true;
				continue;
			}
			else
			{
				const char   *token;
				int		n;
				const char   *pattern = instr;
				int		pattern_len;

				instr = get_token(instr, &token, &n);
				if (token)
				{
					if (IS_TOKEN(token, n, "back") ||
						IS_TOKEN(token, n, "backward"))
					{
						spec->backward = true;

						if (direction_is_specified_already)
						{
							show_info_wait(" Syntax error (direction is specified already)",
										   NULL, NULL, true, false, true);
							return NULL;
						}

						direction_is_specified_already = true;
						continue;
					}
					else if (IS_TOKEN(token, n, "sel") ||
							 IS_TOKEN(token, n, "selected")) 
					{
						spec->selected = true;

						if (range_is_specified_already)
						{
							show_info_wait(" Syntax error (range specification is redundant)",
										   NULL, NULL, true, false, true);
							return NULL;
						}

						range_is_specified_already = true;
						continue;
					}
					else if (IS_TOKEN(token, n, "colum") ||
							 IS_TOKEN(token, n, "column"))
					{
						const char	   *ident;
						int			len;

						if (range_is_specified_already)
						{
							show_info_wait(" Syntax error (range specification is redundant)",
										   NULL, NULL, true, false, true);
							return NULL;
						}

						instr = get_identifier(instr, &ident, &len, false);
						if (len > 0)
						{
							ident = trim_quoted_str(ident, &len);

							if (substr_column_name_search(desc, ident, len, 1, &spec->colno) == 0)
							{
								show_info_wait(" Cannot to identify column",
											   NULL, true, true, false, true);
								return NULL;
							}
						}
						else
						{
							show_info_wait(" Invalid identifier (expected column name)",
										   NULL, true, true, false, true);
							return NULL;
						}

						range_is_specified_already = true;
						continue;
					}
				}

				pattern_len = strlen(pattern);
				pattern = trim_quoted_str(pattern, &pattern_len);

				if (pattern_len > 0)
				{
					spec->pattern = sstrndup(pattern, pattern_len);

					if (pattern_is_specified_already)
					{
						show_info_wait(" Syntax error (pattern is specified already)",
									   NULL, NULL, true, false, true);
						return NULL;
					}
				}

				*is_valid = true;
				return NULL;
			}
		}

		*is_valid = true;
	}

	return instr;
}

/*
 * Parse and processes one backslash command.
 * Returns pointer to next backslash command.
 *
 */
const char *
parse_and_eval_bscommand(const char *cmdline,
						 Options *opts,
						 ScrDesc *scrdesc,
						 DataDesc *desc,
						 int *next_command,
						 long *long_argument,
						 bool *long_argument_is_valid,
						 char **string_argument,
						 bool *string_argument_is_valid,
						 bool *refresh_clear)

{
	bool		next_is_num = false;
	bool		sign_minus = false;
	bool		sign_plus = false;
	const char	   *ptr;
	char	   *endptr;
	int			n;

	if (!cmdline)
		return NULL;

	while (*cmdline == ' ')
		cmdline++;

	/*
	 * Leave when line is empty. This code can be duplicited, because the command
	 * can start here, or we can continue in command line processing.
	 */
	if (*cmdline == '\0')
		return NULL;

	if (*cmdline++ != '\\')
	{
		show_info_wait(" Syntax error (expected \"\\\")",
					   NULL, true, true, false, true);
		return NULL;
	}

	/*
	 * Ignore empty commands on the end command line.
	 * It's probably some artefact of escape.
	 */
	if (*cmdline == '\0')
		return NULL;

	if (*cmdline == '+')
	{
		/* ignore initial + */
		next_is_num = true;
		sign_plus = true;
		cmdline += 1;
	}
	else if (*cmdline == '-')
	{
		next_is_num = true;
		sign_minus = true;
		cmdline += 1;
	}

	if (isdigit(*cmdline))
	{
		*long_argument = strtol(cmdline, &endptr, 10);

		if (sign_plus)
		{
			*long_argument = labs(*long_argument);
			*next_command = cmd_GotoLineRel;
		}
		else if (sign_minus)
		{
			*long_argument = - labs(*long_argument);
			*next_command = cmd_GotoLineRel;
		}
		else if (*endptr == '-')
		{
			*long_argument = - labs(*long_argument);
			*next_command = cmd_GotoLine;
			endptr += 1;
		}
		else /* \N or \N+ */
		{
			*long_argument = labs(*long_argument);
			*next_command = cmd_GotoLine;

			if (*endptr == '+')
				endptr += 1;
		}

		cmdline = endptr;
		*long_argument_is_valid = true;

		return cmdline;
	}
	else if (next_is_num)
	{
		show_info_wait(" Syntax error (expected number)",
					   NULL, true, true, false, true);
		return NULL;
	}

	n = 0;
	ptr = cmdline;

	while (isalpha(*ptr))
	{
		ptr += 1;
		n += 1;
	}

	if (IS_TOKEN(cmdline, n, "q") ||
		IS_TOKEN(cmdline, n, "quit"))
	{
		cmdline += n;
		*next_command = cmd_Quit;
	}
	else if (IS_TOKEN(cmdline, n, "the") ||
			 IS_TOKEN(cmdline, n, "theme"))
	{
		cmdline += n;
		*long_argument = strtol(cmdline, &endptr, 10);

		if (cmdline != endptr)
		{
			*long_argument_is_valid = true;
			*next_command = cmd_SetTheme;
			cmdline = endptr;
		}
		else
		{
			show_info_wait(" expected number",
						   NULL, true, true, false, true);
			return NULL;
		}
	}
	else if (IS_TOKEN(cmdline, n, "cth") ||
			 IS_TOKEN(cmdline, n, "cthe") ||
			 IS_TOKEN(cmdline, n, "ctheme"))
	{
		const char *ident;
		int		len;

		cmdline += n;

		free(*string_argument);
		*string_argument = NULL;
		*string_argument_is_valid = false;

		cmdline = get_identifier(cmdline, &ident, &len, false);

		if (!ident)
		{
			show_info_wait(" Syntax error (expected string)",
						   NULL, NULL, true, false, true);
			return NULL;
		}

		ident = trim_quoted_str(ident, &len);
		if (len > 0)
		{
			*string_argument = sstrndup(ident, len);
			*string_argument_is_valid = true;
		}
		else
		{
			show_info_wait(" Syntax error (expected non empty)",
						   NULL, NULL, true, false, true);
			return NULL;
		}

		*next_command = cmd_SetCustomTheme;
	}
	else if (IS_TOKEN(cmdline, n, "search"))
	{
		SearchSpec		spec;
		bool			is_valid;

		free(*string_argument);
		*string_argument = NULL;
		*string_argument_is_valid = false;

		cmdline = parse_search_spec(desc, cmdline + n, &spec, &is_valid);

		if (is_valid)
		{
			throw_searching(scrdesc, desc);

			if (spec.colno > 0)
			{
				scrdesc->search_first_column = desc->cranges[spec.colno - 1].xmin;
				scrdesc->search_columns = desc->cranges[spec.colno - 1].xmax - scrdesc->search_first_column + 1;
				scrdesc->search_selected_mode = true;
			}
			else if (spec.selected)
			{
				if (scrdesc->selected_first_row == -1 &&
					  scrdesc->selected_first_column == -1)
				{
					show_info_wait(" There are not selected area",
								   NULL, true, true, true, false);
					return NULL;
				}

				scrdesc->search_first_row = scrdesc->selected_first_row;
				scrdesc->search_rows = scrdesc->selected_rows;
				scrdesc->search_first_column = scrdesc->selected_first_column;
				scrdesc->search_columns = scrdesc->selected_columns;
				scrdesc->search_selected_mode = true;
			}

			if (spec.pattern)
			{
				*string_argument = spec.pattern;
				*string_argument_is_valid = true;
			}

			if (spec.backward)
				*next_command = cmd_BackwardSearch;
			else
				*next_command = cmd_ForwardSearch;
		}
		else
			free(spec.pattern);
	}
	else if (IS_TOKEN(cmdline, n, "ord") ||
			 IS_TOKEN(cmdline, n, "order") ||
			 IS_TOKEN(cmdline, n, "ordd") ||
			 IS_TOKEN(cmdline, n, "orderd") ||
			 IS_TOKEN(cmdline, n, "sort") ||
			 IS_TOKEN(cmdline, n, "sortd") ||
			 IS_TOKEN(cmdline, n, "dsort") ||
			  IS_TOKEN(cmdline, n, "rs") ||
			 IS_TOKEN(cmdline, n, "rsort") ||
			 IS_TOKEN(cmdline, n, "asc") ||
			 IS_TOKEN(cmdline, n, "desc"))
	{
		const char   *ident;
		int		len;
		bool	is_desc;
		PspgCommand OrderCommand;

		is_desc = IS_TOKEN(cmdline, n, "ordd") ||
				  IS_TOKEN(cmdline, n, "orderd") ||
				  IS_TOKEN(cmdline, n, "sortd") ||
				  IS_TOKEN(cmdline, n, "dsort") ||
				  IS_TOKEN(cmdline, n, "rs") ||
				  IS_TOKEN(cmdline, n, "rsort") ||
				  IS_TOKEN(cmdline, n, "desc");

		OrderCommand = is_desc ? cmd_SortDesc : cmd_SortAsc;
		cmdline = get_identifier(cmdline + n, &ident, &len, true);

		if (len > 0)
		{
			if (isdigit(*ident))
			{
				/* entered column number */
				*long_argument = strtol(ident, NULL, 10);
				*long_argument_is_valid = true;

				if (*long_argument >= 1 && *long_argument <= desc->columns)
					*next_command = OrderCommand;
				else
				{
					show_info_wait(" Column number is out of range",
								   NULL, true, true, false, true);
					return NULL;
				}
			}
			else
			{
				int		count;
				int		colno;

				ident = trim_quoted_str(ident, &len);
				count = substr_column_name_search(desc, ident, len, 1, &colno);

				*long_argument = colno;

				if (count > 0)
				{
					*long_argument_is_valid = true;
					*next_command = OrderCommand;
				}
				else
				{
					show_info_wait(" Cannot to identify column",
								   NULL, true, true, false, true);
					return NULL;
				}
			}
		}
		else
		{
			show_info_wait(" Invalid identifier (expected column name)",
						   NULL, true, true, false, true);
			return NULL;
		}
	}
	else if (IS_TOKEN(cmdline, n, "save"))
	{
		ExportedSpec expspec;
		bool	is_valid;

		cmdline = parse_exported_spec(cmdline + n, &expspec, &is_valid);
		if (is_valid)
		{
			Options loc_opts;

			memcpy(&loc_opts, opts, sizeof(Options));

			loc_opts.copy_target = COPY_TARGET_FILE;
			loc_opts.nullstr = expspec.nullstr;
			loc_opts.empty_string_is_null = !expspec.nullstr;

			export_to_file(expspec.command,
						   expspec.format,
						   &loc_opts, scrdesc, desc,
						   0, 0,
						   expspec.rows,
						   expspec.percent,
						   expspec.pipecmd,
						   refresh_clear);
		}

		free(expspec.nullstr);
	}
	else if (IS_TOKEN(cmdline, n, "copy"))
	{
		ExportedSpec expspec;
		bool	is_valid;

		cmdline = parse_exported_spec(cmdline + n, &expspec, &is_valid);
		if (is_valid)
		{
			Options loc_opts;

			memcpy(&loc_opts, opts, sizeof(Options));

			loc_opts.copy_target = COPY_TARGET_CLIPBOARD;
			loc_opts.nullstr = expspec.nullstr;
			loc_opts.empty_string_is_null = !expspec.nullstr;

			export_to_file(expspec.command,
						   expspec.format,
						   &loc_opts, scrdesc, desc,
						   0, 0,
						   expspec.rows,
						   expspec.percent,
						   expspec.pipecmd,
						   refresh_clear);
		}

		free(expspec.nullstr);
	}
	else
	{
		show_info_wait(" Unknown command \"%s\"",
						   cmdline, true, true, false, true);
		return NULL;
	}

	return cmdline;
}
