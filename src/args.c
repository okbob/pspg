/*-------------------------------------------------------------------------
 *
 * buildargv.c
 *	  a routines for parsing string to argc, argv format
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/buildargv.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <getopt.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef HAVE_LIBREADLINE

#if defined(HAVE_READLINE_READLINE_H)

#include <readline/readline.h>

#elif defined(HAVE_READLINE_H)

#include <readline.h>

#endif

#if RL_VERSION_MAJOR < 6
#define rl_display_prompt rl_prompt
#endif
#endif

#include "pspg.h"

static struct option long_options[] =
{
	/* These options set a flag. */
	{"force-uniborder", no_argument, 0, 5},
	{"help", no_argument, 0, 1},
	{"hlite-search", no_argument, 0, 'g'},
	{"HILITE-SEARCH", no_argument, 0, 'G'},
	{"ignore-case", no_argument, 0, 'i'},
	{"IGNORE-CASE", no_argument, 0, 'I'},
	{"no-bars", no_argument, 0, 8},
	{"no-mouse", no_argument, 0, 2},
	{"no-sound", no_argument, 0, 3},
	{"less-status-bar", no_argument, 0, 4},
	{"no-commandbar", no_argument, 0, 6},
	{"no-topbar", no_argument, 0, 7},
	{"no-cursor", no_argument, 0, 10},
	{"vertical-cursor", no_argument, 0, 15},
	{"tabular-cursor", no_argument, 0, 11},
	{"line-numbers", no_argument, 0, 9},
	{"quit-if-one-screen", no_argument, 0, 'F'},
	{"version", no_argument, 0, 'V'},
	{"bold-labels", no_argument, 0, 12},
	{"bold-cursor", no_argument, 0, 13},
	{"only-for-tables", no_argument, 0, 14},
	{"about", no_argument, 0, 16},
	{"csv", no_argument, 0, 17},
	{"double-header", no_argument, 0, 24},
	{"csv-separator", required_argument, 0, 18},
	{"border", required_argument, 0, 19},
	{"no-sigint-exit", no_argument, 0, 21},
	{"no-sigint-search-reset", no_argument, 0, 22},
	{"ni", no_argument, 0, 23},
	{"log", required_argument, 0, 25},
	{"watch", required_argument, 0, 'w'},
	{"query", required_argument, 0, 'q'},
	{"host", required_argument, 0, 'h'},
	{"port", required_argument, 0, 'p'},
	{"password", no_argument, 0, 'W'},
	{"username", required_argument, 0, 'U'},
	{"dbname", required_argument, 0, 'd'},
	{"file", required_argument, 0, 'f'},
	{"rr", required_argument, 0, 26},
	{"interactive", no_argument, 0, 27},
	{"csv-header", required_argument, 0, 28},
	{"ignore-short-rows", no_argument, 0, 29},
	{"tsv", no_argument, 0, 30},
	{"null", required_argument, 0, 31},
	{"ignore_file_suffix", no_argument, 0, 32},
	{"no-watch-file", no_argument, 0, 33},
	{"stream", no_argument, 0, 34},
	{"quit-on-f3", no_argument, 0, 35},
	{"wait", required_argument, 0, 36},
	{"hold-stream", required_argument, 0, 37},
	{"skip-columns-like", required_argument, 0, 38},
	{0, 0, 0, 0}
};

static void
consume_whitespace(const char **input)
{
	while (isspace(**input))
	{
		(*input)++;
	}
}

/*
 * This code is part of libiberty library
 */
