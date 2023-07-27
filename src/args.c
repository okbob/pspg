/*-------------------------------------------------------------------------
 *
 * buildargv.c
 *	  a routines for parsing string to argc, argv format
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/buildargv.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef HAVE_SYS_UTSNAME_H

#include <sys/utsname.h>

#endif

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
	{"no-scrollbar", no_argument, 0, 41},
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
	{"on-sigint-exit", no_argument, 0, 21},
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
	{"skip-columns-like", required_argument, 0, 38},
	{"pgcli-fix", no_argument, 0, 39},
	{"style", required_argument, 0, 's'},
	{"reprint-on-exit", no_argument, 0, 'X'},
	{"ascii", no_argument, 0, 'a'},
	{"blackwhite", no_argument, 0, 'b'},
	{"freezecols", required_argument, 0, 'c'},
	{"no-xterm-mouse-mode", no_argument, 0, 40},
	{"clipboard-app", required_argument, 0, 42},
	{"no-sleep", no_argument, 0, 43},
	{"querystream", no_argument, 0, 44},
	{"menu-always", no_argument, 0, 45},
	{"no-last-row-search", no_argument, 0, 46},
	{"no-progressive-load", no_argument, 0, 47},
	{"no-implicit-stream", no_argument, 0, 48},
	{"custom-style-name", required_argument, 0, 49},
	{"highlight-odd-rec", no_argument, 0, 50},
	{"hide-header-line", no_argument, 0, 51},
	{"esc-delay", required_argument, 0, 52},
	{"on-exit-clean", no_argument, 0, 53},
	{"on-exit-reset", no_argument, 0, 54},
	{"on-exit-erase-line", no_argument, 0, 55},
	{"info", no_argument, 0, 56},
	{"on-exit-sgr0", no_argument, 0, 57},
	{"direct-color", no_argument, 0, 58},
	{"csv-trim-width", required_argument, 0, 59},
	{"csv-trim-rows", required_argument, 0, 60},
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
	char	   *copybuf;
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
			char	   *arg;
			bool		squote = false;
			bool		dquote = false;
			bool		bsquote = false;

			/* Pick off argv[argc] */
			consume_whitespace(&input);

			if (argc >= (maxargc - 1))
			{
				maxargc *= 2;
				argv = (char **) srealloc(argv, maxargc * sizeof (char *));
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

static void
print_version(void)
{
	fprintf(stdout, "pspg-%s\n", PSPG_VERSION);
}

static void
print_info(void)
{

#ifdef HAVE_SYS_UTSNAME_H

	struct utsname u_name;

#endif

	fprintf(stdout, "pspg-%s\n", PSPG_VERSION);

#ifdef HAVE_LIBREADLINE

	fprintf(stdout, "with readline (version: 0x%04x)\n", RL_READLINE_VERSION);

#else

	fprintf(stdout, "without readline\n");

#endif

#ifdef COMPILE_MENU

	fprintf(stdout, "with integrated menu\n");

#else

	fprintf(stdout, "without integrated menu\n");

#endif

#ifdef NCURSES_VERSION

	fprintf(stdout, "ncurses version: %s, patch: %ld\n",
			NCURSES_VERSION,
			(long) NCURSES_VERSION_PATCH);

#endif


#ifdef HAVE_NCURSESW

	fprintf(stdout, "ncurses with wide char support\n");

#else

	fprintf(stdout, "without wide char support\n");

#endif

#ifdef NCURSES_WIDECHAR

	fprintf(stdout, "ncurses widechar num: %d\n", NCURSES_WIDECHAR);

#endif

	fprintf(stdout, "wchar_t width: %d, max: %d\n", __SIZEOF_WCHAR_T__, __WCHAR_MAX__);

#if NCURSES_EXT_FUNCS

	fprintf(stdout, "with ncurses extended functions support no: %d\n", NCURSES_EXT_FUNCS);

#else

	fprintf(stdout, "without ncurses extended function support\n");

#endif

#ifdef NCURSES_EXT_COLORS

	fprintf(stdout, "with ncurses extended colors no: %d\n", NCURSES_EXT_COLORS);

#else

	fprintf(stdout, "without ncurses extended colors\n");

#endif

#ifdef PDCURSES

	fprintf(stdout, "with pdcurses %s\n", PDC_VERDOT);

#endif

#ifdef PDC_WIDE

	fprintf(stdout, "with pdcurses wide char support\n");

#endif

#ifdef HAVE_POSTGRESQL

	fprintf(stdout, "with postgres client integration\n");

#else

	fprintf(stdout, "without postgres client\n");

#endif

#if defined(HAVE_INOTIFY)

	fprintf(stdout, "with inotify support\n");

#else

	fprintf(stdout, "without inotify support\n");

#endif


#if defined(HAVE_KQUEUE)

	fprintf(stdout, "with kqueue support\n");

#else

	fprintf(stdout, "without kqueue support\n");

#endif

#ifdef HAVE_SYS_UTSNAME_H

	if (uname(&u_name) != -1)
	{
		fprintf(stdout, "%s %s %s %s %s\n", u_name.sysname,
										 u_name.nodename,
										 u_name.release,
										 u_name.version,
										 u_name.machine);
	}

#endif

}

bool
readargs(char **argv,
		 int argc,
		 Options *opts,
		 StateData *state)
{
	int		opt;
	int		option_index = 0;
	int		lopt;

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
					fprintf(stdout, "pspg is a Unix pager designed for table browsing.\n\n");
					fprintf(stdout, "Usage:\n");
					fprintf(stdout, "  %s [OPTION] [file]\n", argv[0]);
					fprintf(stdout, "\nGeneral options:\n");
					fprintf(stdout, "  --about                  about authors\n");
					fprintf(stdout, "  --help                   show this help\n");
					fprintf(stdout, "  -V, --version            show version\n");
					fprintf(stdout, "  --info                   show info about libraries and system\n");
					fprintf(stdout, "  --direct-color           use direct true colors\n");
					fprintf(stdout, "  -f, --file=FILE          open file\n");
					fprintf(stdout, "  -F, --quit-if-one-screen\n");
					fprintf(stdout, "                           quit if content is one screen\n");
					fprintf(stdout, "  --clipboard-app=NUM      specify app used by copy to clipboard (1, 2, 3)\n");
					fprintf(stdout, "  --esc-delay=NUM          specify escape delay in ms (-1 inf, 0 not used, )\n");
					fprintf(stdout, "  --interactive            force interactive mode\n");
					fprintf(stdout, "  --ignore_file_suffix     don't try to deduce format from file suffix\n");
					fprintf(stdout, "  --ni                     not interactive mode (only for csv and query)\n");
					fprintf(stdout, "  --no-mouse               don't use own mouse handling\n");
					fprintf(stdout, "  --no-progressive-load    don't use progressive data load\n");
					fprintf(stdout, "  --no-sigint-search-reset\n");
					fprintf(stdout, "  --no-watch-file          don't watch inotify event of file\n");
					fprintf(stdout, "                           without reset searching on sigint (CTRL C)\n");
					fprintf(stdout, "  --no-sleep               without waits against flickering\n");
					fprintf(stdout, "  --no-xterm-mouse-mode    don't use optional xterm mouse mode\n");
					fprintf(stdout, "  --only-for-tables        use std pager when content is not table\n");
					fprintf(stdout, "  --on-sigint-exit         exit on sigint(CTRL C or Escape)\n");
					fprintf(stdout, "  --pgcli-fix              try to fix some pgcli related issues\n");
					fprintf(stdout,  "  --querystream            read queries from stream forever\n");
					fprintf(stdout, "  --quit-on-f3             exit on F3 like mc viewers\n");
					fprintf(stdout, "  --rr=ROWNUM              rows reserved for specific purposes\n");
					fprintf(stdout, "  --stream                 read input forever\n");
					fprintf(stdout, "  -X, --reprint-on-exit    preserve content after exit\n");
					fprintf(stdout, "\nOutput format options:\n");
					fprintf(stdout, "  -a, --ascii decor        force ascii\n");
					fprintf(stdout, "  -b, --blackwhite         black-white style\n");
					fprintf(stdout, "  -s, --style=N            set color style number (0..%d)\n", MAX_STYLE);
					fprintf(stdout, "  --bold-labels            row, column labels use bold font\n");
					fprintf(stdout, "  --bold-cursor            cursor use bold font\n");
					fprintf(stdout, "  --border                 type of borders (0..2)\n");
					fprintf(stdout, "  --custom-style=NAME      name of custom color style\n");
					fprintf(stdout, "  --double-header          header separator uses double lines\n");
					fprintf(stdout, "  --force-uniborder        replace ascii borders by unicode borders\n");
					fprintf(stdout, "  --hide-header-line       hides header line\n");
					fprintf(stdout, "  --highlight-odd-rec      use special style for odd records\n");
					fprintf(stdout, "  --ignore-short-rows      rows with wrong column numbers are ignored\n");
					fprintf(stdout, "  --null=STRING            STRING used instead NULL\n");
					fprintf(stdout, "\nSearching options\n");
					fprintf(stdout, "  -g --hlite-search, -G --HILITE-SEARCH\n");
					fprintf(stdout, "                           don't highlight lines for searches\n");
					fprintf(stdout, "  -i --ignore-case         ignore case in searches that do not contain uppercase\n");
					fprintf(stdout, "  -I --IGNORE-CASE         ignore case in all searches\n");
					fprintf(stdout, "\nInterface options:\n");
					fprintf(stdout, "  -c, --freezecols=N       freeze N columns (0..9)\n");
					fprintf(stdout, "  --less-status-bar        status bar like less pager\n");
					fprintf(stdout, "  --line-numbers           show line number column\n");
					fprintf(stdout, "  --menu-always            show top bar menu every time\n");
					fprintf(stdout, "  --no-bars, --no-commandbar, --no-topbar\n");
					fprintf(stdout, "                           don't show bottom, top bar or both\n");
					fprintf(stdout, "  --no-cursor              row cursor will be hidden\n");
					fprintf(stdout, "  --no-last-row-search     don't use the last pattern when starting a new search\n");
					fprintf(stdout, "  --no-scrollbar           don't show scrollbar\n");
					fprintf(stdout, "  --no-sound               don't use beep when scroll is not possible\n");
					fprintf(stdout, "  --tabular-cursor         cursor is visible only when data has table format\n");
					fprintf(stdout, "  --vertical-cursor        show vertical column cursor\n");
					fprintf(stdout, "\nInput format options:\n");
					fprintf(stdout, "  --csv                    input stream has csv format\n");
					fprintf(stdout, "  --csv-separator          char used as field separator\n");
					fprintf(stdout, "  --csv-header [on/off]    specify header line usage\n");
					fprintf(stdout, "  --skip-columns-like=\"SPACE SEPARATED STRING LIST\"\n");
					fprintf(stdout, "                           columns with substr in name are ignored\n");
					fprintf(stdout, "  --csv-trim-width=NUM     trim value after NUM chars\n");
					fprintf(stdout, "  --csv-trim-rows=NUM      trim value after NUM rows\n");
					fprintf(stdout, "  --tsv                    input stream has tsv format\n");
					fprintf(stdout, "\nOn exit options:\n");
					fprintf(stdout, "  --on-exit-reset          sends reset terminal sequence \"\\33c\"\n");
					fprintf(stdout, "  --on-exit-clean          sends clean terminal sequence \"\\033[2J\"\n");
					fprintf(stdout, "  --on-exit-erase-line     sends erase line terminal sequence \"\\033[2K\\r\"\n");
					fprintf(stdout, "  --on-exit-sgr0           sends sgr0 terminal sequence \"\\033[0;10m\"\n");
					fprintf(stdout, "\nWatch mode options:\n");
					fprintf(stdout, "  -q, --query=QUERY        execute query\n");
					fprintf(stdout, "  -w, --watch time         the query (or read file) is repeated every time (sec)\n");
					fprintf(stdout, "\nConnection options:\n");
					fprintf(stdout, "  -d, --dbname=DBNAME      database name\n");
					fprintf(stdout, "  -h, --host=HOSTNAME      database server host (default: \"local socket\")\n");
					fprintf(stdout, "  -p, --port=PORT          database server port (default: \"5432\")\n");
					fprintf(stdout, "  -U, --username=USERNAME  database user name\n");
					fprintf(stdout, "  -W, --password           force password prompt\n");
					fprintf(stdout, "\nDebug options:\n");
					fprintf(stdout, "  --log=FILE               log debug info to file\n");
					fprintf(stdout, "  --wait=NUM               wait NUM seconds to allow attach from a debugger\n");
					fprintf(stdout, "\n");
					fprintf(stdout, "pspg shares lot of key commands with less pager or vi editor.\n");

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
				quiet_mode = true;
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
					fprintf(stdout, "    2017-2023 Pavel Stehule, Benesov district, Czech Republic\n\n");
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

					break;
				}
			case 29:
				opts->ignore_short_rows = true;
				break;
			case 30:
				opts->tsv_format = true;
				break;
			case 31:
				{
					char   *nullstr;
					int		size = strlen(optarg);

					nullstr = trim_quoted_str(optarg, &size);
					if (size > 255)
					{
						state->errstr = "nullstr is too long (only 255 bytes are allowed)";
						return false;
					}
					else
						opts->nullstr = sstrndup(nullstr, size);

					break;
				}
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
			case 38:
				opts->csv_skip_columns_like = sstrdup(optarg);
				break;
			case 39:
				opts->pgcli_fix = true;
				break;
			case 40:
				opts->xterm_mouse_mode =  false;
				break;
			case 41:
				opts->show_scrollbar = false;
				break;
			case 42:
				opts->clipboard_app = atoi(optarg);
				if (opts->clipboard_app < 1 || opts->clipboard_app > 3)
				{
					state->errstr = "value of clipboard_app should be 1, 2, or 3";
					return false;
				}
				break;
			case 'V':
				print_version();
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
			case 43:
				opts->no_sleep = true;
				break;
			case 44:
				opts->querystream = true;
				state->stream_mode = true;
				break;
			case 45:

#ifndef COMPILE_MENU

				state->errstr = "only one file can be browsed";
				return false;

#else

				opts->menu_always = true;
				break;

#endif

			case 46:
				opts->last_row_search = false;
				break;
			case 47:
				opts->progressive_load_mode = false;
				break;
			case 49:
				opts->custom_theme_name = sstrdup(optarg);
				break;
			case 50:
				opts->highlight_odd_rec = true;
				break;
			case 51:
				opts->hide_header_line = true;
				break;
			case 52:
				opts->esc_delay = atoi(optarg);
				break;

			case 53:
				opts->on_exit_clean = true;
				break;

			case 54:
				opts->on_exit_reset = true;
				break;

			case 55:
				opts->on_exit_erase_line = true;
				break;

			case 56:
				print_info();
				return false;

			case 57:
				opts->on_exit_sgr0 = true;
				break;

			case 58:

#ifdef NCURSES_EXT_COLORS

				opts->direct_color = true;
				break;

#else

				state->errstr = "direct color mode requires ncurses with extended function support";
				return false;

#endif

				break;

			case 59:
				lopt = atol(optarg);
				if (lopt < 0 || lopt > UINT_MAX)
				{
					state->errstr = "value for csv-trim-width is out of range (0 .. INT_MAX)";
					return false;
				}

				opts->csv_trim_width = (unsigned int) lopt;
				break;

			case 60:
				lopt = atol(optarg);
				if (lopt < 0 || lopt > UINT_MAX)
				{
					state->errstr = "value for csv-trim-rows is out of range (0 .. INT_MAX)";
					return false;
				}

				opts->csv_trim_rows = (unsigned int) lopt;
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

	if (opts->query && opts->querystream)
	{
		state->errstr = "option --query and --querystream cannot be used together";
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

	/* use progressive load mode only for data */
	if (opts->querystream)
		opts->progressive_load_mode = false;

	return true;
}