char **
buildargv(const char *input, int *_argc, char *appname)
{
	char	   *arg;
	char	   *copybuf;
	bool		squote = false;
	bool		dquote = false;
	bool		bsquote = false;
	int			argc = 1;
	int			maxargc = 8;
	char	  **argv;

	argv = (char **) smalloc(maxargc * sizeof (char *));

	argv[0] = appname;

	if (input != NULL)
	{
		copybuf = (char *) smalloc(strlen(input) + 1);

		/*
		 * Is a do{}while to always execute the loop once.  Always return an
		 * argv, even for null strings.  See NOTES above, test case below.
		 */
		do
		{
			/* Pick off argv[argc] */
			consume_whitespace(&input);

			if (argc >= (maxargc - 1))
			{
				maxargc *= 2;
				argv = (char **) realloc(argv, maxargc * sizeof (char *));
				argv[argc] = NULL;
			}

			/* Begin scanning arg */
			arg = copybuf;
			while (*input != '\0')
			{
				if (isspace(*input) && !squote && !dquote && !bsquote)
				{
					break;
				}
				else
				{
					if (bsquote)
					{
						bsquote = false;
						*arg++ = *input;
					}
					else if (*input == '\\')
					{
						bsquote = true;
					}
					else if (squote)
					{
						if (*input == '\'')
							squote = false;
						else
							*arg++ = *input;
					}
					else if (dquote)
					{
						if (*input == '"')
							dquote = false;
						else
							*arg++ = *input;
					}
					else
					{
						if (*input == '\'')
							squote = true;
						else if (*input == '"')
							dquote = true;
						else
							*arg++ = *input;
					}

					input++;
				}
			}

			*arg = '\0';

			argv[argc++] = sstrdup(copybuf);
			consume_whitespace(&input);
		}
		while (*input != '\0');

		free (copybuf);
	}

	*_argc = argc;

	return argv;
}

bool
readargs(char **argv,
		 int argc,
		 Options *opts,
		 StateData *state)
{
	int		opt;
	int		option_index = 0;

	state->errstr = NULL;

	/* force reset getopt interface */
	optind = 0;

	while ((opt = getopt_long(argc, argv, "abs:c:d:f:h:p:XVFgGiIq:U:w:W",
							  long_options, &option_index)) != -1)
	{
		int		n;

		switch (opt)
		{
			case 1:
				{
					fprintf(stderr, "pspg is a Unix pager designed for table browsing.\n\n");
					fprintf(stderr, "Usage:\n");
					fprintf(stderr, "  %s [OPTION] [file]\n", argv[0]);
					fprintf(stderr, "\nGeneral options:\n");
					fprintf(stderr, "  --about                  about authors\n");
					fprintf(stderr, "  --help                   show this help\n");
					fprintf(stderr, "  -V, --version            show version\n");
					fprintf(stderr, "  -f, --file=FILE          open file\n");
					fprintf(stderr, "  -F, --quit-if-one-screen\n");
					fprintf(stderr, "                           quit if content is one screen\n");
					fprintf(stderr, "  --hold-stream=NUM        can reopen closed FIFO (0, 1, 2)\n");
					fprintf(stderr, "  --interactive            force interactive mode\n");
					fprintf(stderr, "  --ignore_file_suffix     don't try to deduce format from file suffix\n");
					fprintf(stderr, "  --ni                     not interactive mode (only for csv and query)\n");
					fprintf(stderr, "  --no-watch-file          don't watch inotify event of file\n");
					fprintf(stderr, "  --no-mouse               don't use own mouse handling\n");
					fprintf(stderr, "  --no-sigint-search-reset\n");
					fprintf(stderr, "                           without reset searching on sigint (CTRL C)\n");
					fprintf(stderr, "  --only-for-tables        use std pager when content is not table\n");
					fprintf(stderr, "  --on-sigint-exit         without exit on sigint(CTRL C or Escape)\n");
					fprintf(stderr, "  --quit-on-f3             exit on F3 like mc viewers\n");
					fprintf(stderr, "  --rr=ROWNUM              rows reserved for specific purposes\n");
					fprintf(stderr, "  --stream                 read input forever\n");
					fprintf(stderr, "  -X                       don't use alternate screen\n");
					fprintf(stderr, "\nOutput format options:\n");
					fprintf(stderr, "  -a                       force ascii\n");
					fprintf(stderr, "  -b                       black-white style\n");
					fprintf(stderr, "  -s N                     set color style number (0..%d)\n", MAX_STYLE);
					fprintf(stderr, "  --bold-labels            row, column labels use bold font\n");
					fprintf(stderr, "  --bold-cursor            cursor use bold font\n");
					fprintf(stderr, "  --border                 type of borders (0..2)\n");
					fprintf(stderr, "  --double-header          header separator uses double lines\n");
					fprintf(stderr, "  --force-uniborder        replace ascii borders by unicode borders\n");
					fprintf(stderr, "  --ignore-bad-rows        rows with wrong column numbers are ignored\n");
					fprintf(stderr, "  --null=STRING            STRING used instead NULL\n");
					fprintf(stderr, "\nSearching options\n");
					fprintf(stderr, "  -g --hlite-search, -G --HILITE-SEARCH\n");
					fprintf(stderr, "                           don't highlight lines for searches\n");
					fprintf(stderr, "  -i --ignore-case         ignore case in searches that do not contain uppercase\n");
					fprintf(stderr, "  -I --IGNORE-CASE         ignore case in all searches\n");
					fprintf(stderr, "\nInterface options:\n");
					fprintf(stderr, "  -c N                     fix N columns (0..9)\n");
					fprintf(stderr, "  --less-status-bar        status bar like less pager\n");
					fprintf(stderr, "  --line-numbers           show line number column\n");
					fprintf(stderr, "  --no-bars, --no-commandbar, --no-topbar\n");
					fprintf(stderr, "                           don't show bottom, top bar or both\n");
					fprintf(stderr, "  --no-cursor              row cursor will be hidden\n");
					fprintf(stderr, "  --no-sound               don't use beep when scroll is not possible\n");
					fprintf(stderr, "  --tabular-cursor         cursor is visible only when data has table format\n");
					fprintf(stderr, "  --vertical-cursor        show vertical column cursor\n");
					fprintf(stderr, "\nInput format options:\n");
					fprintf(stderr, "  --csv                    input stream has csv format\n");
					fprintf(stderr, "  --csv-separator          char used as field separator\n");
					fprintf(stderr, "  --csv-header [on/off]    specify header line usage\n");
					fprintf(stderr, "  --skip-columns-like=\"SPACE SEPARATED STRING LIST\"\n");
					fprintf(stderr, "                           columns with substr in name are ignored\n");
					fprintf(stderr, "  --tsv                    input stream has tsv format\n");
					fprintf(stderr, "\nWatch mode options:\n");
					fprintf(stderr, "  -q, --query=QUERY        execute query\n");
					fprintf(stderr, "  -w, --watch time         the query (or read file) is repeated every time (sec)\n");
					fprintf(stderr, "\nConnection options\n");
					fprintf(stderr, "  -d, --dbname=DBNAME      database name\n");
					fprintf(stderr, "  -h, --host=HOSTNAME      database server host (default: \"local socket\")\n");
					fprintf(stderr, "  -p, --port=PORT          database server port (default: \"5432\")\n");
					fprintf(stderr, "  -U, --username=USERNAME  database user name\n");
					fprintf(stderr, "  -W, --password           force password prompt\n");
					fprintf(stderr, "\nDebug options:\n");
					fprintf(stderr, "  --log=FILE               log debug info to file\n");
					fprintf(stderr, "  --wait=NUM               wait NUM seconds to allow attach from a debugger\n");
					fprintf(stderr, "\n");
					fprintf(stderr, "pspg shares lot of key commands with less pager or vi editor.\n");

					return false;
				}
			case 'a':
				opts->force_ascii_art = true;
				break;
			case 'I':
				opts->ignore_case = true;
				break;
			case 'i':
				opts->ignore_lower_case = true;
				break;
			case 'q':
				opts->query = optarg;
				break;
			case 'w':
				opts->watch_time = atoi(optarg);
				if (opts->watch_time < 0 || opts->watch_time > 3600)
				{
					state->errstr = "query watch time can be between 0 and 3600";
					return false;
				}
				break;
			case 2:
				opts->no_mouse = true;
				break;
			case 3:
				opts->no_sound = true;
				break;
			case 4:
				opts->less_status_bar = true;
				break;
			case 5:
				opts->force_uniborder = true;
				break;
			case 6:
				opts->no_commandbar = true;
				break;
			case 7:
				opts->no_topbar = true;
				break;
			case 8:
				opts->no_commandbar = true;
				opts->no_topbar = true;
				break;
			case 9:
				opts->show_rownum = true;
				break;
			case 10:
				opts->no_cursor = true;
				break;
			case 11:
				opts->tabular_cursor = true;
				break;
			case 12:
				opts->bold_labels = true;
				break;
			case 13:
				opts->bold_cursor = true;
				break;
			case 14:
				state->only_for_tables = true;
				break;
			case 15:
				opts->vertical_cursor = true;
				break;
			case 16:
				{
					fprintf(stdout, "The pspg-%s is special pager designed for databases.\n\n", PSPG_VERSION);
					fprintf(stdout, "Authors:\n");
					fprintf(stdout, "    2017-2020 Pavel Stehule, Benesov district, Czech Republic\n\n");
					fprintf(stdout, "Licence:\n");
					fprintf(stdout, "    Distributed under BSD licence\n\n");

					return false;
				}
			case 17:
				opts->csv_format = true;
				break;
			case 18:
				opts->csv_separator = *optarg;
				break;
			case 19:
				n = atoi(optarg);
				if (n < 0 || n > 2)
				{
					state->errstr = "csv border type can be between 0 and 2";
					return false;
				}
				opts->border_type = n;
				break;
			case 21:
				opts->on_sigint_exit = true;
				break;
			case 22:
				opts->no_sigint_search_reset = true;
				break;
			case 23:
				state->no_interactive = true;
				break;
			case 24:
				opts->double_header = true;
				break;
			case 25:
				opts->log_pathname = sstrdup(optarg);
				break;
			case 26:
				state->reserved_rows = atoi(optarg);
				if (state->reserved_rows < 1 || state->reserved_rows > 100)
				{
					state->errstr = "reserved rows should be between 1 and 100";
					return false;
				}
				break;
			case 27:
				state->interactive = true;
				break;
			case 28:
				{
					if (nstreq(optarg, "off"))
						opts->csv_header = '-';
					else if (nstreq(optarg, "on"))
						opts->csv_header = '+';
					else
					{
						state->errstr = "csv_header option can be on \"or\" \"off\"";
						return false;
					}
				}
				break;
			case 29:
				opts->ignore_short_rows = true;
				break;
			case 30:
				opts->tsv_format = true;
				break;
			case 31:
				opts->nullstr = sstrdup(optarg);
				break;
			case 32:
				state->ignore_file_suffix = true;
				break;
			case 33:
				opts->watch_file = false;
				break;
			case 34:
				state->stream_mode = true;
				break;
			case 35:
				opts->quit_on_f3 = true;
				break;
			case 36:
				state->boot_wait = atoi(optarg);
				if (state->boot_wait < 0 || state->boot_wait > 120)
				{
					state->errstr = "wait should be between 1 and 120 (sec)";
					return false;
				}
				break;
			case 37:
				state->hold_stream = atoi(optarg);
				if (state->hold_stream < 0 || state->hold_stream > 2)
				{
					state->errstr = "hold-stream should be 0, 1 or 2";
					return false;
				}
				break;
			case 38:
				opts->csv_skip_columns_like = sstrdup(optarg);
				break;

			case 'V':
				fprintf(stdout, "pspg-%s\n", PSPG_VERSION);

#ifdef HAVE_LIBREADLINE

				fprintf(stdout, "with readline (version: 0x%04x)\n", RL_READLINE_VERSION);

#endif

#ifdef COMPILE_MENU

				fprintf(stdout, "with integrated menu\n");

#endif

#ifdef NCURSES_VERSION

				fprintf(stdout, "ncurses version: %s, patch: %ld\n",
						NCURSES_VERSION,
						(long) NCURSES_VERSION_PATCH);

#endif

#ifdef HAVE_NCURSESW

				fprintf(stdout, "ncurses with wide char support\n");

#endif

#ifdef NCURSES_WIDECHAR

				fprintf(stdout, "ncurses widechar num: %d\n", NCURSES_WIDECHAR);

#endif

				fprintf(stdout, "wchar_t width: %d, max: %d\n", __SIZEOF_WCHAR_T__, __WCHAR_MAX__);

#ifdef HAVE_POSTGRESQL

				fprintf(stdout, "with postgres client integration\n");

#endif

#ifdef HAVE_INOTIFY

				fprintf(stdout, "with inotify support\n");

#endif

				return false;
			case 'X':
				state->no_alternate_screen = true;
				break;
			case 'b':
				opts->theme = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > MAX_STYLE)
				{
					format_error("only color schemas 0 .. %d are supported", MAX_STYLE);
					return false;
				}
				opts->theme = n;
				break;
			case 'c':
				n = atoi(optarg);
				if (n < 0 || n > 9)
				{
					state->errstr = "fixed columns should be between 0 and 4";
					return false;
				}
				opts->freezed_cols = n;
				break;
			case 'f':
				{
					if (opts->pathname)
					{
						state->errstr = "only one file can be browsed";
						return false;
					}
					opts->pathname = sstrdup(optarg);
				}
				break;
			case 'F':
				state->quit_if_one_screen = true;
				break;
			case 'g':
				opts->no_highlight_lines = true;
				break;
			case 'G':
				opts->no_highlight_search = true;
				break;
			case 'h':
				opts->host = sstrdup(optarg);
				break;
			case 'p':
				{
					long	port;

					port = strtol(optarg, NULL, 10);
					if ((port < 1) || (port > 65535))
					{
						format_error("invalid port number: %s", optarg);
						return false;
					}
					opts->port = sstrdup(optarg);
				}
				break;
			case 'U':
				opts->username = sstrdup(optarg);
				break;
			case 'W':
				opts->force_password_prompt = true;
				break;
			case 'd':
				opts->dbname = sstrdup(optarg);
				break;

			default:
				{
					format_error("Try %s --help\n", argv[0]);
					return false;
				}
		}
	}

	for (; optind < argc; optind++)
	{
		if (opts->pathname)
		{
			state->errstr = "only one file can be browsed";
			return false;
		}

		opts->pathname = sstrdup(argv[optind]);
	}

	return true;
}

/*
 * Deduce format type from file suffix
 */
static int
get_format_type(char *path)
{
	char		buffer[4];
	char	   *r_ptr, *w_ptr;
	int			i;
	int			l;

	l = strlen(path);
	if (l < 5)
		return FILE_MATRIX;

	r_ptr = path + l - 4;
	w_ptr = buffer;

	if (*r_ptr++ != '.')
		return FILE_MATRIX;

	for (i = 0; i < 3; i++)
		*w_ptr++ = tolower(*r_ptr++);

	*w_ptr = '\0';

	if (strcmp(buffer, "csv") == 0)
		return FILE_CSV;
	else if (strcmp(buffer, "tsv") == 0)
		return FILE_TSV;
	else
		return FILE_MATRIX;
}

/*
 * Post parsing arguments check
 */
bool
args_are_consistent(Options *opts, StateData *state)
{
	state->errstr = NULL;

	if (state->no_interactive && state->interactive)
	{
		state->errstr = "option --ni and --interactive cannot be used together";
		return false;
	}

	if (opts->query && opts->pathname)
	{
		state->errstr = "option --query and --file cannot be used together";
		return false;
	}

	if (opts->csv_format && opts->tsv_format)
	{
		state->errstr = "option --csv and --tsv cannot be used together";
		return false;
	}

	if (opts->watch_time && !(opts->query || opts->pathname))
	{
		state->errstr = "cannot use watch mode when query or file is missing";
		return false;
	}

	if (opts->csv_skip_columns_like && (opts->csv_header != '+' && !opts->query))
	{
		state->errstr = "skipping columns requires header row (option \"csv-header on\")";
		return false;
	}

	/* post parsing, checking auto setting */
	if (opts->pathname)
		state->file_format_from_suffix = get_format_type(opts->pathname);

	if (!opts->csv_format && !opts->tsv_format &&
		state->file_format_from_suffix != FILE_UNDEF &&
		!state->ignore_file_suffix)
	{
		if (state->file_format_from_suffix == FILE_CSV)
			opts->csv_format = true;
		else if (state->file_format_from_suffix == FILE_TSV)
			opts->tsv_format = true;
	}

	return true;
}
