/*-------------------------------------------------------------------------
 *
 * pspg.c
 *	  a terminal pager designed for usage from psql
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pspg.c
 *
 *-------------------------------------------------------------------------
 */
#if defined HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#include <ncurses.h>
#elif defined HAVE_CURSES_H
#include <curses.h>
#else
/* fallback */
#include <ncurses/ncurses.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <langinfo.h>
#include <libgen.h>
#include <locale.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#ifndef GWINSZ_IN_SYS_IOCTL
#include <termios.h>
#endif

#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "config.h"
#include "pspg.h"
#include "themes.h"
#include "unicode.h"

#ifdef HAVE_INOTIFY

#include <sys/inotify.h>
#include <poll.h>

#endif

#ifdef DEBUG_PIPE

/* used for mallinfo */
#include <malloc.h>

#endif

#ifdef COMPILE_MENU

#include "st_menu.h"

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

#ifdef HAVE_READLINE_HISTORY
#if defined(HAVE_READLINE_HISTORY_H)
#include <readline/history.h>
#elif defined(HAVE_HISTORY_H)
#include <history.h>
#endif
#endif

#define PSPG_VERSION "3.0.1"

/* GNU Hurd does not define MAXPATHLEN */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifdef HAVE_LIBREADLINE

static char		readline_buffer[1024];
static bool		got_readline_string;
static bool		force8bit;
static unsigned char	input;
static bool		input_avail = false;

static WINDOW  *g_bottom_bar;
static attr_t	input_attr;

#endif

static bool		handle_sigint = false;
static char		last_row_search[256];
static char		last_col_search[256];
static char		last_line[256];
static char		last_path[1025];

#ifdef HAVE_READLINE_HISTORY

static char		last_history[256];

#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

#define		USE_EXTENDED_NAMES

#ifdef DEBUG_PIPE

FILE *debug_pipe = NULL;
int	debug_eventno = 0;

#endif

static bool	press_alt = false;
static bool	got_sigint = false;
static MEVENT		event;

static long	last_watch_ms = 0;
static time_t	last_watch_sec = 0;					/* time when we did last refresh */
static bool	paused = false;							/* true, when watch mode is paused */
static const char *err = NULL;

static bool active_ncurses = false;

static int inotify_fd = -1;							/* inotify file descriptor for accessing API */
static int inotify_wd = -1;							/* file descriptor of monitored file */
static struct pollfd fds[2];
static bool stream_mode = false;
static bool is_fifo = false;

static int number_width(int num);
static int get_event(MEVENT *mevent, bool *alt, bool *sigint, bool *timeout, bool *notify, bool *reopen, int timeoutval);
static char * tilde(char *path);
static void print_log_prefix(FILE *logfile);

FILE   *logfile = NULL;

int		named_pipe_fd = 0;

#define			FILE_NOT_SET		0
#define			FILE_CSV			1
#define			FILE_TSV			2
#define			FILE_MATRIX			3

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

static void
SigintHandler(int sig_num)
{
	(void) sig_num;

	signal(SIGINT, SigintHandler);

	handle_sigint = true;
}

int
min_int(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
}

/*
 * Prints error message and stops application
 */
void
leave_ncurses(const char *str)
{
	if (active_ncurses)
		endwin();

	fprintf(stderr, "%s\n", str);
	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "leave ncurses: %s\n", str);
	}

	exit(EXIT_FAILURE);
}

void
leave_ncurses2(const char *fmt, const char *str)
{
	if (active_ncurses)
		endwin();

	fprintf(stderr, fmt, str);
	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, fmt, str);
	}

	exit(EXIT_FAILURE);
}

inline void
log_writeln(const char *str)
{
	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "%s\n", str);
	}
}

static void
current_time(time_t *sec, long *ms)
{
	struct timespec spec;

	clock_gettime(CLOCK_MONOTONIC, &spec);
	*ms = roundl(spec.tv_nsec / 1.0e6);
	*sec = spec.tv_sec;
}

#define time_diff(s1, ms1, s2, ms2)		((s1 - s2) * 1000 + ms1 - ms2)

#ifdef DEBUG_PIPE

static void
print_duration(time_t start_sec, long start_ms, const char *label)
{
	time_t		end_sec;
	long		end_ms;

	current_time(&end_sec, &end_ms);

	fprintf(debug_pipe, "duration of \"%s\" is %ld ms\n",
			label,
			time_diff(end_sec, end_ms, start_sec, start_ms));
}

#endif

/*
 * Case insensitive string comparation.
 */
static bool
nstreq(const char *str1, const char *str2)
{
	while (*str1 != '\0')
	{
		if (*str2 == '\0')
			return false;

		if (toupper(*str1++) != toupper(*str2++))
			return false;
	}

	return *str2 == '\0';
}

/*
 * special case insensitive searching routines
 */
const char *
nstrstr(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0, f2 = 0;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		if (*haystack_cur == '\0')
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			f1 = toupper(*needle_cur);
		}

		f2 = toupper(*haystack_cur);
		if (f1 == f2)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}

static const char *
nstrstr_with_sizes(const char *haystack,
				   const int haystack_size,
				   const char *needle,
				   int needle_size)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	const char *haystack_end, *needle_end;
	int		f1 = 0, f2 = 0;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	haystack_end = haystack + haystack_size;
	needle_end = needle + needle_size;

	while (needle_cur < needle_end)
	{
		if (haystack_cur == haystack_end)
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			f1 = toupper(*needle_cur);
		}

		f2 = toupper(*haystack_cur);
		if (f1 == f2)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}

/*
 * Special string searching, lower chars are case insensitive,
 * upper chars are case sensitive.
 */
const char *
nstrstr_ignore_lower_case(const char *haystack, const char *needle)
{
	const char *haystack_cur, *needle_cur, *needle_prev;
	int		f1 = 0;
	bool	eq;

	needle_cur = needle;
	needle_prev = NULL;
	haystack_cur = haystack;

	while (*needle_cur != '\0')
	{
		bool	needle_char_is_upper = false; /* be compiler quiet */

		if (*haystack_cur == '\0')
			return NULL;

		if (needle_prev != needle_cur)
		{
			needle_prev = needle_cur;
			needle_char_is_upper = isupper(*needle_cur);
			f1 = toupper(*needle_cur);
		}

		if (needle_char_is_upper)
		{
			/* case sensitive */
			eq = *haystack_cur == *needle_cur;
		}
		else
		{
			/* case insensitive */
			eq = f1 == toupper(*haystack_cur);
		}

		if (eq)
		{
			needle_cur += 1;
			haystack_cur += 1;
		}
		else
		{
			needle_cur = needle;
			haystack_cur = haystack += 1;
		}
	}

	return haystack;
}

/*
 * Multiple used block - searching in string based on configuration
 */
const char *
pspg_search(Options *opts, ScrDesc *scrdesc, const char *str)
{
	bool	ignore_case = opts->ignore_case;
	bool	ignore_lower_case = opts->ignore_lower_case;
	bool	force8bit = opts->force8bit;
	bool	has_upperchr = scrdesc->has_upperchr;
	const char *searchterm = scrdesc->searchterm;
	const char *result;

	if (ignore_case || (ignore_lower_case && !has_upperchr))
	{
		result = force8bit ? nstrstr(str, searchterm) : utf8_nstrstr(str, searchterm);
	}
	else if (ignore_lower_case && has_upperchr)
	{
		result = force8bit ? nstrstr_ignore_lower_case(str, searchterm) : utf8_nstrstr_ignore_lower_case(str, searchterm);
	}
	else
		result = strstr(str, searchterm);

	return result;
}

/*
 * Translate from UTF8 to semantic characters.
 */
static bool
translate_headline(Options *opts, DataDesc *desc)
{
	char   *srcptr;
	char   *destptr;
	char   *last_black_char = NULL;
	bool	broken_format = false;
	int		processed_chars = 0;
	bool	is_expanded_info = false;
	bool	force8bit = opts->force8bit;

	srcptr = desc->headline;
	destptr = malloc(desc->headline_size + 2);
	if (!destptr)
	{
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}

	memset(destptr, 0, desc->headline_size + 2);
	desc->headline_transl = destptr;

	desc->linestyle = 'a';
	desc->border_type = 0;

	desc->expanded_info_minx = -1;

	while (*srcptr != '\0' && *srcptr != '\n' && *srcptr != '\r')
	{
		/* only spaces can be after known right border */
		if (last_black_char != NULL && *last_black_char == 'R')
		{
			if (*srcptr != ' ')
			{
				broken_format = true;
				break;
			}
		}

		if (*srcptr != ' ')
			last_black_char = destptr;

		if (desc->is_expanded_mode && *srcptr == '[')
		{
			if (desc->expanded_info_minx != -1)
			{
				broken_format = true;
				break;
			}

			/* entry to expanded info mode */
			is_expanded_info = true;
			desc->expanded_info_minx = processed_chars;

			*destptr++ = 'd';
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
		}
		else if (is_expanded_info)
		{
			if (*srcptr == ']')
			{
				is_expanded_info = false;
			}
			*destptr++ = 'd';
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
		}
		else if (strncmp(srcptr, "\342\224\214", 3) == 0 || /* ┌ */
				  strncmp(srcptr, "\342\225\224", 3) == 0)   /* ╔ */
		{
			/* should be expanded mode */
			if (processed_chars > 0 || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\220", 3) == 0 || /* ┐ */
				 strncmp(srcptr, "\342\225\227", 3) == 0)   /* ╗ */
		{
			if (desc->linestyle != 'u' || desc->border_type != 2 ||
				!desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\254", 3) == 0 || /* ┬╤ */
				 strncmp(srcptr, "\342\225\244", 3) == 0 ||
				 strncmp(srcptr, "\342\225\245", 3) == 0 || /* ╥╦ */
				 strncmp(srcptr, "\342\225\246", 3) == 0)
		{
			if (desc->linestyle != 'u' || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;

			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\234", 3) == 0 || /* ├╟ */
				 strncmp(srcptr, "\342\225\237", 3) == 0 ||
				 strncmp(srcptr, "\342\225\236", 3) == 0 || /* ╞╠ */
				 strncmp(srcptr, "\342\225\240", 3) == 0)
		{
			if (processed_chars > 0)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\244", 3) == 0 || /* ┤╢ */
				 strncmp(srcptr, "\342\225\242", 3) == 0 ||
				 strncmp(srcptr, "\342\225\241", 3) == 0 || /* ╡╣ */
				 strncmp(srcptr, "\342\225\243", 3) == 0)
		{
			if (desc->linestyle != 'u' || desc->border_type != 2)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\274", 3) == 0 || /* ┼╪ */
				 strncmp(srcptr, "\342\225\252", 3) == 0 ||
				 strncmp(srcptr, "\342\225\253", 3) == 0 || /* ╫╬ */
				 strncmp(srcptr, "\342\225\254", 3) == 0)
		{
			if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;
			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\200", 3) == 0 || /* ─ */
				 strncmp(srcptr, "\342\225\220", 3) == 0) /* ═ */
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'u';
			}
			else if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 3;
		}
		else if (*srcptr == '+')
		{
			if (processed_chars == 0)
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
			}
			else
			{
				if (desc->linestyle != 'a')
				{
					broken_format = true;
					break;
				}
				if (desc->border_type == 0)
					desc->border_type = 1;

				*destptr++ = (srcptr[1] == '-') ? 'I' : 'R';
			}
			srcptr += 1;
		}
		else if (*srcptr == '-')
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'a';
			}
			else if (desc->linestyle != 'a')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 1;
		}
		else if (*srcptr == '|')
		{
			if (processed_chars == 0 && srcptr[1] == '-')
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
				desc->is_pgcli_fmt = true;
			}
			else if (processed_chars > 0 && desc->is_pgcli_fmt && srcptr[-1] == '-')
			{
				*destptr++ = 'R';
			}
			else
			{
				broken_format = true;
				break;
			}
			srcptr += 1;
		}
		else if (*srcptr == ' ')
		{
			if (desc->border_type != 0)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'I';
			srcptr += 1;
		}
		else
		{
			broken_format = true;
			break;
		}
		processed_chars += 1;
	}

	/* should not be - unclosed header */
	if (is_expanded_info)
		broken_format = true;
	else if (desc->is_expanded_mode && desc->expanded_info_minx == -1)
		broken_format = true;

	if (!broken_format)
	{
		char	   *namesline = desc->namesline;
		char	   *first_char = NULL;				/* first non space char of column name */
		int			offset;
		char	   *ptr;
		int			i;

		/* Move right corner more right */
		if (desc->border_type == 0)
		{
			last_black_char[0] = 'd';
			last_black_char[1] = 'R';
			last_black_char[3] = '\0';
		}

		/* trim ending spaces */
		else if (last_black_char != 0)
		{
			last_black_char[1] = '\0';
		}

		desc->headline_char_size = strlen(desc->headline_transl);

		desc->columns = 1;

		ptr = desc->headline_transl;
		while (*ptr)
		{
			if (*ptr++ == 'I')
				desc->columns += 1;
		}

		desc->cranges = malloc(desc->columns * sizeof(CRange));
		if (!desc->cranges)
		{
			fprintf(stderr, "out of memory\n");
			exit(EXIT_FAILURE);
		}

		i = 0; offset = 0;
		ptr = desc->headline_transl;
		desc->cranges[0].xmin = 0;
		desc->cranges[0].name_pos = -1;
		desc->cranges[0].name_size = -1;

		while (*ptr)
		{
			char	   *nextchar = NULL;
			int			display_width;

			if (namesline)
			{
				/* invalidate namesline if there are not good enough chars */
				if (!*namesline)
				{
					namesline = NULL;
					nextchar = NULL;
				}
				else
					nextchar = namesline + (opts->force8bit ? 1 : utf8charlen(*namesline));
			}

			if (*ptr == 'I')
			{
				desc->cranges[i++].xmax = offset;
				desc->cranges[i].xmin = offset;
				desc->cranges[i].name_pos = -1;
				desc->cranges[i].name_size = -1;
			}
			else if (*ptr == 'd')
			{
				if (namesline && *namesline != ' ')
				{
					if (desc->cranges[i].name_pos == -1)
					{
						first_char = namesline;
						desc->cranges[i].name_pos = namesline - desc->namesline;
						desc->cranges[i].name_size = nextchar - namesline;
						first_char = namesline;
					}
					else
						desc->cranges[i].name_size = nextchar - first_char;
				}
			}

			/* possibly some chars can hold more display possitions */
			if (namesline)
			{
				display_width = utf_dsplen(namesline);
				namesline = nextchar;
			}
			else
				display_width = 1;

			offset += display_width;
			ptr += display_width;
		}

		desc->cranges[i].xmax = offset - 1;

		if (!namesline)
			desc->namesline = NULL;

		/*
		 * New PostgreSQL system tables contains visible oid columns. I would to
		 * detect this situation and increase by one default freezed columns. So
		 * second column (with name) will be freezed by default too.
		 */
		if (desc->namesline && desc->columns >= 2)
		{
			if (desc->cranges[0].name_size == 3 &&
					nstrstr_with_sizes(desc->namesline + desc->cranges[0].name_pos,
									   desc->cranges[0].name_size,
									   "oid",
									   3))
			{
				if (desc->cranges[1].name_size > 4 &&
						nstrstr_with_sizes(desc->namesline + desc->cranges[1].name_pos + desc->cranges[1].name_size - 4,
										   4, "name", 4))
					desc->oid_name_table = true;
			}
		}

		return true;
	}

	free(desc->headline_transl);
	desc->headline_transl = NULL;

	return false;
}

/*
 * Cut text from column and translate it to number.
 */
static bool
cut_text(char *str, int xmin, int xmax, bool border0, bool force8bit, char **result)
{
#define TEXT_STACK_BUFFER_SIZE		1024

	if (str)
	{
		char	   *_str = NULL;
		char	   *after_last_nospc = NULL;
		int			pos = 0;
		int			charlen;
		bool		skip_left_spaces = true;

		while (*str)
		{
			charlen = utf8charlen(*str);

			if (pos > xmin || (border0 && pos >= xmin))
			{
				if (skip_left_spaces)
				{
					if (*str == ' ')
					{
						pos += 1;
						str += 1;
						continue;
					}

					/* first nspc char */
					skip_left_spaces = false;
					_str = str;
				}
			}

			if (*str != ' ')
				after_last_nospc = str + charlen;

			pos += utf_dsplen(str);
			str += charlen;

			if (pos >= xmax)
				break;
		}

		if (_str != NULL)
		{
			char		buffer[TEXT_STACK_BUFFER_SIZE];
			char	   *dynbuf = NULL;
			char	   *cstr = NULL;
			int			size;
			int			dynbuf_size = 0;

			cstr = strndup(_str, after_last_nospc - _str);
			if (!cstr)
				leave_ncurses("out of memory");

			if (force8bit)
			{
				*result = cstr;
				return true;
			}

			errno = 0;
			size = strxfrm(buffer, (const char *) cstr, 1024);
			if (errno != 0)
			{
				/* cannot to sort this string */
				free(cstr);
				return false;
			}

			if (size > TEXT_STACK_BUFFER_SIZE - 1)
			{
				while (size > dynbuf_size)
				{
					if (dynbuf)
						free(dynbuf);

					dynbuf_size = size + 1;
					dynbuf = malloc(dynbuf_size);
					if (!dynbuf)
						leave_ncurses("out of memory");

					errno = 0;
					size = strxfrm(dynbuf, cstr, dynbuf_size);
					if (errno != 0)
					{
						/* cannot to sort this string */
						free(cstr);
						return false;
					}
				}
			}

			free(cstr);

			if (!dynbuf)
			{
				dynbuf = strdup(buffer);
				if (!dynbuf)
					leave_ncurses("out of memory");
			}

			*result = dynbuf;

			return true;
		}
	}

	*result = NULL;

	return false;
}

/*
 * Try to cut numeric (double) value from row defined by specified xmin, xmax positions.
 * Units (bytes, kB, MB, GB, TB) are supported. Returns true, when returned value is valid.
 */
static bool
cut_numeric_value(char *str, int xmin, int xmax, double *d, bool border0, bool *isnull, char **nullstr)
{

#define BUFFER_MAX_SIZE			101

	char		buffer[BUFFER_MAX_SIZE];
	char	   *buffptr;
	char	   *after_last_nospace = NULL;
	char	   *first_nospace_nodigit = NULL;
	char		decimal_point = '\0';
	bool		only_digits = false;
	bool		only_digits_with_point = false;
	bool		skip_initial_spaces = true;
	int			x = 0;
	long		mp = 1;

	*isnull = false;

	if (str)
	{
		after_last_nospace = buffptr = buffer;
		memset(buffer, 0, BUFFER_MAX_SIZE);

		while (*str)
		{
			int		charlen = utf8charlen(*str);

			if (x > xmin || (border0 && x >= xmin))
			{
				char	c =  *str;

				if (skip_initial_spaces)
				{
					if (c == ' ')
					{
						x += 1;
						str += 1;
						continue;
					}

					/* first char should be a digit */
					if (!isdigit(c))
					{
						char	   *_nullstr = *nullstr;
						size_t		len;
						char	   *saved_str = str;

						after_last_nospace = saved_str;

						/*
						 * We should to check nullstr if exists, or we should to save
						 * this string as nullstr.
						 */
						while (*str)
						{
							if (*str != ' ')
								after_last_nospace = str + charlen;

							x += utf_dsplen(str);
							str += charlen;

							if (x >= xmax)
								break;

							if (*str)
								charlen = utf8charlen(*str);
						}

						len = after_last_nospace - saved_str;

						if (_nullstr)
						{
							if (strlen(_nullstr) == len)
								*isnull = strncmp(_nullstr, saved_str, len) == 0;
							else
								*isnull = false;
						}
						else
						{
							_nullstr = malloc(len + 1);
							if (!_nullstr)
								leave_ncurses("out of memory");

							memcpy(_nullstr, saved_str, len);
							_nullstr[len] = '\0';

							*isnull = true;
							*nullstr = _nullstr;
						}

						return false;
					}

					skip_initial_spaces = false;
					only_digits = true;
				}

				memcpy(buffptr, str, charlen);

				/* trim from right */
				if (c != ' ')
				{
					bool	only_digits_prev = only_digits;
					bool	only_digits_with_point_prev = only_digits_with_point;

					after_last_nospace = buffptr + charlen;
					if (after_last_nospace - buffer > (BUFFER_MAX_SIZE - 1))
					{
						/* too long string - should not be translated to number */
						return false;
					}

					if (c == '.' || c == ',')
					{
						if (only_digits)
						{
							only_digits = false;
							only_digits_with_point = true;
							decimal_point = c;
						}
						else
							return false;
					}
					else if (!isdigit(c))
					{
						only_digits = false;
						only_digits_with_point = false;
					}

					/* Save point of chage between digits and other */
					if ((only_digits_prev || only_digits_with_point_prev) &&
					   !(only_digits || only_digits_with_point))
					{
						first_nospace_nodigit = buffptr;
					}
				}
				buffptr += charlen;
			}

			x += utf_dsplen(str);
			str += charlen;

			if (x >= xmax)
				break;
		} /* while (*str) */

		/* trim spaces from right */
		*after_last_nospace = '\0';

		if (first_nospace_nodigit)
		{
			if (nstreq(first_nospace_nodigit, "bytes"))
				mp = 1l;
			else if (nstreq(first_nospace_nodigit, "kB"))
				mp = 1024l;
			else if (nstreq(first_nospace_nodigit, "MB"))
				mp = 1024l * 1024;
			else if (nstreq(first_nospace_nodigit, "GB"))
				mp = 1024l * 1024 * 1024;
			else if (nstreq(first_nospace_nodigit, "TB"))
				mp = 1024l * 1024 * 1024 * 1024;
			else
				/* unknown unit */
				return false;

			*first_nospace_nodigit = '\0';
		}

		if (decimal_point == ',')
		{
			char   *ptr = buffer;

			while (*ptr)
			{
				if (*ptr == ',')
					*ptr = '.';
				ptr += 1;
			}
		}

		errno = 0;
		*d = strtod(buffer, NULL);
		if (errno == 0)
		{
			*d = *d * mp;
			return true;
		}
	}

	return false;
}

/*
 * Trim footer rows - We should to trim footer rows and calculate footer_char_size
 */
static void
trim_footer_rows(Options *opts, DataDesc *desc)
{
	if (desc->headline_transl != NULL && desc->footer_row != -1)
	{
		LineBuffer *rows = &desc->rows;
		int			rowidx = 0;
		int			rownum;

		desc->footer_char_size = 0;

		for (rownum = 0, rowidx = 0; rownum < desc->footer_row; rownum++, rowidx++)
		{
			if (rowidx == 1000)
			{
				rows = rows->next;
				rowidx = 0;
			}
		}

		while (rows != NULL && rowidx < rows->nrows)
		{
			char   *line;
			char   *endptr;
			int		len;

			if (rowidx == 1000)
			{
				rows = rows->next;
				rowidx = 0;
				continue;
			}

			line = rows->rows[rowidx++];
			endptr = line + strlen(line) - 1;

			while (endptr > line)
			{
				if (*endptr != ' ')
				{
					endptr[1] = '\0';
					break;
				}
				endptr -= 1;
			}

			len = opts->force8bit ? strlen(line) : utf8len(line);
			if (len > desc->footer_char_size)
				desc->footer_char_size = len;
		}
	}
	else
		desc->footer_char_size = desc->maxx;
}

/*
 * Returns true when char is left upper corner
 */
static bool
isTopLeftChar(char *str)
{
	const char *u1 = "\342\224\214";
	const char *u2 = "\342\225\224";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * Returns true when char is top left header char
 */
static bool
isHeadLeftChar(char *str)
{
	const char *u1 = "\342\224\200";
	const char *u2 = "\342\225\220";
	const char *u3 = "\342\225\236";
	const char *u4 = "\342\224\234";
	const char *u5 = "\342\225\240";
	const char *u6 = "\342\225\237";

	/* ascii */
	if ((str[0] == '+' || str[0] == '-') && str[1] == '-')
		return true;

	/* pgcli fmt */
	if (str[0] == '|' && str[1] == '-')
		return true;

	/* expanded border 1 */
	if (str[0] == '-' && str[1] == '[')
		return true;

	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;
	if (strncmp(str, u3, 3) == 0)
		return true;
	if (strncmp(str, u4, 3) == 0)
		return true;
	if (strncmp(str, u5, 3) == 0)
		return true;
	if (strncmp(str, u6, 3) == 0)
		return true;

	return false;
}

/*
 * Returns true when char is bottom left corner
 */
static bool
isBottomLeftChar(char *str)
{
	const char *u1 = "\342\224\224";
	const char *u2 = "\342\225\232";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * detect different faces of headline in extended mode
 */
bool
is_expanded_header(Options *opts, char *str, int *ei_minx, int *ei_maxx)
{
	int		pos = 0;

	if (*str == '+')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\214", 3) == 0 || /* ┌ */
			 strncmp(str, "\342\225\224", 3) == 0 || /* ╔ */
			 strncmp(str, "\342\224\234", 3) == 0 || /* ├╟ */
			 strncmp(str, "\342\225\237", 3) == 0 ||
			 strncmp(str, "\342\225\236", 3) == 0 || /* ╞╠ */
			 strncmp(str, "\342\225\240", 3) == 0)
	{
		str += 3;
		pos += 1;
	}

	if (*str == '-')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\200", 3) == 0 || /* ─ */
			 strncmp(str, "\342\225\220", 3) == 0) /* ═ */
	{
		str += 3;
		pos += 1;
	}

	if (strncmp(str, "[ ", 2) != 0)
		return false;

	if (ei_minx != NULL && ei_maxx != NULL)
	{
		pos += 2;
		str += 2;
		*ei_minx = pos - 1;

		while (*str != ']' && *str != '\0')
		{
			pos += 1;
			str += opts->force8bit ? 1 : utf8charlen(*str);
		}

		*ei_maxx = pos - 1;
	}

	return true;
}

/*
 * Returns true when char (multibyte char) correspond with symbols for
 * line continuation.
 */
static bool
is_line_continuation_char(char *str, DataDesc *desc)
{
	const char *u1 = "\342\206\265";	/* ↵ */
	const char *u2 = "\342\200\246";	/* … */

	if (desc->linestyle == 'a')
	{
		return str[0] == '+' || str[0] == '.';
	}
	else
	{
		/* desc->linestyle == 'u'; */
		return strncmp(str, u1, 3) == 0 || strncmp(str, u2, 3) == 0;
	}
}

/*
 * Copy trimmed string
 */
static void
strncpytrim(Options *opts, char *dest, const char *src,
			size_t ndest, size_t nsrc)
{
	const char *endptr;

	endptr = src + nsrc - 1;

	/* skip trailing spaces */
	while (*src == ' ')
	{
		if (nsrc-- <= 0)
			break;
		src++;
	}

	/* skip ending spaces */
	while (*endptr == ' ')
	{
		if (nsrc-- <= 0)
			break;
		endptr--;
	}

	while(nsrc > 0)
	{
		size_t	clen;

		if (*src == '\0')
			break;

		clen = (size_t) opts->force8bit ? 1 : utf8charlen(*src);
		if (clen <= ndest && clen <= nsrc)
		{
			size_t		i;

			for (i = 0; i < clen; i++)
			{
				*dest++ = *src++;
				ndest--;
				nsrc--;
			}
		}
		else
			break;
	}

	*dest = '\0';
}

#define STATBUF_SIZE		(10 * 1024)

static size_t
_getline(char **lineptr, size_t *n, FILE *fp, bool is_blocking, bool wait_on_data)
{
	if (!is_blocking)
	{
		if (!feof(fp) && !ferror(fp))
		{
			char   *dynbuf = NULL;
			char	statbuf[STATBUF_SIZE];
			int		fetched_chars = 0;
			int		bufsize = STATBUF_SIZE;
			char   *writeptr;
			int		rc;

			writeptr = statbuf;

			for (;;)
			{
				char	locbuf[2048];
				char   *result;
				int		_errno;
				struct pollfd fds[1];

				fds[0].fd = fileno(fp);
				fds[0].events = POLLIN;

				errno = 0;

				result = fgets(locbuf, 2048, fp);
				_errno = errno;

				if (result)
				{
					int	len = strlen(result);

					if (dynbuf)
					{
						if (fetched_chars + len + 1 >= bufsize)
						{
							bufsize += 4096;
							dynbuf = realloc(dynbuf, bufsize);
							if (dynbuf == NULL)
								return -1;

							writeptr = dynbuf + fetched_chars;
						}
					}
					else
					{
						if (fetched_chars + len + 1 >= bufsize)
						{
							bufsize += 4096;
							dynbuf = malloc(bufsize);
							if (dynbuf == NULL)
								return -1;

							memcpy(dynbuf, statbuf, fetched_chars);
							writeptr = dynbuf + fetched_chars;
						}
					}

					memcpy(writeptr, result, len + 1);
					writeptr += len;
					fetched_chars += len;

					if (result[len - 1] == '\n')
						break;

					errno = _errno;
				}

				if (errno)
				{
					if (feof(fp))
					{
						break;
					}
					else if (errno == EAGAIN)
					{
						if (fetched_chars == 0 && !wait_on_data)
						{
							return -1;
						}

						rc = poll(fds, 1, -1);
						if (rc == -1)
						{
							if (logfile)
								fprintf(logfile, "POLL error\n");

							usleep(100);
						}

						clearerr(fp);
						continue;
					}
					else
					{
						free(dynbuf);
						return -1;
					}
				}
			}

			if (fetched_chars > 0)
			{
				char	   *result;

				result = malloc(fetched_chars + 1);
				if (result == NULL)
					return -1;

				if (dynbuf)
					memcpy(result, dynbuf, fetched_chars + 1);
				else
					memcpy(result, statbuf, fetched_chars + 1);

				*lineptr = result;
				*n = fetched_chars + 1;

				free(dynbuf);

				return fetched_chars;
			}
		}

		return -1;
	}
	else
	{
		ssize_t result;

		result = getline(lineptr, n, fp);
		if (result < 0)
		{
			free(*lineptr);
			*lineptr = NULL;
		}

		return result;
	}
}

/*
 * Read data from file and fill DataDesc.
 */
static bool
readfile(FILE *fp, Options *opts, DataDesc *desc)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	LineBuffer *rows;
	bool		is_blocking;

#ifdef DEBUG_PIPE

	time_t		start_sec;
	long		start_ms;

	fprintf(debug_pipe, "readfile start\n");
	current_time(&start_sec, &start_ms);

#endif

	/* safe reset */
	desc->filename[0] = '\0';

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

	is_blocking = !(fcntl(fileno(fp), F_GETFL) & O_NONBLOCK);

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
	rows = &desc->rows;
	desc->rows.prev = NULL;
	desc->oid_name_table = false;
	desc->multilines_already_tested = false;

	errno = 0;
	read = _getline(&line, &len, fp, is_blocking, false);
	if (read == -1)
		return false;

	do
	{
		int		clen;

		/* In streaming mode exit when you find empty row */
		if (stream_mode && read == 0)
		{
			free(line);

			/* ignore this line if we are on second line - probably watch mode */
			if (nrows == 1)
				goto next_row;

			break;
		}

		if (line[read - 1] == '\n')
		{
			line[read - 1] = '\0';
			read -= 1;
		}

		clen = utf_string_dsplen(line, read);

		if (rows->nrows == 1000)
		{
			LineBuffer *newrows = malloc(sizeof(LineBuffer));
			if (!newrows)
			{
				fprintf(stderr, "out of memory\n");
				exit(EXIT_FAILURE);
			}

			memset(newrows, 0, sizeof(LineBuffer));
			rows->next = newrows;
			newrows->prev = rows;
			rows = newrows;
		}

		rows->rows[rows->nrows++] = line;

		/* save possible table name */
		if (nrows == 0 && !isTopLeftChar(line))
		{
			strncpytrim(opts, desc->title, line, 63, read);
			desc->title_rows = 1;
		}

		if (desc->border_head_row == -1 && desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
			desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;

			if (!desc->is_expanded_mode)
				desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);

			/* title surely doesn't it there */
			if ((!desc->is_expanded_mode && nrows == 1) ||
			    (desc->is_expanded_mode && nrows == 0))
			{
				desc->title[0] = '\0';
				desc->title_rows = 0;
			}
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row == -1 && isBottomLeftChar(line))
		{
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row != -1 && desc->footer_row == -1)
		{
			desc->footer_row = nrows;
		}
		else if (desc->is_expanded_mode && isBottomLeftChar(line))
		{
			/* Outer border is repeated in expanded mode, use last detected row */
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}

		if (!desc->is_expanded_mode && desc->border_head_row != -1 && desc->border_head_row < nrows
			 && desc->alt_footer_row == -1)
		{
			if (*line != '\0' && *line != ' ')
				desc->alt_footer_row = nrows;
		}

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > desc->maxx + 1)
			desc->maxx = clen - 1;

		if ((int) clen > 1 || (clen == 1 && *line != '\n'))
			desc->last_row = nrows;

		nrows += 1;

next_row:

		line = NULL;

		read = _getline(&line, &len, fp, is_blocking, true);
	} while (read != -1);

	if (errno && errno != EAGAIN)
	{

#ifdef DEBUG_PIPE

		fprintf(stderr, "cannot to read file: %s\n", strerror(errno));

#endif

		return false;

	}

	desc->total_rows = nrows;

	/*
	 * border headline cannot be higher than 1000, to simply find it
	 * in first row block. Higher number is surelly wrong, probably
	 * some comment.
	 */
	if (desc->border_top_row >= 1000)
		desc->border_top_row = -1;
	if (desc->border_head_row >= 1000)
		desc->border_head_row = -1;

	if (desc->last_row != -1)
		desc->maxy = desc->last_row;

	desc->headline_char_size = 0;

	if (desc->border_head_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_head_row];
		desc->headline_size = strlen(desc->headline);

		if (desc->last_data_row == -1)
			desc->last_data_row = desc->last_row - 1;

		if (desc->border_head_row >= 1)
			desc->namesline = desc->rows.rows[desc->border_head_row - 1];

	}
	else if (desc->is_expanded_mode && desc->border_top_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_top_row];
		desc->headline_size = strlen(desc->headline);
	}
	else
	{
		desc->headline = NULL;
		desc->headline_size = 0;
		desc->headline_char_size = 0;

		/* there are not a data set */
		desc->last_data_row = desc->last_row;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

#ifdef DEBUG_PIPE

	print_duration(start_sec, start_ms, "read file");

#endif

	/* clean event buffer */
	if (inotify_fd >= 0)
		lseek(inotify_fd, 0, SEEK_END);

	return true;
}

/*
 * Prepare order map - it is used for printing data in different than
 * original order. "sbcn" - sort by column number
 */
static void
update_order_map(Options *opts, ScrDesc *scrdesc, DataDesc *desc, int sbcn, bool desc_sort)
{
	LineBuffer	   *lnb = &desc->rows;
	char		   *nullstr = NULL;
	int				xmin, xmax;
	int				lineno = 0;
	bool			continual_line = false;
	bool			has_multilines = false;
	bool			isnull;
	bool			detect_string_column = false;
	bool			border0 = (desc->border_type == 0);
	bool			border1 = (desc->border_type == 1);
	bool			border2 = (desc->border_type == 2);
	SortData	   *sortbuf;
	int				sortbuf_pos = 0;
	int			i;

	xmin = desc->cranges[sbcn - 1].xmin;
	xmax = desc->cranges[sbcn - 1].xmax;

	sortbuf = malloc(desc->total_rows * sizeof(SortData));
	if (!sortbuf)
		leave_ncurses("out of memory");

	/* first time we should to detect multilines */
	if (!desc->multilines_already_tested)
	{
		desc->multilines_already_tested = true;

		lnb = &desc->rows;
		lineno = 0;

		while (lnb)
		{
			for (i = 0; i < lnb->nrows; i++)
			{
				if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
				{
					char   *str = lnb->rows[i];
					bool	found_continuation_symbol = false;
					int		j = 0;

					while (j < desc->headline_char_size)
					{
						if (border0)
						{
							/* border 0, last continuation symbol is after headline */
							if (j + 1 == desc->headline_char_size)
							{
								char	*sym;

								sym = str + (opts->force8bit ? 1 : utf8charlen(*str));
								if (*sym != '\0')
									found_continuation_symbol = is_line_continuation_char(sym, desc);
							}
							else if (desc->headline_transl[j] == 'I')
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}
						else if (border1)
						{
							if ((j + 1 < desc->headline_char_size && desc->headline_transl[j + 1] == 'I') ||
									  (j + 1 == desc->headline_char_size))
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}
						else if (border2)
						{
							if ((j + 1 < desc->headline_char_size) &&
									(desc->headline_transl[j + 1] == 'I' || desc->headline_transl[j + 1] == 'R'))
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}

						if (found_continuation_symbol)
							break;

						j += opts->force8bit ? 1 : utf_dsplen(str);
						str += opts->force8bit ? 1 : utf8charlen(*str);
					}

					if (found_continuation_symbol)
					{
						if (lnb->lineinfo == NULL)
						{
							lnb->lineinfo = malloc(1000 * sizeof(LineInfo));
							if (lnb->lineinfo == NULL)
								leave_ncurses("out of memory");

							memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));
						}

						lnb->lineinfo[i].mask ^= LINEINFO_CONTINUATION;
						has_multilines = true;
					}
				}

				lineno += 1;
			}
			lnb = lnb->next;
		}
	}

	lnb = &desc->rows;
	lineno = 0;
	sortbuf_pos = 0;

	if (!desc->order_map)
	{
		desc->order_map = malloc(desc->total_rows * sizeof(MappedLine));
		if (!desc->order_map)
			leave_ncurses("out of memory");
	}

	/*
	 * There are two possible sorting methods: numeric or string.
	 * We can try numeric sort first if all values are numbers or
	 * just only one type of string value (like NULL string). This
	 * value can be repeated,
	 *
	 * When there are more different strings, then start again and
	 * use string sort.
	 */
	while (lnb)
	{
		for (i = 0; i < lnb->nrows; i++)
		{
			desc->order_map[lineno].lnb = lnb;
			desc->order_map[lineno].lnb_row = i;

			if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
			{
				if (!continual_line)
				{
					sortbuf[sortbuf_pos].lnb = lnb;
					sortbuf[sortbuf_pos].lnb_row = i;
					sortbuf[sortbuf_pos].strxfrm = NULL;

					if (cut_numeric_value(lnb->rows[i],
										   xmin, xmax,
										   &sortbuf[sortbuf_pos].d,
										   border0,
										   &isnull,
										   &nullstr))
						sortbuf[sortbuf_pos++].info = INFO_DOUBLE;
					else
					{
						sortbuf[sortbuf_pos++].info = INFO_UNKNOWN;
						if (!isnull)
						{
							detect_string_column = true;
							goto sort_by_string;
						}
					}
				}

				if (has_multilines)
				{
					continual_line = (lnb->lineinfo &&
									  (lnb->lineinfo[i].mask & LINEINFO_CONTINUATION));
				}
			}

			lineno += 1;
		}

		lnb = lnb->next;
	}

sort_by_string:

	free(nullstr);

	if (detect_string_column)
	{
		/* read data again and use nls_string */
		lnb = &desc->rows;
		lineno = 0;
		sortbuf_pos = 0;
		while (lnb)
		{
			for (i = 0; i < lnb->nrows; i++)
			{
				desc->order_map[lineno].lnb = lnb;
				desc->order_map[lineno].lnb_row = i;

				if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
				{
					if (!continual_line)
					{
						sortbuf[sortbuf_pos].lnb = lnb;
						sortbuf[sortbuf_pos].lnb_row = i;
						sortbuf[sortbuf_pos].d = 0.0;

						if (cut_text(lnb->rows[i], xmin, xmax, border0, opts->force8bit, &sortbuf[sortbuf_pos].strxfrm))
							sortbuf[sortbuf_pos++].info = INFO_STRXFRM;
						else
							sortbuf[sortbuf_pos++].info = INFO_UNKNOWN;		/* empty string */
					}

					if (has_multilines)
					{
						continual_line =  (lnb->lineinfo &&
										   (lnb->lineinfo[i].mask & LINEINFO_CONTINUATION));
					}
				}

				lineno += 1;
			}
			lnb = lnb->next;
		}
	}

	if (lineno != desc->total_rows)
		leave_ncurses("unexpected processed rows after sort prepare");

	if (detect_string_column)
		sort_column_text(sortbuf, sortbuf_pos, desc_sort);
	else
		sort_column_num(sortbuf, sortbuf_pos, desc_sort);

	lineno = desc->first_data_row;

	for (i = 0; i < sortbuf_pos; i++)
	{
		desc->order_map[lineno].lnb = sortbuf[i].lnb;
		desc->order_map[lineno].lnb_row = sortbuf[i].lnb_row;
		lineno += 1;

		/* assign other continual lines */
		if (has_multilines)
		{
			int		lnb_row;
			bool	continual = false;

			lnb = sortbuf[i].lnb;
			lnb_row = sortbuf[i].lnb_row;

			continual = lnb->lineinfo &&
									   (lnb->lineinfo[lnb_row].mask & LINEINFO_CONTINUATION);

			while (lnb && continual)
			{
				lnb_row += 1;
				if (lnb_row >= lnb->nrows)
				{
					lnb_row = 0;
					lnb = lnb->next;
				}

				desc->order_map[lineno].lnb = lnb;
				desc->order_map[lineno].lnb_row = lnb_row;
				lineno += 1;

				continual = lnb && lnb->lineinfo &&
								(lnb->lineinfo[lnb_row].mask & LINEINFO_CONTINUATION);
			}
		}
	}

	/*
	 * We cannot to say nothing about found_row, so most
	 * correct solution is clean it now.
	 */
	scrdesc->found_row = -1;

	for (i = 0; i < sortbuf_pos; i++)
		free(sortbuf[i].strxfrm);

	free(sortbuf);
}

/*
 * Prepare dimensions of windows layout
 */
static void
create_layout_dimensions(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
				   int fixCols, int fixRows,
				   int maxy, int maxx)
{
	scrdesc->maxy = maxy;
	scrdesc->maxx = maxx;

	if (opts->show_rownum)
	{
		int startx = number_width(desc->maxy) + 2;

		scrdesc->main_start_x = startx;
		scrdesc->main_maxx -= startx;
	}

	scrdesc->fix_cols_cols = 0;

	/* search end of fixCol'th column */
	if (desc->headline_transl != NULL && fixCols > 0)
	{
		char   *c = desc->headline_transl;

		while (*c != 0)
		{
			if (*c == 'I' && --fixCols == 0)
			{
				scrdesc->fix_cols_cols = c - desc->headline_transl + 1;
				break;
			}
			c += 1;
		}
	}

	scrdesc->fix_rows_rows = 0;
	scrdesc->footer_rows = 0;

	if (fixRows != -1)
	{
		scrdesc->fix_rows_rows = fixRows;
	}
	else if (!desc->is_expanded_mode && desc->border_head_row != -1 &&
			  desc->headline_transl != NULL)
	{
		scrdesc->fix_rows_rows = desc->border_head_row + 1 - desc->title_rows;
	}

	/* disable fixed parts when is not possible draw in screen */
	if (scrdesc->fix_cols_cols > scrdesc->main_maxx)
		scrdesc->fix_cols_cols = 0;

	if (scrdesc->fix_rows_rows > scrdesc->main_maxy)
		scrdesc->fix_rows_rows = 0;

	if (scrdesc->fix_rows_rows == 0 && !desc->is_expanded_mode)
	{
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	desc->fixed_rows = scrdesc->fix_rows_rows;
}

static void
create_layout(Options *opts, ScrDesc *scrdesc, DataDesc *desc, int first_data_row, int first_row)
{
	if (w_luc(scrdesc) != NULL)
	{
		delwin(w_luc(scrdesc));
		w_luc(scrdesc) = NULL;
	}
	if (w_fix_rows(scrdesc) != NULL)
	{
		delwin(w_fix_rows(scrdesc));
		w_fix_rows(scrdesc) = NULL;
	}
	if (w_fix_cols(scrdesc) != NULL)
	{
		delwin(w_fix_cols(scrdesc));
		w_fix_cols(scrdesc) = NULL;
	}
	if (w_rows(scrdesc) != NULL)
	{
		delwin(w_rows(scrdesc));
		w_rows(scrdesc) = NULL;
	}
	if (w_footer(scrdesc) != NULL)
	{
		delwin(w_footer(scrdesc));
		w_footer(scrdesc) = NULL;
	}
	if (w_rownum(scrdesc) != NULL)
	{
		delwin(w_rownum(scrdesc));
		w_rownum(scrdesc) = NULL;
	}
	if (w_rownum_luc(scrdesc) != NULL)
	{
		delwin(w_rownum_luc(scrdesc));
		w_rownum_luc(scrdesc) = NULL;
	}

	if (desc->headline_transl != NULL && desc->footer_row > 0)
	{
		int		rows_rows = desc->footer_row - first_row - first_data_row;
		int		data_rows;

		/* desc->footer_row == desc->first_data_row when result is empty */
		if (rows_rows > 0 || desc->footer_row == desc->first_data_row)
		{
			data_rows = scrdesc->main_maxy - desc->fixed_rows;
			scrdesc->rows_rows = min_int(rows_rows, data_rows);
		}
		else
		{
			int		new_fix_rows_rows = scrdesc->fix_rows_rows + rows_rows - 1;

			scrdesc->fix_rows_rows = new_fix_rows_rows > 0 ? new_fix_rows_rows : 0;
			scrdesc->rows_rows = new_fix_rows_rows >= 0 ? 1 : 0;
			data_rows = scrdesc->main_maxy - scrdesc->fix_rows_rows;
		}

		scrdesc->footer_rows = min_int(data_rows - scrdesc->rows_rows,
									   desc->last_row - desc->footer_row + 1);

		if (scrdesc->footer_rows > 0)
		{
			w_footer(scrdesc) = subwin(stdscr,
										scrdesc->footer_rows,
										scrdesc->main_maxx,
										scrdesc->main_start_y + scrdesc->fix_rows_rows + scrdesc->rows_rows,
										scrdesc->main_start_x);
		}
	}
	else if (desc->headline_transl != NULL)
	{
		scrdesc->rows_rows = min_int(scrdesc->main_maxy - scrdesc->fix_rows_rows,
									 desc->last_row - desc->first_data_row + 1);
	}
	else
	{
		scrdesc->rows_rows = 0;
		scrdesc->fix_rows_rows = 0;
		scrdesc->footer_rows = min_int(scrdesc->main_maxy, desc->last_row + 1);
		w_footer(scrdesc) = subwin(stdscr,
									scrdesc->footer_rows,
									scrdesc->main_maxx,
									scrdesc->main_start_y,
									scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		w_fix_rows(scrdesc) = subwin(stdscr,
									  scrdesc->fix_rows_rows,
									  scrdesc->main_maxx - scrdesc->fix_cols_cols,
									  scrdesc->main_start_y,
									  scrdesc->fix_cols_cols + scrdesc->main_start_x);
	}

	if (scrdesc->fix_cols_cols > 0 && scrdesc->rows_rows > 0)
	{
		w_fix_cols(scrdesc) = subwin(stdscr,
									  scrdesc->rows_rows,
									  scrdesc->fix_cols_cols,
									  scrdesc->fix_rows_rows + scrdesc->main_start_y,
									  scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		w_luc(scrdesc) = subwin(stdscr,
								scrdesc->fix_rows_rows,
								scrdesc->fix_cols_cols,
								scrdesc->main_start_y,
								scrdesc->main_start_x);
	}

	if (scrdesc->rows_rows > 0)
	{
		w_rows(scrdesc) = subwin(stdscr, scrdesc->rows_rows,
								   scrdesc->main_maxx - scrdesc->fix_cols_cols,
								   scrdesc->fix_rows_rows + scrdesc->main_start_y,
								   scrdesc->fix_cols_cols + scrdesc->main_start_x);
	}

	if (scrdesc->fix_rows_rows > 0 && opts->show_rownum)
	{
		Theme   *theme = &scrdesc->themes[WINDOW_ROWNUM_LUC];

		w_rownum_luc(scrdesc) = subwin(stdscr,
								   scrdesc->fix_rows_rows,
								   scrdesc->main_start_x,
								   scrdesc->main_start_y,
								   0);

		wbkgd(w_rownum_luc(scrdesc), theme->data_attr);
	}
	if (scrdesc->rows_rows + scrdesc->footer_rows > 0 && opts->show_rownum)
	{
		w_rownum(scrdesc) = subwin(stdscr,
								   scrdesc->rows_rows + scrdesc->footer_rows,
								   scrdesc->main_start_x,
								   scrdesc->fix_rows_rows + scrdesc->main_start_y,
								   0);
	}
}

/*
 * Refresh aux windows like top bar or bottom bar.
 */
static void
refresh_aux_windows(Options *opts, ScrDesc *scrdesc)
{
	int		maxy, maxx;
	WINDOW	   *top_bar = w_top_bar(scrdesc);
	WINDOW	   *bottom_bar = w_bottom_bar(scrdesc);

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	if (top_bar != NULL)
	{
		delwin(top_bar);
		top_bar = NULL;
		w_top_bar(scrdesc) = NULL;
	}

	if (opts->no_topbar)
		scrdesc->top_bar_rows = 0;
	else
	{
		scrdesc->top_bar_rows = 1;
		top_bar = subwin(stdscr, 1, 0, 0, 0);
		wbkgd(top_bar, COLOR_PAIR(2));
		wnoutrefresh(top_bar);
		w_top_bar(scrdesc) = top_bar;
	}

	if (bottom_bar != NULL)
	{
		delwin(bottom_bar);
		bottom_bar = NULL;
		w_bottom_bar(scrdesc) = NULL;
	}

	bottom_bar = subwin(stdscr, 1, 0, maxy - 1, 0);
	w_bottom_bar(scrdesc) = bottom_bar;
	werase(bottom_bar);

	/* data colours are better than default */
	wbkgd(bottom_bar, COLOR_PAIR(3));
	wnoutrefresh(bottom_bar);

	scrdesc->main_maxy = maxy;

	scrdesc->main_maxx = maxx;
	scrdesc->main_start_y = 0;
	scrdesc->main_start_x = 0;

	if (top_bar != NULL)
	{
		scrdesc->main_maxy -= 1;
		scrdesc->main_start_y = 1;
	}

	if (bottom_bar != NULL)
	{
		if (!opts->no_commandbar)
			scrdesc->main_maxy -= 1;
	}
}

/*
 * Returns width of number
 */
static int
number_width(int num)
{
	if (num < 10)
		return 1;
	if (num < 100)
		return 2;
	if (num < 1000)
		return 3;
	if (num < 10000)
		return 4;
	if (num < 100000)
		return 5;
	if (num < 1000000)
		return 6;
	if (num < 10000000)
		return 7;

	return 8;
}

/*
 * returns true when cursor is on footer window
 */
static bool
is_footer_cursor(int cursor_row, ScrDesc *scrdesc, DataDesc *desc)
{
	if (w_footer(scrdesc) == NULL)
		return false;
	else if (scrdesc->rows_rows == 0)
		return true;

	return cursor_row + scrdesc->fix_rows_rows + desc->title_rows + 1 > desc->footer_row;
}

static void
print_status(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
						 int cursor_row, int cursor_col, int first_row, int fix_rows_offset,
						 int vertical_cursor_column)
{
	int		maxy, maxx;
	int		smaxy, smaxx;
	char	buffer[200];
	WINDOW   *top_bar = w_top_bar(scrdesc);
	WINDOW   *bottom_bar = w_bottom_bar(scrdesc);
	Theme	*top_bar_theme = &scrdesc->themes[WINDOW_TOP_BAR];
	Theme	*bottom_bar_theme = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	/* do nothing when there are not top status bar */
	if (scrdesc->top_bar_rows > 0)
	{
		getmaxyx(top_bar, maxy, maxx);
		getmaxyx(stdscr, smaxy, smaxx);

		(void) maxy;

		wbkgd(top_bar, err ? bottom_bar_theme->error_attr : COLOR_PAIR(2));
		werase(top_bar);

		if (desc->title[0] != '\0' || desc->filename[0] != '\0')
		{
			wattron(top_bar, top_bar_theme->title_attr);
			if (desc->title[0] != '\0' && desc->title_rows > 0)
				mvwprintw(top_bar, 0, 0, "%s", desc->title);
			else if (desc->filename[0] != '\0')
				mvwprintw(top_bar, 0, 0, "%s", desc->filename);
			wattroff(top_bar, top_bar_theme->title_attr);
		}

		if (opts->watch_time > 0)
		{
			if (last_watch_sec > 0)
			{
				long	ms, td;
				time_t	sec;
				struct timespec spec;
				int		w = number_width(opts->watch_time);
				int		x = 0;

				clock_gettime(CLOCK_MONOTONIC, &spec);
				ms = roundl(spec.tv_nsec / 1.0e6);
				sec = spec.tv_sec;

				td = (sec - last_watch_sec) * 1000 + ms - last_watch_ms;

				if (desc->title[0] != '\0' || desc->filename[0] != '\0')
					x = maxx / 4;

				if (paused)
					mvwprintw(top_bar, 0, x, "paused %ld sec", td / 1000);
				else
					mvwprintw(top_bar, 0, x, "%*ld/%d", w, td/1000 + 1, opts->watch_time);
			}

			if (err)
			{
				int		i;
				char   *ptr = buffer;

				/* copy first row to buffer */
				for (i = 0; i < 200; i++)
					if (err[i] == '\0' || err[i] == '\n')
					{
						*ptr = '\0';
						break;
					}
					else
						*ptr++ = err[i];

				wprintw(top_bar, "   %s", buffer);
			}
		}

		if (opts->no_cursor)
		{
			double	percent;

			if (desc->headline_transl)
			{
				percent = (first_row + scrdesc->main_maxy - 1 - desc->fixed_rows - desc->title_rows) /
								((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0;
				percent = percent > 100.0 ? 100.0 : percent;

				if (opts->vertical_cursor && desc->columns > 0 && vertical_cursor_column > 0)
				{
					int		vminx = desc->cranges[vertical_cursor_column - 1].xmin;
					int		vmaxx = desc->cranges[vertical_cursor_column - 1].xmax;

					snprintf(buffer, 199, "V:[%*d/%*d %*d..%*d] [FC:%*d C:%*d..%*d/%*d  L:%*d/%*d %3.0f%%",
									number_width(desc->columns), vertical_cursor_column,
									number_width(desc->columns), desc->columns,
									number_width(desc->headline_char_size), vminx + 1,
									number_width(desc->headline_char_size), vmaxx + 1,
									number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
									number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
									number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
									number_width(desc->headline_char_size), desc->headline_char_size,
									number_width(desc->maxy - desc->fixed_rows), first_row + scrdesc->main_maxy - fix_rows_offset - desc->fixed_rows - desc->title_rows,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
									percent);
				}
				else
				{
					snprintf(buffer, 199, "FC:%*d C:%*d..%*d/%*d  L:%*d/%*d %3.0f%%",
									number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
									number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
									number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
									number_width(desc->headline_char_size), desc->headline_char_size,
									number_width(desc->maxy - desc->fixed_rows), first_row + scrdesc->main_maxy - fix_rows_offset - desc->fixed_rows - desc->title_rows,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
									percent);
				}
			}
			else
			{
				percent = ((first_row + scrdesc->main_maxy) / ((double) (desc->last_row + 1))) * 100.0;
				percent = percent > 100.0 ? 100.0 : percent;

				snprintf(buffer, 199, "C:%*d..%*d/%*d  L:%*d/%*d %3.0f%%",
								number_width(desc->maxx), cursor_col + scrdesc->fix_cols_cols + 1,
								number_width(desc->maxx), min_int(smaxx + cursor_col, desc->maxx),
								number_width(desc->maxx), desc->maxx,
								number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + scrdesc->main_maxy,
								number_width(desc->last_row), desc->last_row + 1,
								percent);
			}
		}
		else
		{
			if (desc->headline_transl)
			{
				if (opts->vertical_cursor  && desc->columns > 0 && vertical_cursor_column > 0)
				{
					int		vminx = desc->cranges[vertical_cursor_column - 1].xmin;
					int		vmaxx = desc->cranges[vertical_cursor_column - 1].xmax;

					snprintf(buffer, 199, "V:[%*d/%*d %*d..%*d] FC:%*d C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
									number_width(desc->columns), vertical_cursor_column,
									number_width(desc->columns), desc->columns,
									number_width(desc->headline_char_size), vminx + 1,
									number_width(desc->headline_char_size), vmaxx + 1,
									number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
									number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
									number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
									number_width(desc->headline_char_size), desc->headline_char_size,
									number_width(desc->maxy - desc->fixed_rows), first_row + 1 - fix_rows_offset,
									number_width(smaxy), cursor_row - first_row + fix_rows_offset,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), cursor_row + 1,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
									(cursor_row + 1) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
				}
				else
				{
					snprintf(buffer, 199, "FC:%*d C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
									number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
									number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
									number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
									number_width(desc->headline_char_size), desc->headline_char_size,
									number_width(desc->maxy - desc->fixed_rows), first_row + 1 - fix_rows_offset,
									number_width(smaxy), cursor_row - first_row + fix_rows_offset,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), cursor_row + 1,
									number_width(desc->maxy - desc->fixed_rows - desc->title_rows), desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
									(cursor_row + 1) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
				}
			}
			else
			{
				snprintf(buffer, 199, "C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
								number_width(desc->maxx), cursor_col + scrdesc->fix_cols_cols + 1,
								number_width(desc->maxx), min_int(smaxx + cursor_col, desc->maxx),
								number_width(desc->maxx), desc->maxx,
								number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + 1,
								number_width(smaxy), cursor_row - first_row,
								number_width(desc->last_row), cursor_row + 1,
								number_width(desc->last_row), desc->last_row + 1,
								((cursor_row + 1) / ((double) (desc->last_row + 1))) * 100.0);
			}
		}

		mvwprintw(top_bar, 0, maxx - strlen(buffer) - 2, "  %s", buffer);
		wnoutrefresh(top_bar);
	}

	if (opts->less_status_bar)
	{
		/* less-status-bar */
		char	title[65];
		char	*str;
		size_t	bytes = sizeof(title) - 2;
		char	*ptr = title;

		if (desc->title_rows > 0 && desc->title[0] != '\0')
			str = desc->title;
		else if (desc->filename[0] != '\0')
			str = desc->filename;
		else
			str = "";

		while (bytes > 0 && *str != '\0')
		{
			size_t		sz = opts->force8bit ? 1 : utf8charlen(*str);

			if (sz > bytes)
				break;

			memcpy(ptr, str, sz);
			ptr += sz;
			str += sz;
			bytes -= sz;
		}

		if (ptr != title)
			*ptr++ = ' ';
		*ptr = '\0';

		wattron(bottom_bar, bottom_bar_theme->prompt_attr);

		if (desc->headline_transl)
		{
			snprintf(buffer, 199, "%slines %d-%d/%d %.0f%% ",
								title,
								first_row + 1 - fix_rows_offset,
								first_row + 1 - fix_rows_offset + scrdesc->rows_rows,
								desc->maxy + 1 - desc->fixed_rows - desc->title_rows,
								(cursor_row + 1) / ((double) (desc->maxy + 1 - desc->fixed_rows - desc->title_rows)) * 100.0);
		}
		else
		{
			snprintf(buffer, 199, "%slines %d-%d/%d %.0f%% ",
								title,
								first_row + 1,
								first_row + 1 + scrdesc->footer_rows,
								desc->last_row + 1,
								((cursor_row + 1) / ((double) (desc->last_row + 1))) * 100.0);
		}

		mvwprintw(bottom_bar, 0, 0, "%s", buffer);
		wclrtoeol(bottom_bar);
		wnoutrefresh(bottom_bar);

		wattroff(bottom_bar, bottom_bar_theme->prompt_attr);
	}
}

static void
make_beep(Options *opts)
{
	if (!opts->no_sound)
		beep();
}

/*
 * It is used for result of action info
 */
static int
show_info_wait(Options *opts, ScrDesc *scrdesc, char *fmt, char *par, bool beep, bool refresh_first, bool applytimeout, bool is_error)
{
	WINDOW	*bottom_bar = w_bottom_bar(scrdesc);
	Theme	*t = &scrdesc->themes[WINDOW_BOTTOM_BAR];
	attr_t  att;
	int		c;
	int		timeout = -1;

	/*
	 * When refresh is required first, then store params and quit immediately.
	 * Only once can be info moved after refresh
	 */
	if (refresh_first && scrdesc->fmt == NULL)
	{
		if (fmt != NULL)
		{
			scrdesc->fmt = strdup(fmt);
			if (!scrdesc->fmt)
				leave_ncurses("out of memory");
		}
		else
			scrdesc->fmt = NULL;

		if (par != NULL)
		{
			scrdesc->par = strdup(par);
			if (!scrdesc->par)
				leave_ncurses("out of memory");
		}
		else
			scrdesc->par = NULL;
		scrdesc->beep = beep;
		scrdesc->applytimeout = applytimeout;
		scrdesc->is_error = is_error;

		return 0;
	}

	att = !is_error ? t->bottom_light_attr : t->error_attr;

	wattron(bottom_bar, att);

	if (par != NULL)
		mvwprintw(bottom_bar, 0, 0, fmt, par);
	else
		mvwprintw(bottom_bar, 0, 0, "%s", fmt);

	if (logfile)
	{
		char buffer[1024];

		print_log_prefix(logfile);

		if (par)
			snprintf(buffer, 1024, fmt, par);
		else
			snprintf(buffer, 1024, "%s", fmt);

		fprintf(logfile, "info: %s\n", buffer);
	}

	wclrtoeol(bottom_bar);
	mvwchgat(bottom_bar, 0, 0, -1, att, PAIR_NUMBER(att), 0);

	wattroff(bottom_bar,  att);
	wnoutrefresh(bottom_bar);

	refresh();

	if (beep)
		make_beep(opts);

	if (applytimeout)
		timeout = strlen(fmt) < 50 ? 2000 : 6000;

	c = get_event(&event, &press_alt, &got_sigint, NULL, NULL, NULL, timeout);

	/*
	 * Screen should be refreshed after show any info.
	 */
	scrdesc->refresh_scr = true;

	/* eat escape if pressed here */
	if (c == 27 && press_alt)
	{
		press_alt = false;
		return 0;
	}
	else
		return c == ERR ? 0 : c;
}

#ifdef HAVE_LIBREADLINE

#if RL_READLINE_VERSION >= 0x0603

static int
readline_input_avail(void)
{
    return input_avail;
}

#endif

static int
readline_getc(FILE *dummy)
{
	(void) dummy;

    input_avail = false;
    return input;
}

static void
forward_to_readline(char c)
{
    input = c;
    input_avail = true;
    rl_callback_read_char();
}

static void
got_string(char *line)
{
	if (line)
		strcpy(readline_buffer, line);
	else
		readline_buffer[0] = '\0';

	got_readline_string = true;
}

static void
readline_redisplay()
{
	size_t cursor_col;

	if (!force8bit)
	{
		size_t prompt_dsplen = utf_string_dsplen(rl_display_prompt, SIZE_MAX);

		cursor_col = prompt_dsplen
					  + readline_utf_string_dsplen(rl_line_buffer, rl_point, prompt_dsplen);
	}
	else
	{
		cursor_col = strlen(rl_display_prompt) + min_int(strlen(rl_line_buffer), rl_point);
	}

	wbkgd(g_bottom_bar, input_attr);
	werase(g_bottom_bar);
	mvwprintw(g_bottom_bar, 0, 0, "%s%s", rl_display_prompt, rl_line_buffer);
	mvwchgat(g_bottom_bar, 0, 0, -1, input_attr, PAIR_NUMBER(input_attr), 0);

	if (cursor_col >= (size_t) COLS)
		curs_set(0);
	else
	{
		wmove(g_bottom_bar, 0, cursor_col);
		curs_set(2);
	}

	wrefresh(g_bottom_bar);
}


#endif

static void
get_string(Options *opts, ScrDesc *scrdesc, char *prompt, char *buffer, int maxsize, char *defstr)
{
	WINDOW	*bottom_bar = w_bottom_bar(scrdesc);
	Theme	*t = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "input string prompt- \"%s\"\n", prompt);
	}

#ifdef HAVE_LIBREADLINE

	int		c;
	int		prev_c = 0;
	mmask_t		prev_mousemask = 0;
	bool	input_is_valid = true;

	g_bottom_bar = bottom_bar;
	got_readline_string = false;
	force8bit = opts->force8bit;
	input_attr = t->input_attr;

	wattron(bottom_bar, t->input_attr);
	mvwprintw(bottom_bar, 0, 0, "");
	wclrtoeol(bottom_bar);

	curs_set(1);
	echo();

	rl_getc_function = readline_getc;

#if RL_READLINE_VERSION >= 0x0603

	rl_input_available_hook = readline_input_avail;

#endif

	rl_redisplay_function = readline_redisplay;

	rl_callback_handler_install(prompt, got_string);

	mousemask(0, &prev_mousemask);

	/* use default value from buffer */
	if (defstr && *defstr)
	{
		rl_insert_text(defstr);
		rl_forced_update_display();
		wrefresh(bottom_bar);
	}

	wtimeout(bottom_bar, 100);

	while (!got_readline_string)
	{
		do
		{
			c = wgetch(bottom_bar);
			if (c == ERR && errno == EINTR)
				goto finish_read;

			if (handle_sigint)
				goto finish_read;
		}
		while (c == ERR || c == 0);

		/* detect double alts .. escape */
		if (c == 27 && prev_c == 27)
		{
			/*
			 * Cannot leave here - readline requires complete ALT pair.
			 * So just update flag here.
			 */
			input_is_valid = false;
		}

		prev_c = c;

		forward_to_readline(c);
		wrefresh(bottom_bar);

		if (!input_is_valid)
			break;
	}

finish_read:

	if (handle_sigint)
	{
		handle_sigint = false;
		input_is_valid = false;
	}

	mousemask(prev_mousemask, NULL);

	rl_callback_handler_remove();

	curs_set(0);
	noecho();

	/* don't allow alt chars (garbage) in input string */
	if (input_is_valid)
	{
		char   *ptr = readline_buffer;

		while (*ptr)
			if (*ptr++ == 27)
			{
				input_is_valid = false;
				break;
			}
	}

	if (input_is_valid)
	{
		strncpy(buffer, readline_buffer, maxsize - 1);
		buffer[maxsize] = '\0';

#ifdef HAVE_READLINE_HISTORY

		if (*buffer)
		{
			/*
			 * Don't write same strings to hist file
			 */
			if (*last_history == '\0' || strncmp(last_history, buffer, sizeof(last_history)) != 0)
			{
				add_history(buffer);
				strncpy(last_history, buffer, sizeof(last_history) - 1);
				last_history[sizeof(last_history) - 1] = '\0';
			}
		}

#endif

	}
	else
	{
		if (defstr)
			*defstr = '\0';
		buffer[0] = '\0';
	}

#else

	wbkgd(bottom_bar, t->input_attr);
	werase(bottom_bar);
	mvwprintw(bottom_bar, 0, 0, "%s", prompt);
	curs_set(1);
	echo();
	wgetnstr(bottom_bar, buffer, maxsize);

	/* reset ctrlc, wgetnstr doesn't handle this signal now */
	handle_sigint = false;

	curs_set(0);
	noecho();

#endif

	/*
	 * Screen should be refreshed after show any info.
	 */
	scrdesc->refresh_scr = true;

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "input string - \"%s\"\n", buffer);
	}
}

#define SEARCH_FORWARD			1
#define SEARCH_BACKWARD			2

static bool
has_upperchr(Options *opts, char *str)
{
	if (opts->force8bit)
	{
		while (*str != '\0')
		{
			if (isupper(*str))
				return true;

			str += 1;
		}
	}
	else
	{
		while (*str != '\0')
		{
			if (utf8_isupper(str))
				return true;

			str += utf8charlen(*str);
		}
	}

	return false;
}

static void
reset_searching_lineinfo(LineBuffer *lnb)
{
	while (lnb != NULL)
	{
		if (lnb->lineinfo != NULL)
		{
			int		i;

			for (i = 0; i < lnb->nrows; i++)
			{
				lnb->lineinfo[i].mask |= LINEINFO_UNKNOWN;
				lnb->lineinfo[i].mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);
			}
		}
		lnb = lnb->next;
	}
}

/*
 * Replace tilde by HOME dir
 */
static char *
tilde(char *path)
{
	static char	writebuf[MAXPATHLEN];
	int			chars = 0;
	char *w;

	w = writebuf;

	while (*path && chars < MAXPATHLEN - 1)
	{
		if (*path == '~')
		{
			char *home = getenv("HOME");

			if (home == NULL)
				leave_ncurses("HOME directory is not defined");

			while (*home && chars < MAXPATHLEN - 1)
			{
				*w++ = *home++;
				chars += 1;
			}
			path++;
		}
		else
		{
			*w++ = *path++;
			chars += 1;
		}
	}

	*w = '\0';

	return writebuf;
}

/*
 * Set cursor_col to ensure visibility of vertical column
 */
static int
get_cursor_col_for_vertical_column(int vertical_cursor_column,
								   int cursor_col,
								   DataDesc *desc,
								   ScrDesc *scrdesc)
{
	int		xmin = desc->cranges[vertical_cursor_column - 1].xmin;
	int		xmax = desc->cranges[vertical_cursor_column - 1].xmax;

	/* Do nothing if vertical cursor is visible already */
	if (xmax < scrdesc->fix_cols_cols)
		return 0;
	else if (xmin > scrdesc->fix_cols_cols && xmax < scrdesc->main_maxx + cursor_col)
		return cursor_col;
	else
	{
		int		max_cursor_col = desc->headline_char_size - scrdesc->main_maxx;
		int		column_center = (xmin + xmax) / 2;
		int		cursor_fixed;

		cursor_col = column_center - ((scrdesc->main_maxx - scrdesc->fix_cols_cols) / 2 + scrdesc->fix_cols_cols);
		cursor_col = cursor_col < max_cursor_col ? cursor_col : max_cursor_col;

		cursor_col = cursor_col > 0 ? cursor_col : 0;

		/* try to show starts chars when it is possible */
		if (xmin < scrdesc->fix_cols_cols + cursor_col)
		{
			cursor_fixed = xmin - scrdesc->fix_cols_cols + 1;
			if (column_center < scrdesc->main_maxx + cursor_fixed)
				cursor_col = cursor_fixed;
		}

		return cursor_col;
	}
}

/*
 * Calculate focus point from left border of selected columns.
 */
static int
get_x_focus(int vertical_cursor_column,
			int cursor_col,
			DataDesc *desc,
			ScrDesc *scrdesc)
{
	int xmin = desc->cranges[vertical_cursor_column - 1].xmin;

	return xmin > scrdesc->fix_cols_cols ? xmin - cursor_col : xmin;
}

/*
 * Reads keycode. When keycode is Esc - then read next keycode, and sets flag alt.
 * When keycode is related to mouse, then get mouse event details.
 */
static int
get_event(MEVENT *mevent,
		  bool *alt,
		  bool *sigint,
		  bool *timeout,
		  bool *file_event,
		  bool *reopen_file,
		  int timeoutval)
{
	bool	first_event = true;
	int		c;
	int		loops = -1;
	int		retry_count = 0;

#ifdef DEBUG_PIPE

	char	buffer[20];

	if (0)
	{
		struct mallinfo mi;

		mi = mallinfo();

		fprintf(debug_pipe, "Total non-mmapped bytes (arena):       %d\n", mi.arena);
		fprintf(debug_pipe, "# of free chunks (ordblks):            %d\n", mi.ordblks);
		fprintf(debug_pipe, "# of free fastbin blocks (smblks):     %d\n", mi.smblks);
		fprintf(debug_pipe, "# of mapped regions (hblks):           %d\n", mi.hblks);
		fprintf(debug_pipe, "Bytes in mapped regions (hblkhd):      %d\n", mi.hblkhd);
		fprintf(debug_pipe, "Max. total allocated space (usmblks):  %d\n", mi.usmblks);
		fprintf(debug_pipe, "Free bytes held in fastbins (fsmblks): %d\n", mi.fsmblks);
		fprintf(debug_pipe, "Total allocated space (uordblks):      %d\n", mi.uordblks);
		fprintf(debug_pipe, "Total free space (fordblks):           %d\n", mi.fordblks);
		fprintf(debug_pipe, "Topmost releasable block (keepcost):   %d\n", mi.keepcost);
	}

#endif

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	wint_t	ch;
	int		ret;

#endif

retry:

	*alt = false;
	*sigint = false;

	if (timeout)
		*timeout = false;

	/*
	 * Read event when file or pipe event is expected, and
	 * when second polled fd is ready. First is a STDIN_FILENO.
	 */
	if (file_event && fds[1].fd != -1)
	{
		int		poll_num;

		*file_event = false;

		if (reopen_file)
			*reopen_file = false;

		poll_num = poll(fds, 2, timeoutval);
		if (poll_num == -1)
		{
			if (logfile)
			{
				print_log_prefix(logfile);
				fprintf(logfile, "poll error %s\n", strerror(errno));
			}
		}
		else if (poll_num > 0)
		{
			/* process inotify event, but only when we can process it */
			if (fds[1].revents & POLLIN && file_event)
			{
				char		buff[64];
				ssize_t		len;

				*file_event = true;

				if (inotify_fd == -1)
					return 0;

				/* there are a events on monitored file */
				len = read(inotify_fd, buff, sizeof(buff));

				/*
				 * read to end, it is notblocking IO, only one event and
				 * one file is monitored
				 */
				while (len > 0)
				{
#ifdef HAVE_INOTIFY

					const struct inotify_event *event = (struct inotify_event *) buff;

					while (len > 0 && 0)
					{
						if ((event->mask & IN_CLOSE_WRITE))
						{
							if (reopen_file)
								*reopen_file = true;
						}

						len -= sizeof (struct inotify_event) + event->len;
						event += sizeof (struct inotify_event) + event->len;
					}

#endif

					len = read(inotify_fd, buff, sizeof(buff));
				}

				/*
				 * wait 100ms - sometimes inotify is too fast, and file content
				 * is buffered, and readfile reads only first line
				 */
				usleep(1000 * 100);

				return 0;
			}
		}
		else
		{
			/* timeout */
			if (timeout)
				*timeout = true;

			return 0;
		}
	}

repeat:

	if (timeoutval != -1)
		loops = timeoutval / 1000;

	for (;;)
	{
		errno = 0;

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

		ret = get_wch(&ch);
		(void) ret;

		c = ch;

#else

		c = getch();

#endif

		if ((c == ERR && errno == EINTR) || handle_sigint)
		{
			*sigint = true;
			handle_sigint = false;
			return 0;
		}

		/*
		 * Leave this cycle if there is some unexpected error.
		 * Outer cycle is limited by 10 iteration.
		 */
		if (err != 0)
			break;

		/*
		 * On ncurses6 (Linux) get_wch returns zero on delay. But by man pages it
		 * should to return ERR (CentOS 7 does it). So repead reading for both cases.
		 */
		if (c != 0 && c != ERR)
			break;

		if (loops >= 0)
		{
			if (--loops == 0)
			{
				if (timeout)
					*timeout = true;

				return 0;
			}
		}
	}

	if (c == KEY_MOUSE)
	{
		int	ok = getmouse(mevent);

		if (ok != OK)
		{

#ifdef DEBUG_PIPE

			/*
			 * This is almost all problematic error, that can disable mouse
			 * functionality. It is based on ncurses resets (endwin) or unwanted
			 * mouse reconfigurations (mousemask).
			 */
			fprintf(debug_pipe, "Attention: error reading mouse event\n");

#endif

			goto repeat;
		}
	}

	if (c == 27) /* Escape (before ALT chars) */
	{
		if (first_event)
		{
			first_event = false;
			goto repeat;
		}
	}

	*alt = !first_event;

#ifdef DEBUG_PIPE

	debug_eventno += 1;
	if (c == KEY_MOUSE)
	{
		sprintf(buffer, ", bstate: %08lx", (unsigned long) mevent->bstate);
	}
	else
		buffer[0] = '\0';

	fprintf(debug_pipe, "*** eventno: %d, key: %s%s%s ***\n",
			  debug_eventno,
			  *alt ? "Alt " : "",
			  keyname(c),
			  buffer);
	fflush(debug_pipe);

#endif

	if (c == ERR)
	{
#ifdef DEBUG_PIPE

		fprintf(debug_pipe, "ERR input - retry no: %d\n", retry_count);

#endif

		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "ERR input - retry no: %d\n", retry_count);
		}

		if (++retry_count < 10)
			goto retry;

#ifdef DEBUG_PIPE

		fprintf(debug_pipe, "ERR input - touch retry limit, stop\n");

#endif

		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "ERR input - touch retry limit, stop\n");
		}
	}

	return c;
}

#define VISIBLE_DATA_ROWS		(scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset)
#define MAX_FIRST_ROW			(desc.last_row - desc.title_rows - scrdesc.main_maxy + 1)
#define MAX_CURSOR_ROW			(desc.last_row - desc.first_data_row)
#define CURSOR_ROW_OFFSET		(scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset)

static void
exit_ncurses(void)
{
	if (active_ncurses)
		endwin();
}

static void
DataDescFree(DataDesc *desc)
{
	LineBuffer	   *lb = &desc->rows;

	while (lb)
	{
		LineBuffer   *next;
		int		i;

		for (i = 0; i < lb->nrows; i++)
			free(lb->rows[i]);

		free(lb->lineinfo);
		next = lb->next;
		if (lb != &desc->rows)
			free(lb);

		lb = next;
	}

	free(desc->order_map);
	free(desc->headline_transl);
	free(desc->cranges);
}

static void
print_log_prefix(FILE *logfile)
{
	time_t		rawtime;
	struct tm  *timeinfo;
	const char *asct;
	int		len;

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	asct = asctime(timeinfo);
	len = strlen(asct);

	fprintf(logfile, "%.*s ", len - 1, asct);
	fprintf(logfile, "[%ld] ", (long) getpid());
}

/*
 * Copy persistent data (search related and info box related)
 * to new instance.
 */
static void
MergeScrDesc(ScrDesc *new, ScrDesc *old)
{
	memcpy(new->searchterm, old->searchterm, 255);
	new->searchterm_char_size = old->searchterm_char_size;
	new->searchterm_size = old->searchterm_size;

	memcpy(new->searchcolterm, old->searchcolterm, 255);
	new->searchcolterm_size = old->searchcolterm_size;

	new->has_upperchr = old->has_upperchr;
	new->found = old->found;
	new->found_start_x = old->found_start_x;
	new->found_start_bytes = old->found_start_bytes;
	new->found_row = old->found_row;

	new->fmt = old->fmt;
	new->par = old->par;
}

/*
 * Ensure so first_row is in correct range
 */
static int
adjust_first_row(int first_row, DataDesc *desc, ScrDesc *scrdesc)
{
	int		max_first_row;

	max_first_row = desc->last_row - desc->title_rows - scrdesc->main_maxy + 1;
	max_first_row = max_first_row < 0 ? 0 : max_first_row;

	return first_row > max_first_row ? max_first_row : first_row;
}

int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		event_keycode = 0;
	int		prev_event_keycode = 0;
	int		next_event_keycode = 0;
	int		command = cmd_Invalid;
	int		translated_command = cmd_Invalid;
	int		translated_command_history = cmd_Invalid;
	long	last_ms = 0;							/* time of last mouse release in ms */
	time_t	last_sec = 0;							/* time of last mouse release in sec */
	long	next_watch = 0;
	int		next_command = cmd_Invalid;
	bool	reuse_event = false;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		footer_cursor_col = 0;
	int		vertical_cursor_column = -1;			/* table columns are counted from one */
	int		last_x_focus = -1;						/* it is used for repeated vertical cursor display */
	int		first_row = 0;
	int		prev_first_row;
	int		first_data_row;
	int		default_freezed_cols = 1;
	int		i;
	int		reserved_rows = -1;					/* dbcli has significant number of self reserved lines */
	DataDesc		desc;
	ScrDesc			scrdesc;
	Options			opts;
	int		fixedRows = -1;			/* detect automatically (not yet implemented option) */
	FILE   *fp = NULL;
	bool	detected_format = false;
	bool	no_alternate_screen = false;
	int		fix_rows_offset = 0;

	int		opt;
	int		option_index = 0;
	mmask_t		prev_mousemask = 0;
	bool	quit_if_one_screen = false;
	int		search_direction = SEARCH_FORWARD;
	bool	redirect_mode;
	bool	noatty;					/* true, when cannot to get keys from stdin */
	bool	fresh_found = false;
	int		fresh_found_cursor_col = -1;
	bool	reinit = false;

	bool	ignore_mouse_release = false;		/* after leave menu by press ignore release too */
	bool	no_doupdate = false;				/* when we sure stdstr refresh is useles */
	bool	prev_event_is_mouse_press = false;
	int		prev_mouse_event_y = -1;
	int		prev_mouse_event_x = -1;
	bool	only_for_tables = false;
	bool	no_interactive = false;
	bool	interactive = false;
	bool	raw_output_quit = false;

	bool	mouse_was_initialized = false;

	int		last_ordered_column = -1;			/* order by when watch mode is active */
	bool	last_order_desc = false;			/* true, when sort of data is descend */

	long	mouse_event = 0;
	long	vertical_cursor_changed_mouse_event = 0;

	int		file_format_from_suffix = FILE_NOT_SET;
	bool	ignore_file_suffix = false;

	WINDOW	   *win = NULL;
	SCREEN	   *term = NULL;

	int		boot_wait = 0;

#ifdef DEBUG_PIPE

	time_t		start_app_sec;
	long		start_app_ms;
	bool		first_doupdate = true;

#endif

	struct winsize size;
	bool		size_is_valid = false;
	int			ioctl_result;
	bool		handle_timeout = false;
	struct stat statbuf;

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
		{0, 0, 0, 0}
	};

#ifdef COMPILE_MENU

	bool	menu_is_active = false;
	struct ST_MENU		*menu = NULL;
	struct ST_CMDBAR	*cmdbar = NULL;

#endif


	memset(&opts, 0, sizeof(opts));

	opts.pathname = NULL;
	opts.ignore_case = false;
	opts.ignore_lower_case = false;
	opts.no_sound = false;
	opts.no_mouse = false;
	opts.less_status_bar = false;
	opts.no_highlight_search = false;
	opts.force_uniborder = false;
	opts.force8bit = false;
	opts.no_commandbar = false;
	opts.no_topbar = false;
	opts.theme = 1;
	opts.show_rownum = false;
	opts.no_cursor = false;
	opts.vertical_cursor = false;
	opts.tabular_cursor = false;
	opts.freezed_cols = -1;				/* default will be 1 if screen width will be enough */
	opts.force_ascii_art = false;
	opts.bold_labels = false;
	opts.bold_cursor = false;
	opts.tsv_format = false;
	opts.csv_format = false;
	opts.csv_separator = -1;			/* auto detection */
	opts.csv_header = 'a';				/* auto detection */
	opts.nullstr = "";
	opts.ignore_short_rows = false;
	opts.double_header = false;
	opts.border_type = 2;			/* outer border */
	opts.on_sigint_exit = false;
	opts.no_sigint_search_reset = false;
	opts.query = NULL;
	opts.watch_time = 0;
	opts.host = NULL;
	opts.username = NULL;
	opts.port = NULL;
	opts.force_password_prompt = false;
	opts.password = NULL;
	opts.dbname = NULL;
	opts.watch_file = true;
	opts.quit_on_f3 = false;
	opts.no_highlight_lines = false;

	load_config(tilde("~/.pspgconf"), &opts);

	memset(&desc, 0, sizeof(desc));
	memset(&scrdesc, 0, sizeof(scrdesc));

#ifdef DEBUG_PIPE

	debug_pipe = fopen(DEBUG_PIPE, "w");
	setlinebuf(debug_pipe);
	fprintf(debug_pipe, "demo application start\n");

	current_time(&start_app_sec, &start_app_ms);

#endif

	while ((opt = getopt_long(argc, argv, "abs:c:d:f:h:p:XVFgGiIq:U:w:W",
							  long_options, &option_index)) != -1)
	{
		int		n;

		switch (opt)
		{
			case 1:
				fprintf(stderr, "pspg is a Unix pager designed for table browsing.\n\n");
				fprintf(stderr, "Usage:\n");
				fprintf(stderr, "  %s [OPTION]\n", argv[0]);
				fprintf(stderr, "\nGeneral options:\n");
				fprintf(stderr, "  --about                  about authors\n");
				fprintf(stderr, "  --help                   show this help\n");
				fprintf(stderr, "  -V, --version            show version\n\n");
				fprintf(stderr, "\n");
				fprintf(stderr, "  -f, --file=FILE          open file\n");
				fprintf(stderr, "  -F, --quit-if-one-screen\n");
				fprintf(stderr, "                           quit if content is one screen\n");
				fprintf(stderr, "  -X                       don't use alternate screen\n");
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
				fprintf(stderr, "  --rr ROWNUM              rows reserved for specific purposes\n");
				fprintf(stderr, "  --stream                 input file is read continually\n");
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
				fprintf(stderr, "  --null string            string used instead NULL\n");
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
				exit(0);

			case 'a':
				opts.force_ascii_art = true;
				break;
			case 'I':
				opts.ignore_case = true;
				break;
			case 'i':
				opts.ignore_lower_case = true;
				break;
			case 'q':
				opts.query = optarg;
				break;
			case 'w':
				opts.watch_time = atoi(optarg);
				if (opts.watch_time < 0 || opts.watch_time > 3600)
				{
					fprintf(stderr, "query watch time can be between 0 and 3600\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 2:
				opts.no_mouse = true;
				break;
			case 3:
				opts.no_sound = true;
				break;
			case 4:
				opts.less_status_bar = true;
				break;
			case 5:
				opts.force_uniborder = true;
				break;
			case 6:
				opts.no_commandbar = true;
				break;
			case 7:
				opts.no_topbar = true;
				break;
			case 8:
				opts.no_commandbar = true;
				opts.no_topbar = true;
				break;
			case 9:
				opts.show_rownum = true;
				break;
			case 10:
				opts.no_cursor = true;
				break;
			case 11:
				opts.tabular_cursor = true;
				break;
			case 12:
				opts.bold_labels = true;
				break;
			case 13:
				opts.bold_cursor = true;
				break;
			case 14:
				only_for_tables = true;
				break;
			case 15:
				opts.vertical_cursor = true;
				break;
			case 16:
				fprintf(stdout, "The pspg-%s is special pager designed for databases.\n\n", PSPG_VERSION);
				fprintf(stdout, "Authors:\n");
				fprintf(stdout, "    2017-2020 Pavel Stehule, Benesov district, Czech Republic\n\n");
				fprintf(stdout, "Licence:\n");
				fprintf(stdout, "    Distributed under BSD licence\n\n");
				exit(0);
				break;
			case 17:
				opts.csv_format = true;
				break;
			case 18:
				opts.csv_separator = *optarg;
				break;
			case 19:
				n = atoi(optarg);
				if (n < 0 || n > 2)
				{
					fprintf(stderr, "csv border type can be between 0 and 2\n");
					exit(EXIT_FAILURE);
				}
				opts.border_type = n;
				break;
			case 21:
				opts.on_sigint_exit = true;
				break;
			case 22:
				opts.no_sigint_search_reset = true;
				break;
			case 23:
				no_interactive = true;
				break;
			case 24:
				opts.double_header = true;
				break;
			case 25:
				{
					const char *path;

					path = tilde(optarg);
					logfile = fopen(path, "a");
					if (logfile == NULL)
					{
						fprintf(stderr, "cannot to open log file file: %s\n", path);
						exit(EXIT_FAILURE);
					}
					setlinebuf(logfile);
				}
				break;
			case 26:
				reserved_rows = atoi(optarg);
				if (reserved_rows < 1 || reserved_rows > 100)
				{
					fprintf(stderr, "reserved rows should be between 1 and 100\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 27:
				interactive = true;
				break;
			case 28:
				{
					if (nstreq(optarg, "off"))
						opts.csv_header = '-';
					else if (nstreq(optarg, "on"))
						opts.csv_header = '+';
					else
					{
						fprintf(stderr, "csv_header option can be on \"or\" \"off\"\n");
						exit(EXIT_FAILURE);
					}
				}
				break;
			case 29:
				opts.ignore_short_rows = true;
				break;
			case 30:
				opts.tsv_format = true;
				break;
			case 31:
				opts.nullstr = strdup(optarg);
				break;
			case 32:
				ignore_file_suffix = true;
				break;
			case 33:
				opts.watch_file = false;
				break;
			case 34:
				stream_mode = true;
				break;
			case 35:
				opts.quit_on_f3 = true;
				break;
			case 36:
				boot_wait = atoi(optarg);
				if (boot_wait < 0 || boot_wait > 120)
				{
					fprintf(stderr, "wait should be between 1 and 120 (sec)\n");
					exit(EXIT_FAILURE);
				}
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

				exit(0);
			case 'X':
				no_alternate_screen = true;
				break;
			case 'b':
				opts.theme = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > MAX_STYLE)
				{
					fprintf(stderr, "only color schemas 0 .. %d are supported\n", MAX_STYLE);
					exit(EXIT_FAILURE);
				}
				opts.theme = n;
				break;
			case 'c':
				n = atoi(optarg);
				if (n < 0 || n > 9)
				{
					fprintf(stderr, "fixed columns should be between 0 and 4\n");
					exit(EXIT_FAILURE);
				}
				opts.freezed_cols = n;
				break;
			case 'f':
				{
					char *path = tilde(optarg);

					fp = fopen(path, "r");
					if (fp == NULL)
					{
						fprintf(stderr, "cannot to read file: %s\n", path);
						exit(EXIT_FAILURE);
					}
					opts.pathname = strdup(optarg);

					file_format_from_suffix = get_format_type(optarg);
				}
				break;
			case 'F':
				quit_if_one_screen = true;
				break;
			case 'g':
				opts.no_highlight_lines = true;
				break;
			case 'G':
				opts.no_highlight_search = true;
				break;

			case 'h':
				opts.host = strdup(optarg);
				break;
			case 'p':
				{
					long port;

					port = strtol(optarg, NULL, 10);
					if ((port < 1) || (port > 65535))
					{
						fprintf(stderr, "invalid port number: %s\n", optarg);
						exit(EXIT_FAILURE);
					}
					opts.port = strdup(optarg);
				}
				break;
			case 'U':
				opts.username = strdup(optarg);
				break;
			case 'W':
				opts.force_password_prompt = true;
				break;
			case 'd':
				opts.dbname = strdup(optarg);
				break;

			default:
				fprintf(stderr, "Try %s --help\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (boot_wait > 0)
		usleep(1000 * 1000 * boot_wait);

	if (!opts.csv_format && !opts.tsv_format && file_format_from_suffix != FILE_NOT_SET && !ignore_file_suffix)
	{
		if (file_format_from_suffix == FILE_CSV)
			opts.csv_format = true;
		else if (file_format_from_suffix == FILE_TSV)
			opts.tsv_format = true;
	}

	if (opts.watch_time && !(opts.query || opts.pathname))
	{
		fprintf(stderr, "cannot use watch mode when query or file is missing\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * don't use inotify, when user prefer periodic watch time, or when we
	 * have not file for watching
	 */
	if (opts.watch_time || !opts.pathname)
		opts.watch_file = false;

	if (fp)
	{
		if (fstat(fileno(fp), &statbuf ) != 0 )
		{
			fprintf(stderr, "cannot to get fstat file: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		is_fifo = S_ISFIFO(statbuf.st_mode);
	}
	else
		is_fifo = false;

	if (is_fifo)
	{
		stream_mode = true;
		opts.watch_file = true;
	}

	if (opts.watch_file)
	{

		if (!is_fifo)
		{

#ifdef HAVE_INOTIFY

			inotify_fd = inotify_init1(IN_NONBLOCK);
			if (inotify_fd == -1)
			{
				fprintf(stderr, "inotify_init1: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}

			inotify_wd = inotify_add_watch(inotify_fd,
										   opts.pathname,
										   IN_CLOSE_WRITE |
										   (stream_mode ? IN_MODIFY : 0));

			if (inotify_wd == -1)
			{
				fprintf(stderr, "inotify_add_watch(%s): %s\n", opts.pathname, strerror(errno));
				exit(EXIT_FAILURE);
			}

#else

			fprintf(stderr, "missing inotify support\n");
			exit(EXIT_FAILURE);

#endif

		}
	}

	if (stream_mode)
	{
		if (is_fifo)
		{
			/* use nonblock mode */
			fcntl(fileno(fp), F_SETFL, O_NONBLOCK);
		}
		else
		{

#ifndef HAVE_INOTIFY

			fprintf(stderr, "streaming is not available without inotify support\n");
			exit(EXIT_FAILURE);

#endif

		}

		fseek(fp, 0, SEEK_END);
	}

	if (no_interactive && interactive)
	{
		fprintf(stderr, "option --ni and --interactive cannot be used together\n");
		exit(EXIT_FAILURE);
	}

	if (opts.query && opts.pathname)
	{
		fprintf(stderr, "option --query and --file cannot be used together\n");
		exit(EXIT_FAILURE);
	}

	if (opts.csv_format && opts.tsv_format)
	{
		fprintf(stderr, "option --csv and --tsv cannot be used together\n");
		exit(EXIT_FAILURE);
	}

	if (opts.less_status_bar)
		opts.no_topbar = true;

	setlocale(LC_ALL, "");

	/* Don't use UTF when terminal doesn't use UTF */
	opts.force8bit = strcmp(nl_langinfo(CODESET), "UTF-8") != 0;

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "started\n");
	}

	if (opts.csv_format || opts.tsv_format || opts.query)
	{
		/*
		 * ToDo: first query can be broken too in watch mode.
		 */
		if (!read_and_format(fp, &opts, &desc, &err))
		{
			fprintf(stderr, "%s\n", err);
			exit(EXIT_FAILURE);
		}
	}
	else
		readfile(fp, &opts, &desc);

	if (opts.watch_time > 0)
	{
		current_time(&last_watch_sec, &last_watch_ms);
		next_watch = last_watch_sec * 1000 + last_watch_ms + opts.watch_time * 1000;
	}

	if (fp != NULL && !stream_mode)
	{
		fclose(fp);
		fp = NULL;
	}

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "read input %d rows\n", desc.total_rows);
	}

	if ((opts.csv_format || opts.tsv_format || opts.query) &&
		(no_interactive || (!interactive && !isatty(STDOUT_FILENO))))
	{
		LineBuffer *lnb = &desc.rows;
		int			lnb_row = 0;

		/* write formatted data to stdout and quit */
		while (lnb)
		{
			while (lnb_row < lnb->nrows)
				fprintf(stdout, "%s\n", lnb->rows[lnb_row++]);

			lnb = lnb->next;
			lnb_row = 0;
		}

		return 0;
	}

	if (desc.headline)
		(void) translate_headline(&opts, &desc);

	detected_format = desc.headline_transl;

	if (detected_format && desc.oid_name_table)
		default_freezed_cols = 2;

	/*
	 * The issue #75 - COLUMNS, LINES are not correctly initialized.
	 * Get real terminal size, and refresh ncurses data.
	 */
	if ((ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size)) >= 0)
	{
		size_is_valid = true;
		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "terminal size by TIOCGWINSZ rows: %d, cols: %d\n", size.ws_row, size.ws_col);
		}
	}
	else
	{
		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "cannot to detect terminal size via TIOCGWINSZ: res: %d\n", ioctl_result);
		}
	}

	/* When we know terminal dimensions */
	if (size_is_valid && quit_if_one_screen)
	{
		int		available_rows = size.ws_row;

		if (reserved_rows != -1)
			available_rows -= reserved_rows;

		/* the content can be displayed in one screen */
		if (available_rows >= desc.last_row && size.ws_col > desc.maxx)
		{
			LineBuffer *lnb = &desc.rows;
			int			lnb_row = 0;

			endwin();

			while (lnb_row < lnb->nrows)
				printf("%s\n", lnb->rows[lnb_row++]);

			if (logfile)
			{
				print_log_prefix(logfile);
				fprintf(logfile, "quit due quit_if_one_screen option without ncurses init\n");
			}

			return 0;
		}
	}

	if (!detected_format && only_for_tables)
	{
		const char *pagerprog;
		FILE	   *fout = NULL;
		LineBuffer *lnb = &desc.rows;
		int			lnb_row = 0;

		pagerprog = getenv("PSPG_PAGER");
		if (!pagerprog)
			pagerprog = getenv("PAGER");
		if (!pagerprog)
			pagerprog = "more";
		else
		{
			/* if PAGER is empty or all-white-space, don't use pager */
			if (strspn(pagerprog, " \t\r\n") == strlen(pagerprog))
				fout = stdout;
		}

		if (!fout)
		{
			fout = popen(pagerprog, "w");
			if (!fout)
			{
				/* if popen fails, silently proceed without pager */
				fout = stdout;
			}
		}

		if (fout != stdout)
		{
			signal(SIGPIPE, SIG_IGN);
			signal(SIGINT, SIG_IGN);
		}

		while (lnb)
		{
			while (lnb_row < lnb->nrows)
			{
				int r;
				r = fprintf(fout, "%s\n", lnb->rows[lnb_row++]);
				if (r < 0)
					goto exit_while_01;
			}
			lnb = lnb->next;
			lnb_row = 0;
		}

exit_while_01:

		if (fout != stdout)
			pclose(fout);

		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "exit without start ncurses\n");
			fclose(logfile);
		}

		return 0;
	}

	if (!isatty(fileno(stdin)))
	{
		if (freopen("/dev/tty", "r", stdin) != NULL)
			noatty = false;
		else if (freopen(ttyname(fileno(stdout)), "r", stdin) != NULL)
			noatty = false;
		else
		{
			/*
			 * cannot to reopen terminal device. See discussion to issue #35
			 * fallback solution - read keys directly from stderr. Just check
			 * it it is possible.
			 */
			if (!isatty(fileno(stderr)))
			{
				fprintf(stderr, "missing a access to terminal device\n");
				exit(EXIT_FAILURE);
			}
			noatty = true;
			fclose(stdin);
		}
	}
	else
		noatty = false;

	signal(SIGINT, SigintHandler);
	atexit(exit_ncurses);

	if (noatty)
		/* use stderr like stdin. This is fallback solution used by less */
		term = newterm(termname(), stdout, stderr);
	else
		win = initscr();

	(void) term;
	(void) win;

	if (opts.watch_file)
	{
		fds[0].fd = noatty ? STDERR_FILENO : STDIN_FILENO;
		fds[0].events = POLLIN;

		if (is_fifo)
		{
			fds[1].fd = fileno(fp);
			fds[1].events = POLLIN;
		}
		else
		{
			fds[1].fd = inotify_fd;
			fds[1].events = POLLIN;
		}
	}
	else
	{
		fds[0].fd = -1;
		fds[1].fd = -1;
	}

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "ncurses started\n");
	}

	active_ncurses = true;

	if(!has_colors())
		leave_ncurses("your terminal does not support color");

	start_color();

reinit_theme:

	initialize_color_pairs(opts.theme, opts.bold_labels, opts.bold_cursor);

	timeout(1000);

	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	wbkgdset(stdscr, COLOR_PAIR(1));

#ifdef NCURSES_EXT_FUNCS

	set_escdelay(25);

#endif

	initialize_special_keycodes();

	if (!opts.no_mouse)
	{
		mouse_was_initialized = true;
		mouseinterval(0);


#if NCURSES_MOUSE_VERSION > 1

		mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | BUTTON_ALT, NULL);

#else

		mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED, NULL);

#endif

	}

	if (desc.headline_transl != NULL && !desc.is_expanded_mode)
	{
		if (desc.border_head_row != -1)
			desc.first_data_row = desc.border_head_row + 1;
	}
	else if (desc.title_rows > 0 && desc.is_expanded_mode)
		desc.first_data_row = desc.title_rows;
	else
	{
		desc.first_data_row = 0;
		desc.last_data_row = desc.last_row;
		desc.title_rows = 0;
		desc.title[0] = '\0';
	}

	first_data_row = desc.first_data_row;

	trim_footer_rows(&opts, &desc);

	if (reinit)
	{
		ScrDesc		aux;
		int			i;

		/* we should to save searching related data from scrdesc */
		memcpy(&aux, &scrdesc, sizeof(ScrDesc));

		for (i = 0; i < 9; i++)
			if (scrdesc.wins[i])
				delwin(scrdesc.wins[i]);

		memset(&scrdesc, 0, sizeof(ScrDesc));
		MergeScrDesc(&scrdesc, &aux);
	}
	else
		memset(&scrdesc, 0, sizeof(ScrDesc));

	initialize_theme(opts.theme, WINDOW_TOP_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_TOP_BAR]);
	initialize_theme(opts.theme, WINDOW_BOTTOM_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_BOTTOM_BAR]);

	if (size_is_valid)
		resize_term(size.ws_row, size.ws_col);

	clear();

	refresh_aux_windows(&opts, &scrdesc);

	getmaxyx(stdscr, maxy, maxx);

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "screen size - maxy: %d, maxx: %d\n", maxy, maxx);
	}

	if (quit_if_one_screen)
	{
		int		available_rows = maxy;

		if (reserved_rows != -1)
			available_rows -= reserved_rows;

		/* the content can be displayed in one screen */
		if (available_rows >= desc.last_row && maxx >= desc.maxx)
		{
			LineBuffer *lnb = &desc.rows;
			int			lnb_row = 0;

			endwin();

			while (lnb_row < lnb->nrows)
				printf("%s\n", lnb->rows[lnb_row++]);

			if (logfile)
			{
				print_log_prefix(logfile);
				fprintf(logfile, "ncurses ended and quit due quit_if_one_screen option\n");
			}

			return 0;
		}
	}

	/* some corrections */
	if (detected_format)
	{
		if (desc.is_expanded_mode)
		{
			if (strchr(desc.headline_transl,'I') == NULL)
			{
				char *str = desc.rows.rows[desc.title_rows + 1];
				int pos = 0;

				/* fallback point, didn't find separator already */
				while (pos < 40)
				{
					if ((desc.linestyle == 'a' && *str == '|' && pos > 1) ||
					    (desc.linestyle == 'u' && pos > 1 &&
					    (strncmp(str, /* │ */ "\342\224\202", 3) == 0 ||
					     strncmp(str, /* ║ */ "\342\225\221", 3) == 0)))
					{
						desc.headline_transl[pos] = 'I';
						break;
					}
					pos += 1;
					str += opts.force8bit ? 1 : utf8charlen(*str);
				}
			}
		}
		else
		{
			if (desc.border_type != 2)
			{
				if (desc.border_bottom_row == -1 && desc.footer_row == -1)
				{
					if (desc.alt_footer_row != -1 && desc.border_type == 1)
					{
						desc.footer_row = desc.alt_footer_row;
						desc.last_data_row = desc.footer_row - 1;
					}
					else
					{
						/* fallback */
						desc.last_data_row = desc.last_row - 1;
						desc.footer_row = desc.last_row;
					}
				}

				trim_footer_rows(&opts, &desc);
			}
		}
	}

	if (opts.tabular_cursor && !opts.no_cursor)
		opts.no_cursor = desc.headline_transl == NULL;

	/* run this part only once, don't repeat it when theme is reinitialized */
	if (opts.vertical_cursor && desc.columns > 0 && vertical_cursor_column == -1)
	{
		int freezed_cols = opts.freezed_cols != -1 ?  opts.freezed_cols : default_freezed_cols;

		/* The position of vertical cursor should be set */
		if (freezed_cols + 1 <= desc.columns)
			vertical_cursor_column = freezed_cols + 1;
		else
			vertical_cursor_column = 1;

		/* in this moment, there are not any vertical offset, calculation is simple */
		last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
	}

	initialize_theme(opts.theme, WINDOW_ROWNUM_LUC, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWNUM_LUC]);
	initialize_theme(opts.theme, WINDOW_ROWNUM, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWNUM]);

	create_layout_dimensions(&opts, &scrdesc, &desc, opts.freezed_cols != -1 ? opts.freezed_cols : default_freezed_cols, fixedRows, maxy, maxx);
	create_layout(&opts, &scrdesc, &desc, first_data_row, first_row);

	initialize_theme(opts.theme, WINDOW_LUC, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_LUC]);
	initialize_theme(opts.theme, WINDOW_FIX_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_ROWS]);
	initialize_theme(opts.theme, WINDOW_FIX_COLS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FIX_COLS]);
	initialize_theme(opts.theme, WINDOW_ROWS, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_ROWS]);
	initialize_theme(opts.theme, WINDOW_FOOTER, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_FOOTER]);

	print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, 0, vertical_cursor_column);

	/* initialize readline if it is active */
#ifdef HAVE_LIBREADLINE

	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;

	last_row_search[0] = '\0';
	last_col_search[0] = '\0';
	last_line[0] = '\0';
	last_path[0] = '\0';

#if RL_READLINE_VERSION > 0x0603

	rl_change_environment = 0;

#endif

	rl_inhibit_completion = 1;

#ifdef HAVE_READLINE_HISTORY

	if (!reinit)
		read_history(tilde("~/.pspg_history"));

	last_history[0] = '\0';

#endif

#endif

#ifdef COMPILE_MENU

	init_menu_config(&opts);
	if (!opts.less_status_bar && !opts.no_commandbar)
		cmdbar = init_cmdbar(cmdbar, &opts);

#endif

	while (true)
	{
		bool	refresh_scr = false;
		bool	resize_scr = false;
		bool	after_freeze_signal = false;
		bool	recheck_vertical_cursor_visibility = false;
		bool	force_refresh = false;

#ifdef DEBUG_PIPE

		time_t	start_draw_sec;
		long	start_draw_ms;

#endif

		fix_rows_offset = desc.fixed_rows - scrdesc.fix_rows_rows;

		/*
		 * Next code allows to inject event, and later process original event again.
		 * It is used for reuse mouse event: 1. replace top bar by menubar, 2. activate
		 * field on menubar - possibly pulldown menu. Following code holds event one
		 * iteration.
		 */
		if (reuse_event)
		{
			/* unfortunately, gcc raises false warning here -Wmaybe-uninitialized */
			if (prev_event_keycode == 0)
			{
				prev_event_keycode = event_keycode;
			}
			else
			{
				next_event_keycode = prev_event_keycode;
				reuse_event = false;
				prev_event_keycode = 0;
			}
		}

		/*
		 * Draw windows, only when function (key) redirect was not forced.
		 * Redirect emmit immediate redraw.
		 */
		if (next_command == cmd_Invalid)
		{
			if (!no_doupdate && !handle_timeout)
			{
				int		vcursor_xmin_fix = -1;
				int		vcursor_xmax_fix = -1;
				int		vcursor_xmin_data = -1;
				int		vcursor_xmax_data = -1;

				if (opts.vertical_cursor && desc.columns > 0 && vertical_cursor_column > 0)
				{
					int		vcursor_xmin = desc.cranges[vertical_cursor_column - 1].xmin;
					int		vcursor_xmax = desc.cranges[vertical_cursor_column - 1].xmax;

					if (vcursor_xmin < scrdesc.fix_cols_cols)
					{
						vcursor_xmin_fix = vcursor_xmin;
						vcursor_xmin_data = vcursor_xmin - scrdesc.fix_cols_cols;
					}
					else
					{
						vcursor_xmin_fix = vcursor_xmin - cursor_col;
						vcursor_xmin_data = vcursor_xmin - scrdesc.fix_cols_cols - cursor_col;
					}

					if (vcursor_xmax < scrdesc.fix_cols_cols)
					{
						vcursor_xmax_fix = vcursor_xmax;
						vcursor_xmax_data = vcursor_xmax - scrdesc.fix_cols_cols;
					}
					else
					{
						vcursor_xmax_fix = vcursor_xmax - cursor_col;
						vcursor_xmax_data = vcursor_xmax - scrdesc.fix_cols_cols - cursor_col;
					}

					/*
					 * When vertical cursor is not in freezed columns, then it cannot to
					 * overwrite fixed col cols. Only last char position can be shared.
					 */
					if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols))
						if (vcursor_xmin_fix < scrdesc.fix_cols_cols - 1)
							vcursor_xmin_fix = scrdesc.fix_cols_cols - 1;
				}

#ifdef DEBUG_PIPE

				current_time(&start_draw_sec, &start_draw_ms);

#endif

				window_fill(WINDOW_LUC,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							0,
							-1,
							vcursor_xmin_fix, vcursor_xmax_fix,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWS,
							first_data_row + first_row - fix_rows_offset,
							scrdesc.fix_cols_cols + cursor_col,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_data, vcursor_xmax_data,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_COLS,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_fix, vcursor_xmax_fix,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_ROWS,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							scrdesc.fix_cols_cols + cursor_col,
							-1,
							vcursor_xmin_data, vcursor_xmax_data,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FOOTER,
							first_data_row + first_row + scrdesc.rows_rows - fix_rows_offset,
							footer_cursor_col,
							cursor_row - first_row - scrdesc.rows_rows + fix_rows_offset,
							-1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM_LUC,
							0,
							0,
							0,
							-1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							-1, -1,
							&desc, &scrdesc, &opts);

				if (w_luc(&scrdesc))
					wnoutrefresh(w_luc(&scrdesc));
				if (w_rows(&scrdesc))
					wnoutrefresh(w_rows(&scrdesc));
				if (w_fix_cols(&scrdesc))
					wnoutrefresh(w_fix_cols(&scrdesc));
				if (w_fix_rows(&scrdesc))
					wnoutrefresh(w_fix_rows(&scrdesc));
				if (w_footer(&scrdesc))
					wnoutrefresh(w_footer(&scrdesc));
				if (w_rownum(&scrdesc))
					wnoutrefresh(w_rownum(&scrdesc));
				if (w_rownum_luc(&scrdesc))
					wnoutrefresh(w_rownum_luc(&scrdesc));

#ifdef DEBUG_PIPE

			print_duration(start_draw_sec, start_draw_ms, "draw time");

#endif

			} /* !no_doupdate */

#ifdef COMPILE_MENU

			if (cmdbar)
				st_cmdbar_post(cmdbar);

			if (menu != NULL && menu_is_active)
			{
				st_menu_post(menu);
				st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
			}

#endif

			if (no_doupdate)
				no_doupdate = false;
			else if (next_command == 0 || scrdesc.fmt != NULL)
			{
#ifdef DEBUG_PIPE

				current_time(&start_draw_sec, &start_draw_ms);

#endif


				doupdate();

#ifdef DEBUG_PIPE

			print_duration(start_draw_sec, start_draw_ms, "doupdate");

#endif


#ifdef DEBUG_PIPE

				if (first_doupdate)
				{
					first_doupdate = false;
					print_duration(start_app_sec, start_app_ms, "first view");
				}

#endif

			}

			if (scrdesc.fmt != NULL)
			{
				next_event_keycode = show_info_wait(&opts, &scrdesc,
									scrdesc.fmt, scrdesc.par, scrdesc.beep,
									false, scrdesc.applytimeout,
									scrdesc.is_error);
				if (scrdesc.fmt != NULL)
				{
					free(scrdesc.fmt);
					scrdesc.fmt = NULL;
				}
				if (scrdesc.par != NULL)
				{
					free(scrdesc.par);
					scrdesc.par = NULL;
				}

				refresh_aux_windows(&opts, &scrdesc);
				continue;
			}

			if (next_event_keycode != 0)
			{
				event_keycode = next_event_keycode;
				next_event_keycode = 0;
			}
			else
			{
				bool		handle_file_event;
				bool		reopen_file;

				/*
				 * Store previous event, if this event is mouse press. With it we
				 * can join following mouse release together, and reduce useles
				 * refresh.
				 */
				if (event_keycode == KEY_MOUSE && event.bstate == BUTTON1_PRESSED)
				{
					prev_event_is_mouse_press = true;
					prev_mouse_event_y = event.y;
					prev_mouse_event_x = event.x;
				}
				else
					prev_event_is_mouse_press = false;

				event_keycode = get_event(&event,
										  &press_alt,
										  &got_sigint,
										  &handle_timeout,
										  &handle_file_event,
										  &reopen_file,
										  opts.watch_time > 0 ? 1000 : -1);

force_refresh_data:

				if (force_refresh ||
					opts.watch_time ||
					(opts.watch_file && handle_file_event))
				{
					long	ms;
					time_t	sec;
					long	ct;

					current_time(&sec, &ms);
					ct = sec * 1000 + ms;

					if (force_refresh ||
						(ct > next_watch && !paused) ||
						(opts.watch_file && handle_file_event))
					{
						FILE	   *fp2 = NULL;
						DataDesc	desc2;
						bool		fresh_data = false;

						memset(&desc2, 0, sizeof(desc2));

						if (opts.pathname)
						{
							char *path = tilde(opts.pathname);

							if (fp)
							{
								/* should be strem mode */
								if (reopen_file)
								{
									fclose(fp);

									errno = 0;
									fp = fopen(path, "r");
									if (fp == NULL)
									{
										err = strerror(errno);
										fresh_data = false;
									}
									else
									{
										fresh_data = true;

										if (stream_mode)
											fseek(fp, 0, SEEK_END);
									}
								}
								else
								{
									clearerr(fp);
									fresh_data = true;
								}

								fp2 = fp;
							}
							else
							{
								errno = 0;
								fp2 = fopen(path, "r");
								if (fp2 == NULL)
								{
									err = strerror(errno);
									fresh_data = false;
								}
								else
									fresh_data = true;
							}
						}
						else if (opts.query)
							fresh_data = true;
						else
							/* We cannot to repeat read from stdin */
							fresh_data = false;

						/* when we wanted fresh data */
						if (fresh_data)
						{
							if (opts.csv_format || opts.tsv_format || opts.query)
								/* returns false when format is broken */
								fresh_data = read_and_format(fp2, &opts, &desc2, &err);
							else
								fresh_data = readfile(fp2, &opts, &desc2);

							if (!stream_mode)
								fclose(fp2);
						}

						/* when we have fresh data */
						if (fresh_data)
						{
							int		max_cursor_row;
							ScrDesc		aux;

							DataDescFree(&desc);
							memcpy(&desc, &desc2, sizeof(desc));

							if (desc.headline)
								(void) translate_headline(&opts, &desc);

							if (desc.headline_transl != NULL && !desc.is_expanded_mode)
							{
								if (desc.border_head_row != -1)
									desc.first_data_row = desc.border_head_row + 1;
							}
							else if (desc.title_rows > 0 && desc.is_expanded_mode)
								desc.first_data_row = desc.title_rows;
							else
							{
								desc.first_data_row = 0;
								desc.last_data_row = desc.last_row;
								desc.title_rows = 0;
								desc.title[0] = '\0';
							}

							first_data_row = desc.first_data_row;

							detected_format = desc.headline_transl;
							if (detected_format && desc.oid_name_table)
								default_freezed_cols = 2;

							/* we should to save searching related data from scrdesc */
							memcpy(&aux, &scrdesc, sizeof(ScrDesc));

							create_layout_dimensions(&opts, &scrdesc, &desc, opts.freezed_cols != -1 ? opts.freezed_cols : default_freezed_cols, fixedRows, maxy, maxx);

							/* create_layout requires correct first_row */
							first_row = adjust_first_row(first_row, &desc, &scrdesc);
							create_layout(&opts, &scrdesc, &desc, first_data_row, first_row);

							MergeScrDesc(&scrdesc, &aux);

							/* new result can have different number of row, check cursor */
							max_cursor_row = MAX_CURSOR_ROW;
							cursor_row = cursor_row > max_cursor_row ? max_cursor_row : cursor_row;

							if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
								first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

							first_row = adjust_first_row(first_row, &desc, &scrdesc);

							last_watch_sec = sec; last_watch_ms = ms;

							if (last_ordered_column != -1)
								update_order_map(&opts, &scrdesc, &desc, last_ordered_column, last_order_desc);
						}
						else
							DataDescFree(&desc2);

						if ((ct - next_watch) < (opts.watch_time * 1000))
							next_watch = next_watch + 1000 * opts.watch_time;
						else
							next_watch = ct + 100 * opts.watch_time;

						clear();
						refresh_scr = true;
						handle_timeout = false;
					}

					print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset, vertical_cursor_column);
					if (scrdesc.wins[WINDOW_TOP_BAR])
						wrefresh(scrdesc.wins[WINDOW_TOP_BAR]);

					if (force_refresh)
					{
						force_refresh = false;
						event_keycode = 0;
						next_event_keycode = 0;
						next_command = 0;
						command = 0;
					}
				}

				/* the comment for ignore_mouse_release follow */
				if (ignore_mouse_release)
				{
					ignore_mouse_release = false;
					if (event_keycode == KEY_MOUSE && event.bstate & event.bstate & BUTTON1_RELEASED)
					{
						no_doupdate = true;
						continue;
					}
				}
			}

			redirect_mode = false;
		}
		else
		{
			command = next_command;
			next_command = cmd_Invalid;
			redirect_mode = true;
			no_doupdate = false;
		}

		/* Exit immediately on F10 or input error */
		if (got_sigint)
		{
			if (!opts.no_sigint_search_reset &&
				  (*scrdesc.searchterm || *scrdesc.searchcolterm))
			{
				*scrdesc.searchterm = '\0';
				*scrdesc.searchcolterm = '\0';

				scrdesc.searchterm_size = 0;
				scrdesc.searchterm_char_size = 0;

				reset_searching_lineinfo(&desc.rows);
			}
			else
			{
				if (opts.on_sigint_exit)
					break;
				else
					show_info_wait(&opts, &scrdesc, " For quit press \"q\" (or use on-sigint-exit option).", NULL, true, true, true, false);
			}
		}
		else if ((event_keycode == ERR || event_keycode == KEY_F(10)) && !redirect_mode)
		{

#ifdef DEBUG_PIPE

			fprintf(debug_pipe, "exit main loop: %s\n", event_keycode == ERR ? "input error" : "F10");

#endif

			if (logfile)
			{
				print_log_prefix(logfile);
				fprintf(logfile, "exit main loop: %s\n", event_keycode == ERR ? "input error" : "F10");
			}

			break;
		}

#ifndef COMPILE_MENU

		if (!redirect_mode)
		{
			translated_command_history = translated_command;
			command = translate_event(event_keycode, press_alt, &opts);
			translated_command = command;
		}

#else

		/*
		 * Don't send RESIZE to menu. It cannot to handle this event, and it
		 * cannot to translate this event. This event is lost in menu.
		 * So, don't do it.
		 */
		if (!redirect_mode && event_keycode != KEY_RESIZE)
		{
			bool	processed = false;
			bool	activated = false;
			ST_MENU_ITEM		*ami;
			ST_CMDBAR_ITEM		*aci;

			processed = st_menu_driver(menu, event_keycode, press_alt, &event);
			if (processed)
			{
				ami = st_menu_selected_item(&activated);
				if (activated)
				{
					next_command = ami->code;
					goto hide_menu;
				}

				aci = st_menu_selected_command(&activated);
				if (activated)
				{
					next_command = aci->code;
					goto refresh;
				}
			}

			if (menu_is_active && !processed &&
					(event_keycode == ST_MENU_ESCAPE || event_keycode == KEY_MOUSE))
			{
hide_menu:
				st_menu_unpost(menu, true);
				menu_is_active = false;
				st_menu_set_focus(menu, ST_MENU_FOCUS_NONE);

				/*
				 * When we leave menu due mouse action, and this mouse action
				 * is button1 press, then we would to ignore button1 release.
				 * The behave is consistent for this mouse click (press, release).
				 */
				if (event_keycode == KEY_MOUSE && event.bstate & event.bstate & BUTTON1_PRESSED)
					ignore_mouse_release = true;

				goto refresh;
			}

			if (!processed)
			{
				translated_command_history = translated_command;
				command = translate_event(event_keycode, press_alt, &opts);
				translated_command = command;
			}
			else
				continue;
		}
		else
		{
			if (!redirect_mode)
			{
				translated_command_history = translated_command;
				command = translate_event(event_keycode, press_alt, &opts);
				translated_command = command;
			}
		}

#endif

		prev_first_row = first_row;

#ifdef DEBUG_PIPE

		fprintf(debug_pipe, "main switch: %s\n", cmd_string(command));

#endif

		if (logfile)
		{
			print_log_prefix(logfile);
			fprintf(logfile, "process command: %s\n", cmd_string(command));
		}

		if (command == cmd_Quit)
			break;
		else if (command == cmd_Invalid)
			continue;
		else if (command == cmd_RawOutputQuit)
		{
			raw_output_quit = true;
			break;
		}
		else if (command == cmd_Escape)
		{
			/* same like sigterm handling */
			if (!opts.no_sigint_search_reset &&
				  (*scrdesc.searchterm || *scrdesc.searchcolterm))
			{
				*scrdesc.searchterm = '\0';
				*scrdesc.searchcolterm = '\0';

				scrdesc.searchterm_size = 0;
				scrdesc.searchterm_char_size = 0;

				reset_searching_lineinfo(&desc.rows);
			}
			else
			{
				if (opts.on_sigint_exit)
					break;
				else
					show_info_wait(&opts, &scrdesc, " For quit press \"q\" (or use on-sigint-exit option).", NULL, true, true, true, false);
			}
		}

		switch (command)
		{

#ifdef COMPILE_MENU

			case cmd_ShowMenu:
				{
					if (menu == NULL || reinit)
					{
						st_menu_set_desktop_window(stdscr);
						init_menu_config(&opts);
						menu = init_menu(menu);
					}

					st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
					post_menu(&opts, menu);

					menu_is_active = true;
					continue;
				}

#endif

			case cmd_NoHighlight:
				opts.no_highlight_search = true;
				opts.no_highlight_lines = false;
				goto reset_search;

			case cmd_HighlightValues:
				opts.no_highlight_search = false;
				opts.no_highlight_lines = true;
				goto reset_search;

			case cmd_HighlightLines:
				opts.no_highlight_search = false;
				opts.no_highlight_lines = false;
				goto reset_search;

			case cmd_CISearchSet:
				opts.ignore_lower_case = false;
				opts.ignore_case = true;
				goto reset_search;

			case cmd_USSearchSet:
				opts.ignore_lower_case = true;
				opts.ignore_case = false;
				goto reset_search;

			case cmd_CSSearchSet:
				opts.ignore_lower_case = false;
				opts.ignore_case = false;

reset_search:
				scrdesc.searchterm[0] = '\0';
				scrdesc.searchterm_size = 0;
				scrdesc.searchterm_char_size = 0;

				reset_searching_lineinfo(&desc.rows);
				break;

			case cmd_ShowTopBar:
				opts.no_topbar = !opts.no_topbar;
				refresh_scr = true;
				break;

			case cmd_ShowBottomBar:
				opts.no_commandbar = !opts.no_commandbar;
				if (opts.no_commandbar)
				{
					if (cmdbar)
					{
						st_cmdbar_unpost(cmdbar);
						st_cmdbar_free(cmdbar);
						cmdbar = NULL;
					}
				}
				else
					if (!opts.less_status_bar)
					cmdbar = init_cmdbar(cmdbar, &opts);

				refresh_scr = true;
				break;

			case cmd_RowNumToggle:
				opts.show_rownum = !opts.show_rownum;
				refresh_scr = true;
				break;

			case cmd_UtfArtToggle:
				opts.force_uniborder = !opts.force_uniborder;
				refresh_scr = true;
				break;

			case cmd_MenuAsciiArtToggle:
				opts.force_ascii_art = !opts.force_ascii_art;
				reinit = true;
				goto reinit_theme;

			case cmd_SoundToggle:
				opts.no_sound = !opts.no_sound;
				break;

			case cmd_SaveSetup:
				if (!save_config(tilde("~/.pspgconf"), &opts))
				{
					if (errno != 0)
						show_info_wait(&opts, &scrdesc, " Cannot write to ~/.pspgconf (%s) (press any key)", strerror(errno), true, true, false, true);
					else
						show_info_wait(&opts, &scrdesc, " Cannot write to ~/.pspgconf (press any key)", NULL, true, true, false, true);
				}
				else
					show_info_wait(&opts, &scrdesc, " Setup saved to ~/.pspgconf", NULL, true, true, true, false);
				break;

			case cmd_SetTheme_MidnightBlack:
			case cmd_SetTheme_Midnight:
			case cmd_SetTheme_Foxpro:
			case cmd_SetTheme_Pdmenu:
			case cmd_SetTheme_White:
			case cmd_SetTheme_Mutt:
			case cmd_SetTheme_Pcfand:
			case cmd_SetTheme_Green:
			case cmd_SetTheme_Blue:
			case cmd_SetTheme_WP:
			case cmd_SetTheme_Lowcontrast:
			case cmd_SetTheme_Darkcyan:
			case cmd_SetTheme_Paradox:
			case cmd_SetTheme_DBase:
			case cmd_SetTheme_DBasemagenta:
			case cmd_SetTheme_Red:
			case cmd_SetTheme_Simple:
			case cmd_SetTheme_SolarDark:
			case cmd_SetTheme_SolarLight:
			case cmd_SetTheme_GruvboxLight:
			case cmd_SetTheme_TaoLight:
				opts.theme = cmd_get_theme(command);
				reinit = true;
				goto reinit_theme;

			case cmd_BoldLabelsToggle:
				opts.bold_labels = !opts.bold_labels;
				reinit = true;
				goto reinit_theme;

			case cmd_BoldCursorToggle:
				opts.bold_cursor = !opts.bold_cursor;
				reinit = true;
				goto reinit_theme;

			case cmd_MouseToggle:
				{
					if (!opts.no_mouse)
					{
						mousemask(0, &prev_mousemask);
						opts.no_mouse = true;
					}
					else
					{

						if (!mouse_was_initialized)
						{
							mouseinterval(0);

#if NCURSES_MOUSE_VERSION > 1

							mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | BUTTON_ALT, NULL);

#else

							mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED, NULL);

#endif

							mouse_was_initialized = true;
						}
						else
							mousemask(prev_mousemask, NULL);

						opts.no_mouse= false;

					}

					show_info_wait(&opts, &scrdesc, " mouse handling: %s ", opts.no_mouse ? "off" : "on", false, true, true, false);
					break;
				}

			case cmd_ShowCursor:
				opts.no_cursor = !opts.no_cursor;
				refresh_scr = true;
				break;

			case cmd_ShowVerticalCursor:
				{
					if (desc.columns == 0)
					{
						show_info_wait(&opts, &scrdesc, " Vertical cursor is available only for tables.", NULL, true, true, true, false);
						break;
					}

					opts.vertical_cursor = !opts.vertical_cursor;

					if (opts.vertical_cursor)
					{
						int		i;
						int		xpoint;
						int		prev_command = translated_command_history;

						if (scrdesc.found && (
								prev_command == cmd_SearchPrev || prev_command == cmd_SearchNext ||
								prev_command == cmd_ForwardSearch || prev_command == cmd_BackwardSearch))
						{
							/*
							 * When immediately previous command was some search command, try to
							 * set vertical cursor by searching result.
							 */
							for (i = 0; i < desc.columns; i++)
							{
								if (desc.cranges[i].xmin <= scrdesc.found_start_x &&
										scrdesc.found_start_x < desc.cranges[i].xmax)
								{
									vertical_cursor_column = i + 1;
									last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
									break;
								}
							}
						}

						if (last_x_focus == -1)
						{
							/* try to find first visible columns after fixed columns */
							last_x_focus = scrdesc.fix_cols_cols;
						}

						if (last_x_focus >= scrdesc.fix_cols_cols - 1)
							xpoint = last_x_focus + cursor_col;
						else
							xpoint = last_x_focus;

						for (i = 0; i  < desc.columns; i++)
						{
							if (desc.cranges[i].xmin <= xpoint && desc.cranges[i].xmax > xpoint)
							{
								vertical_cursor_column = i + 1;

								if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols))
								{
									if (desc.cranges[i].xmax > scrdesc.main_maxx + cursor_col)
									{
										cursor_col = desc.cranges[i].xmax - scrdesc.main_maxx;
									}
									else if (desc.cranges[i].xmin < scrdesc.fix_cols_cols + cursor_col)
									{
										cursor_col = desc.cranges[i].xmin - scrdesc.fix_cols_cols + 1;
									}
								}

								break;
							}
						}
					}

					refresh_scr = true;
				}
				break;

			case cmd_FlushBookmarks:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;

					while (lnb != NULL)
					{
						if (lnb->lineinfo != NULL)
						{
							rownum_cursor_row = 0;

							while (rownum_cursor_row < lnb->nrows)
							{
								if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
									lnb->lineinfo[rownum_cursor_row].mask ^= LINEINFO_BOOKMARK;

								rownum_cursor_row += 1;
							}
						}

						lnb = lnb->next;
					}
				}
				break;

			case cmd_ToggleBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int			lnb_row;
					int			_cursor_row = cursor_row + scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset;

					if (desc.order_map)
					{
						lnb = desc.order_map[_cursor_row].lnb;
						lnb_row = desc.order_map[_cursor_row].lnb_row;
					}
					else
					{
						/* skip first x LineBuffers */
						while (_cursor_row > 1000)
						{
							lnb = lnb->next;
							_cursor_row -= 1000;
						}

						lnb_row = _cursor_row;
					}

					if (lnb->lineinfo == NULL)
					{
						lnb->lineinfo = malloc(1000 * sizeof(LineInfo));
						if (lnb->lineinfo == NULL)
							leave_ncurses("out of memory");

						memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));
					}

					lnb->lineinfo[lnb_row].mask ^= LINEINFO_BOOKMARK;
				}
				break;

			case cmd_PrevBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;
					int		rownum = 0;
					bool	found = false;

					/* start from previous line before cursor */
					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET - 1;

					if (rownum_cursor_row >= 0)
					{
						if (desc.order_map)
						{
							while (rownum_cursor_row >= 0)
							{
								MappedLine *mp = &desc.order_map[rownum_cursor_row];

								if (mp->lnb->lineinfo)
								{
									if ((mp->lnb->lineinfo[mp->lnb_row].mask & LINEINFO_BOOKMARK) != 0)
									{
										found = true;
										rownum = rownum_cursor_row;
										goto exit_search_prev_bookmark;
									}
								}

								rownum_cursor_row -= 1;
							}
						}
						else
						{
							/* skip first x LineBuffers */
							while (rownum_cursor_row >= 1000 && lnb != NULL)
							{
								lnb = lnb->next;
								rownum_cursor_row -= 1000;
								rownum += 1000;
							}

							rownum += rownum_cursor_row;

							while (lnb != NULL)
							{
								if (lnb->lineinfo != NULL)
								{
									if (rownum_cursor_row < 0)
										rownum_cursor_row = lnb->nrows - 1;

									while (rownum_cursor_row >= 0)
									{
										if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
										{
											found = true;
											goto exit_search_prev_bookmark;
										}
										rownum -= 1;
										rownum_cursor_row -= 1;
									}
								}
								else
									rownum -= 1000;

								lnb = lnb->prev;
							}
						}
					}

exit_search_prev_bookmark:

					if (found)
					{
						cursor_row = rownum - CURSOR_ROW_OFFSET;
						if (cursor_row < first_row)
							first_row = cursor_row;
					}
					else
						make_beep(&opts);
				}
				break;

			case cmd_NextBookmark:
				{
					LineBuffer *lnb = &desc.rows;
					int		rownum_cursor_row;
					int		rownum = 0;
					bool	found = false;

					/* start after (next line) cursor line */
					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET + 1;

					if (desc.order_map)
					{
						while (rownum_cursor_row < desc.total_rows)
						{
							MappedLine *mp = &desc.order_map[rownum_cursor_row];

							if (mp->lnb->lineinfo)
							{
								if ((mp->lnb->lineinfo[mp->lnb_row].mask & LINEINFO_BOOKMARK) != 0)
								{
									found = true;
									rownum = rownum_cursor_row;
									goto exit_search_next_bookmark;
								}
							}

							rownum_cursor_row += 1;
						}
					}
					else
					{
						/* skip first x LineBuffers */
						while (rownum_cursor_row >= 1000 && lnb != NULL)
						{
							lnb = lnb->next;
							rownum_cursor_row -= 1000;
							rownum += 1000;
						}

						rownum += rownum_cursor_row;

						while (lnb != NULL)
						{
							if (lnb->lineinfo != NULL)
							{
								while (rownum_cursor_row < lnb->nrows)
								{
									if ((lnb->lineinfo[rownum_cursor_row].mask & LINEINFO_BOOKMARK) != 0)
									{
										found = true;
										goto exit_search_next_bookmark;
									}
									rownum += 1;
									rownum_cursor_row += 1;
								}
							}
							else
								rownum += 1000;

							rownum_cursor_row = 0;
							lnb = lnb->next;
						}
					}

exit_search_next_bookmark:

					if (found)
					{
						cursor_row = rownum - CURSOR_ROW_OFFSET;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

						first_row = adjust_first_row(first_row, &desc, &scrdesc);
					}
					else
						make_beep(&opts);
				}
				break;

			case cmd_ReleaseCols:
				opts.freezed_cols = 0;

show_first_col:

				if (after_freeze_signal && opts.vertical_cursor &&
						vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols))
					recheck_vertical_cursor_visibility = true;
				else
					cursor_col = 0;

				refresh_scr = true;
				break;

			case cmd_FreezeOneCol:
				opts.freezed_cols = 1;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeTwoCols:
				opts.freezed_cols = 2;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeThreeCols:
				opts.freezed_cols = 3;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeFourCols:
				opts.freezed_cols = 4;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeFiveCols:
				opts.freezed_cols = 5;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeSixCols:
				opts.freezed_cols = 6;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeSevenCols:
				opts.freezed_cols = 7;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeEightCols:
				opts.freezed_cols = 8;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_FreezeNineCols:
				opts.freezed_cols = 9;
				after_freeze_signal = true;
				goto show_first_col;

			case cmd_CursorFirstRow:
				cursor_row = 0;
				first_row = 0;
				break;

			case cmd_CursorLastRow:
				cursor_row = MAX_CURSOR_ROW;
				first_row = MAX_FIRST_ROW;
				if (first_row < 0)
					first_row = 0;
				break;

			case cmd_CursorUp:
				{
					if (opts.no_cursor)
					{
						next_command = cmd_ScrollUp;
						break;
					}

					if (cursor_row > 0)
					{
						/*
						 * When we are on data position, and we are going up, and a
						 * fixed rows are hidden, then unhide fixed rows first (by
						 * decreasing first_row)
						 */
						if (fix_rows_offset > 0 &&
								!is_footer_cursor(cursor_row, &scrdesc, &desc))
							first_row -= 1;
						else
							cursor_row -= 1;

						/*
						 * When fixed rows are hidden, then gap between first
						 * row and cursor row can be bigger (about fix_rows_offset.
						 */
						if (cursor_row + fix_rows_offset < first_row)
							first_row = cursor_row + fix_rows_offset;
					}
					else
						make_beep(&opts);

					break;
				}

			case cmd_CursorDown:
				{
					int		max_cursor_row;

					if (opts.no_cursor)
					{
						next_command = cmd_ScrollDown;
						break;
					}

					max_cursor_row = MAX_CURSOR_ROW;

					if (++cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						make_beep(&opts);
					}

					if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
						first_row += 1;

					first_row = adjust_first_row(first_row, &desc, &scrdesc);
				}
				break;

			case cmd_ScrollDownHalfPage:
				{
					int		offset = ((VISIBLE_DATA_ROWS - 1) >> 1);
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = MAX_FIRST_ROW;
					max_cursor_row = MAX_CURSOR_ROW;

					if (first_row + offset <= max_first_row)
					{
						first_row += offset;
						cursor_row += offset;
					}
					else if (cursor_row + offset <= max_cursor_row)
					{
						cursor_row += offset;
						first_row = max_first_row;
					}
					else
					{
						cursor_row = max_cursor_row;
						first_row = max_first_row;
					}
				}
				break;

			case cmd_ScrollUpHalfPage:
				{
					int		offset = ((VISIBLE_DATA_ROWS - 1) >> 1);

					if (first_row - offset > 0)
					{
						first_row -= offset;
						cursor_row -= offset;
					}
					else if (cursor_row - offset > 0)
					{
						first_row = 0;
						cursor_row -= offset;
					}
					else
					{
						first_row = 0;
						cursor_row = 0;
					}
				}
				break;

			case cmd_ScrollDown:
				{
					int		max_cursor_row;
					int		max_first_row;

					max_first_row = MAX_FIRST_ROW;
					max_cursor_row = MAX_CURSOR_ROW;

					if (first_row < max_first_row)
					{
						first_row += 1;
						cursor_row += 1;
					}
					else if (cursor_row < max_cursor_row)
					{
						cursor_row += 1;
					}
				}
				break;

			case cmd_ScrollUp:
				if (first_row > 0)
				{
					first_row -= 1;
					cursor_row -= 1;
				}
				else if (cursor_row > 0)
					cursor_row -= 1;
				break;

			case cmd_MoveLeft:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_left:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col > 0)
							footer_cursor_col -= 1;
						else if (scrdesc.rows_rows >= 0)
						{
							_is_footer_cursor = false;
							footer_cursor_col = 0;
							goto recheck_left;
						}
					}
					else
					{
						int		move_left = 30;

						if (cursor_col == 0 && scrdesc.footer_rows > 0 &&
							(!opts.vertical_cursor || (opts.vertical_cursor && vertical_cursor_column == 1)))
						{
							_is_footer_cursor = true;
							goto recheck_left;
						}

						if (desc.headline_transl != NULL)
						{
							if (opts.vertical_cursor && desc.columns > 0 && vertical_cursor_column > 0)
							{
								move_left = 0;

								if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols))
								{
									int		left_border = scrdesc.fix_cols_cols + cursor_col - 1;
									int		xmin = desc.cranges[vertical_cursor_column - 1].xmin;

									if (xmin < left_border)
									{
										move_left = left_border - xmin;

										if (move_left > 30)
											move_left = 30;
									}
									else
									{
										if (vertical_cursor_column > 1)
										{
											vertical_cursor_column -= 1;
											last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);

											xmin = desc.cranges[vertical_cursor_column - 1].xmin;

											if (xmin < left_border)
											{
												move_left = left_border - xmin;
												if (move_left > 30)
													move_left = 30;
											}
										}
									}
								}
								else
								{
									if (vertical_cursor_column > 1)
										vertical_cursor_column -= 1;

									cursor_col = 0;
									break;
								}
							}
							else
							{
								int		i;

								for (i = 1; i <= 30; i++)
								{
									int		pos = scrdesc.fix_cols_cols + cursor_col - i - 1;

									if (pos < 0)
										break;

									if (desc.headline_transl[pos] == 'I')
									{
										move_left = i;
										break;
									}
								}
							}
						}

						cursor_col -= move_left;
						if (cursor_col < 3)
							cursor_col = 0;
					}
				}
				break;

			case cmd_MoveRight:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_right:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						int max_footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;

						if (footer_cursor_col + 1 >= max_footer_cursor_col && scrdesc.rows_rows >= 0)
						{
							_is_footer_cursor = false;
							footer_cursor_col = max_footer_cursor_col;
							goto recheck_right;
						}
						else
							footer_cursor_col += 1;

						if (footer_cursor_col > max_footer_cursor_col)
							footer_cursor_col = max_footer_cursor_col;
					}
					else
					{
						int		move_right = 30;
						int		max_cursor_col;
						int		new_cursor_col = cursor_col;

						if (desc.headline_transl != NULL)
						{
							if (opts.vertical_cursor)
							{
								int vmaxx = desc.cranges[vertical_cursor_column - 1].xmax;

								/* move only right when right corner is not visible already */
								if (cursor_col + scrdesc.main_maxx < vmaxx)
								{
									int wx = vmaxx - scrdesc.main_maxx - cursor_col + 1;

									move_right = wx > 30 ? 30 : wx;
								}
								else
								{
									if (vertical_cursor_column < desc.columns)
									{
										vertical_cursor_column += 1;
										last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);

										vmaxx = desc.cranges[vertical_cursor_column - 1].xmax;
										if (cursor_col + scrdesc.main_maxx < vmaxx)
										{
											int wx = vmaxx - scrdesc.main_maxx - cursor_col + 1;

											move_right = wx > 30 ? 30 : wx;
										}
										else
											move_right = 0;
									}
								}
							}
							else
							{
								int		i;
								char   *str = &desc.headline_transl[scrdesc.fix_cols_cols + cursor_col];

								for (i = 1; i <= 30; i++)
								{
									if (str[i] == 'I')
									{
										move_right = i + 1;
										break;
									}
								}
							}
						}

						new_cursor_col += move_right;

						if (desc.headline_transl != NULL)
							max_cursor_col = desc.headline_char_size - scrdesc.main_maxx;
						else
							max_cursor_col = desc.maxx - scrdesc.maxx - 1;

						max_cursor_col = max_cursor_col > 0 ? max_cursor_col : 0;

						if (new_cursor_col > max_cursor_col)
							new_cursor_col = max_cursor_col;

						if (new_cursor_col == cursor_col && scrdesc.footer_rows > 0 &&
							(!opts.vertical_cursor || (opts.vertical_cursor && vertical_cursor_column == desc.columns)))
						{
							_is_footer_cursor = true;
							goto recheck_right;
						}
						cursor_col = new_cursor_col;

						/*
						 * When we go leave fixed columns, then first unfixed column should
						 * be visible.
						 */
						if (desc.headline_transl && opts.vertical_cursor)
						{
							int fixed_columns = opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols;

							if (vertical_cursor_column - 1 == fixed_columns)
								cursor_col = 0;
						}
					}
				}
				break;

			case cmd_CursorFirstRowPage:
				cursor_row = first_row;
				break;

			case cmd_CursorLastRowPage:
				cursor_row = first_row + VISIBLE_DATA_ROWS - 1;
				break;

			case cmd_CursorHalfPage:
				cursor_row = first_row + ((VISIBLE_DATA_ROWS - 1) >> 1);
				break;

			case cmd_PageUp:
				{
					int		offset;

					if (desc.is_expanded_mode &&
							scrdesc.first_rec_title_y != -1 && scrdesc.last_rec_title_y != -1)
						offset = scrdesc.last_rec_title_y - scrdesc.first_rec_title_y;
					else
						offset = scrdesc.main_maxy - scrdesc.fix_rows_rows;

					if (first_row > 0)
					{
						first_row -= offset;
						if (first_row < 0)
							first_row = 0;
					}
					if (cursor_row > 0)
					{
						cursor_row -= offset;
						if (cursor_row < 0)
							cursor_row = 0;
					}
						else
						make_beep(&opts);
				}
				break;

			case cmd_PageDown:
				{
					int		max_cursor_row;
					int		offset;

					if (desc.is_expanded_mode &&
							scrdesc.first_rec_title_y != -1 && scrdesc.last_rec_title_y != -1)
						offset = scrdesc.last_rec_title_y - scrdesc.first_rec_title_y;
					else
						offset = scrdesc.main_maxy - scrdesc.fix_rows_rows;

					first_row += offset;
					cursor_row += offset;

					max_cursor_row = MAX_CURSOR_ROW;
					if (cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						make_beep(&opts);
					}

					if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
						first_row += 1;

					first_row = adjust_first_row(first_row, &desc, &scrdesc);
				}
				break;

			case cmd_RESIZE_EVENT:
				refresh_scr = true;
				resize_scr = true;
				break;

			case cmd_ShowFirstCol:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_home:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col > 0)
							footer_cursor_col = 0;
						else if (scrdesc.rows_rows > 0)
						{
							footer_cursor_col = 0;
							_is_footer_cursor = false;
							goto recheck_home;
						}
					}
					else
					{
						if (opts.vertical_cursor && desc.columns > 0)
						{
							vertical_cursor_column = 1;
							last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						}

						if (cursor_col > 0)
							cursor_col = 0;
						else if (scrdesc.footer_rows > 0)
						{
							cursor_col = 0;
							_is_footer_cursor = true;

							goto recheck_home;
						}
					}
					break;
				}

			case cmd_ShowLastCol:
				{
					bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);
					int		recheck_count = 0;

recheck_end:

					if (++recheck_count > 2)
						break;

					if (_is_footer_cursor)
					{
						if (footer_cursor_col < desc.footer_char_size - scrdesc.main_maxx)
							footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;
						else if (scrdesc.rows_rows > 0)
						{
							footer_cursor_col = desc.footer_char_size - scrdesc.main_maxx;
							_is_footer_cursor = false;
							goto recheck_end;
						}
					}
					else
					{
						int		new_cursor_col;

						if (opts.vertical_cursor && desc.columns > 0)
						{
							vertical_cursor_column = desc.columns;
							last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						}

						if (desc.headline != NULL)
							new_cursor_col = desc.headline_char_size - scrdesc.main_maxx;
						else
							new_cursor_col = desc.maxx - maxx - 1;

						new_cursor_col = new_cursor_col > 0 ? new_cursor_col : 0;
						if (new_cursor_col > cursor_col)
							cursor_col = new_cursor_col;
						else if (scrdesc.footer_rows > 0)
						{
							_is_footer_cursor = true;
							cursor_col = new_cursor_col;
							goto recheck_end;
						}
					}
					break;
				}

			case cmd_GotoLine:
				{
					char	linenotxt[256];

					get_string(&opts, &scrdesc, "line: ", linenotxt, sizeof(linenotxt) - 1, last_line);
					if (linenotxt[0] != '\0')
					{
						char   *endptr;
						long lineno;

						errno = 0;
						lineno = strtol(linenotxt, &endptr, 10);

						if (endptr == linenotxt)
							show_info_wait(&opts, &scrdesc, " Cannot convert input string to number", NULL, true, true, false, true);
						else if (errno != 0)
							show_info_wait(&opts, &scrdesc, " Cannot convert input string to number (%s)", strerror(errno), true, true, false, true);
						else
						{
							int max_cursor_row;

							cursor_row = lineno - 1;
							if (cursor_row < 0)
								cursor_row = 0;

							max_cursor_row = MAX_CURSOR_ROW;
		  					if (cursor_row > max_cursor_row)
							{
								cursor_row = max_cursor_row;
								make_beep(&opts);
							}

							if (cursor_row < first_row || cursor_row - first_row > VISIBLE_DATA_ROWS)
							{
								first_row = cursor_row - VISIBLE_DATA_ROWS / 2;
								first_row = adjust_first_row(first_row, &desc, &scrdesc);
							}

							snprintf(last_line, sizeof(last_line), "%ld", lineno);
						}
					}
					break;
				}

			case cmd_OriginalSort:
				if (desc.order_map)
				{
					free(desc.order_map);
					desc.order_map = NULL;

					last_ordered_column = -1;
				}

				/*
				 * We cannot to say nothing about found_row, so most
				 * correct solution is clean it now.
				 */
				scrdesc.found_row = -1;

				break;

			case cmd_SortAsc:
			case cmd_SortDesc:
				{
					if (opts.vertical_cursor && vertical_cursor_column > 0 && desc.columns > 0)
					{
						update_order_map(&opts,
										 &scrdesc,
										 &desc,
										 vertical_cursor_column,
										 command == cmd_SortDesc);

						last_ordered_column = vertical_cursor_column;
						last_order_desc = command == cmd_SortDesc;
					}
					else if (desc.columns == 0)
						show_info_wait(&opts, &scrdesc, " Sort is available only for tables.", NULL, true, true, true, false);
					else 
						show_info_wait(&opts, &scrdesc, " Vertical cursor is not visible", NULL, true, true, true, false);

					break;
				}

			case cmd_SaveData:
				{
					char	buffer[MAXPATHLEN + 1024];
					char   *path;
					FILE   *fp;
					bool	ok = false;

					errno = 0;

					get_string(&opts, &scrdesc, "log file: ", buffer, sizeof(buffer) - 1, last_path);

					if (buffer[0] != '\0')
					{
						strncpy(last_path, buffer, sizeof(last_path) - 1);
						last_path[sizeof(last_path) - 1] = '\0';

						path = tilde(buffer);
						fp = fopen(path, "w");
						if (fp != NULL)
						{
							LineBuffer *lnb = &desc.rows;

							ok = true;

							while (lnb)
							{
								for (i = 0; i < lnb->nrows; i++)
								{
									/*
									 * Reset errno. Previous openf can dirty it, when file was
									 * created.
									 */
									errno = 0;

									fprintf(fp, "%s\n", lnb->rows[i]);
									if (errno != 0)
									{
										ok = false;
										goto exit;
									}
								}
								lnb = lnb->next;
							}

							fclose(fp);
						}

exit:

						if (!ok)
						{
							if (errno != 0)
								snprintf(buffer, sizeof(buffer), "%s (%s)", path, strerror(errno));
							else
								strcpy(buffer, path);

							next_event_keycode = show_info_wait(&opts, &scrdesc, " Cannot write to %s (press any key)", buffer, true, false, false, true);
						}
					}

					refresh_scr = true;

					break;
				}

			case cmd_ForwardSearch:
				{
					char	locsearchterm[256];

					get_string(&opts, &scrdesc, "/", locsearchterm, sizeof(locsearchterm) - 1, last_row_search);
					if (locsearchterm[0] != '\0')
					{
						memcpy(last_row_search, locsearchterm, sizeof(last_row_search));

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = opts.force8bit ? strlen(scrdesc.searchterm) : utf8len(scrdesc.searchterm);
					}
					else
					{
						scrdesc.searchterm[0] = '\0';
						scrdesc.searchterm_size = 0;
						scrdesc.searchterm_char_size = 0;
					}

					reset_searching_lineinfo(&desc.rows);

					search_direction = SEARCH_FORWARD;

					/* continue to find next: */
				}

			case cmd_SearchNext:
				{
					int		rownum_cursor_row;
					int		rownum = 0;
					int		skip_bytes = 0;
					LineBuffer   *lnb = &desc.rows;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchNext && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchPrev;
						break;
					}

					rownum_cursor_row = cursor_row + CURSOR_ROW_OFFSET;
					if (scrdesc.found && rownum_cursor_row == scrdesc.found_row)
						skip_bytes = scrdesc.found_start_bytes + scrdesc.searchterm_size;

					scrdesc.found = false;

					if (desc.order_map)
					{
						rownum = rownum_cursor_row;

						while (rownum < desc.total_rows)
						{
							MappedLine *mp = &desc.order_map[rownum];
							const char	   *str;
							const char	   *rowstr;

							rowstr = mp->lnb->rows[mp->lnb_row];
							str = pspg_search(&opts, &scrdesc, rowstr + skip_bytes);
							if (str != NULL)
							{
								scrdesc.found_start_x = opts.force8bit ? (size_t) (str - rowstr) : utf8len_start_stop(rowstr, str);
								scrdesc.found_start_bytes = str - rowstr;
								scrdesc.found = true;
								goto found_next_pattern;
							}

							rownum += 1;
							skip_bytes = 0;
						}
					}
					else
					{
						/* skip first x LineBuffers */
						while (rownum_cursor_row > 1000 && lnb != NULL)
						{
							lnb = lnb->next;
							rownum_cursor_row -= 1000;
							rownum += 1000;
						}

						rownum += rownum_cursor_row;

						while (lnb != NULL)
						{
							while (rownum_cursor_row < lnb->nrows)
							{
								const char	   *str;
								const char	   *rowstr = lnb->rows[rownum_cursor_row];

								str = pspg_search(&opts, &scrdesc, rowstr + skip_bytes);

								if (str != NULL)
								{
									scrdesc.found_start_x = opts.force8bit ? (size_t) (str - rowstr) : utf8len_start_stop(rowstr, str);
									scrdesc.found_start_bytes = str - rowstr;
									scrdesc.found = true;
									goto found_next_pattern;
								}

								rownum += 1;
								rownum_cursor_row += 1;
								skip_bytes = 0;
							}

							rownum_cursor_row = 0;
							lnb = lnb->next;
						}
					}

found_next_pattern:

					if (scrdesc.found)
					{
						cursor_row = rownum - CURSOR_ROW_OFFSET;
						scrdesc.found_row = rownum;
						fresh_found = true;
						fresh_found_cursor_col = -1;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

						first_row = adjust_first_row(first_row, &desc, &scrdesc);
					}
					else
						show_info_wait(&opts, &scrdesc, " Not found (press any key)", NULL, true, true, false, false);
					break;
				}

			case cmd_BackwardSearch:
				{
					char	locsearchterm[256];

					get_string(&opts, &scrdesc, "?", locsearchterm, sizeof(locsearchterm) - 1, last_row_search);
					if (locsearchterm[0] != '\0')
					{
						memcpy(last_row_search, locsearchterm, sizeof(last_row_search));

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(&opts, scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = utf8len(scrdesc.searchterm);
					}
					else
					{
						scrdesc.searchterm[0] = '\0';
						scrdesc.searchterm_size = 0;
						scrdesc.searchterm_char_size = 0;
					}

					reset_searching_lineinfo(&desc.rows);

					search_direction = SEARCH_BACKWARD;

					/* continue to find next: */
				}

			case cmd_SearchPrev:
				{
					int		rowidx;
					int		search_row;
					LineBuffer   *rows = &desc.rows;
					int		cut_bytes = 0;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchPrev && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchNext;
						break;
					}

					rowidx = cursor_row + scrdesc.fix_rows_rows + desc.title_rows;
					search_row = cursor_row;

					/*
					 * when we can search on found line, the use it,
					 * else try start searching from previous row.
					 */
					if (scrdesc.found && rowidx == scrdesc.found_row && scrdesc.found_start_bytes > 0)
						cut_bytes = scrdesc.found_start_bytes;
					else
					{
						rowidx -= 1;
						search_row -= 1;
					}

					scrdesc.found = false;

					if (desc.order_map)
					{
						if (search_row > -1)
						{
							MappedLine *mp = &desc.order_map[rowidx];

							rows = mp->lnb;
							rowidx = mp->lnb_row;
						}
					}
					else
					{
						/* go forward */
						while (rowidx > 1000)
						{
							rows = rows->next;
							rowidx -= 1000;
						}
					}

					while (search_row >= 0)
					{
						const char *str;
						char *row;
						bool	free_row;

						if (cut_bytes != 0)
						{
							row = malloc(strlen(rows->rows[rowidx]) + 1);
							if (row == NULL)
								leave_ncurses("out of memory");

							strcpy(row, rows->rows[rowidx]);
							row[cut_bytes] = '\0';
							free_row = true;
						}
						else
						{
							row = rows->rows[rowidx];
							free_row = false;
						}

						str = row;

						/* try to find most right pattern */
						while (str != NULL)
						{
							str = pspg_search(&opts, &scrdesc, str);

							if (str != NULL)
							{
								cursor_row = search_row;
								if (first_row > cursor_row)
									first_row = cursor_row;

								scrdesc.found_start_x = opts.force8bit ? (size_t) (str - row) : utf8len_start_stop(row, str);
								scrdesc.found_start_bytes = str - row;
								scrdesc.found_row = cursor_row + CURSOR_ROW_OFFSET;
								scrdesc.found = true;
								fresh_found = true;
								fresh_found_cursor_col = -1;

								str += scrdesc.searchterm_size;
							}
						}

						if (free_row)
							free(row);

						if (scrdesc.found)
							break;

						search_row -= 1;
						cut_bytes = 0;

						if (desc.order_map)
						{
							MappedLine *mp = &desc.order_map[search_row + scrdesc.fix_rows_rows + desc.title_rows];

							rows = mp->lnb;
							rowidx = mp->lnb_row;
						}
						else
						{
							rowidx -= 1;

							if (rowidx < 0)
							{
								rows = rows->prev;
								rowidx = 999;
							}
						}
					}

					if (!scrdesc.found)
						show_info_wait(&opts, &scrdesc, " Not found (press any key)", NULL, true, true, false, false);

					break;
				}

			case cmd_SearchColumn:
				{
					if (desc.namesline)
					{
						char	locsearchterm[256];
						int		startcolumn = 1;
						int		colnum;
						bool	found = false;
						bool	search_from_start = false;

						get_string(&opts, &scrdesc, "c:", locsearchterm, sizeof(locsearchterm) - 1, last_col_search);

						if (locsearchterm[0] != '\0')
						{
							memcpy(last_col_search, locsearchterm, sizeof(last_col_search));

							strncpy(scrdesc.searchcolterm, locsearchterm, sizeof(scrdesc.searchcolterm));
							scrdesc.searchcolterm_size = strlen(scrdesc.searchcolterm);
						}

						if (scrdesc.searchcolterm[0] != '\0')
						{
							/*
							 * Where we should to start searching?
							 * 1. after visible vertical cursor
							 * 2. after cursor_col
							 * 3. from first column
							 */
							if (opts.vertical_cursor)
								startcolumn = vertical_cursor_column + 1;
							else if (cursor_col > 0)
							{
								int		first_x = scrdesc.fix_cols_cols + cursor_col;
								int		i;

								/* fallback */
								startcolumn = 1;

								for (i = 0; i < desc.columns; i++)
								{
									if (desc.cranges[i].xmin <= first_x &&
										first_x < desc.cranges[i].xmax)
									{
										startcolumn = i + 1;
										break;
									}
								}
							}
							else
								startcolumn = 1;

							for (colnum = startcolumn; colnum <= desc.columns; colnum++)
							{
								if (opts.force8bit)
								{
									if (nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_pos,
														   desc.cranges[colnum - 1].name_size,
														   scrdesc.searchcolterm,
														   scrdesc.searchcolterm_size))
									{
										found = true;
										break;
									}
								}
								else
								{
									if (utf8_nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_pos,
														   desc.cranges[colnum - 1].name_size,
														   scrdesc.searchcolterm,
														   scrdesc.searchcolterm_size))
									{
										found = true;
										break;
									}
								}
							}

							if (!found)
							{
								search_from_start = true;

								for (colnum = 1; colnum < startcolumn; colnum++)
								{
									if (nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_pos,
														   desc.cranges[colnum - 1].name_size,
														   scrdesc.searchcolterm,
														   scrdesc.searchcolterm_size))
									{
										found = true;
										break;
									}
								}
							}

							if (found)
							{
								if (search_from_start)
									show_info_wait(&opts, &scrdesc, " Search from first column (press any key)", NULL, true, true, true, false);

								opts.vertical_cursor = true;
								vertical_cursor_column = colnum;

								cursor_col = get_cursor_col_for_vertical_column(vertical_cursor_column, cursor_col, &desc, &scrdesc);
								last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
							}
							else
								show_info_wait(&opts, &scrdesc, " Not found (press any key)", NULL, true, true, false, false);
						}
						else
							show_info_wait(&opts, &scrdesc, " Search pattern is a empty string (press any key)", NULL, true, true, true, false);
					}
					else
						show_info_wait(&opts, &scrdesc, " Columns names are not detected (press any key)", NULL, true, true, true, false);

					break;
				}

			case cmd_TogglePause:
				paused = !paused;
				break;

			case cmd_Refresh:
				force_refresh = true;
				goto force_refresh_data;
				break;

			case cmd_MOUSE_EVENT:
				{
					mouse_event += 1;

#if NCURSES_MOUSE_VERSION > 1

					if (event.bstate & BUTTON_ALT && event.bstate & BUTTON5_PRESSED)
					{
						next_command = cmd_MoveRight;
						break;
					}

					if (event.bstate & BUTTON_ALT && event.bstate & BUTTON4_PRESSED)
					{
						next_command = cmd_MoveLeft;
						break;
					}

					if (event.bstate & BUTTON5_PRESSED)
					{
						int		max_cursor_row;
						int		offset = 1;
						int		max_first_row = MAX_FIRST_ROW;

						if (max_first_row < 0)
							max_first_row = 0;

						if (desc.headline_transl != NULL)
							offset = (scrdesc.main_maxy - scrdesc.fix_rows_rows) / 3;

						if (first_row + offset > max_first_row)
							offset = 1;

						first_row += offset;
						cursor_row += offset;

						max_cursor_row = MAX_CURSOR_ROW;
						if (cursor_row > max_cursor_row)
						{
							cursor_row = max_cursor_row;
							make_beep(&opts);
						}

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row += 1;

						first_row = first_row > max_first_row ? max_first_row : first_row;
					}
					else if (event.bstate & BUTTON4_PRESSED)
					{
						int		offset = 1;

						if (desc.headline_transl != NULL)
							offset = (scrdesc.main_maxy - scrdesc.fix_rows_rows) / 3;

						if (first_row <= offset)
							offset = 1;

						if (first_row > 0)
						{
							first_row -= offset;
							if (first_row < 0)
								first_row = 0;
						}
						if (cursor_row > 0)
						{
							cursor_row -= offset;
							if (cursor_row < 0)
								cursor_row = 0;
						}
						else
							make_beep(&opts);
					}
					else

#endif

					if (event.bstate & BUTTON1_PRESSED || event.bstate & BUTTON1_RELEASED)
					{
						int		max_cursor_row;
						bool	is_double_click = false;
						bool	_is_footer_cursor;
						long	ms;
						time_t	sec;

						if (event.y == 0 && scrdesc.top_bar_rows > 0)
						{
							next_command = cmd_ShowMenu;
							reuse_event = true;
							prev_event_keycode = 0;
							break;
						}

						if (event.bstate & BUTTON1_RELEASED)
						{
							current_time(&sec, &ms);

							if (last_sec > 0)
							{
								long	td;

								td = time_diff(sec, ms, last_sec, last_ms);
								if (td < 250)
									is_double_click = true;
							}

							last_sec = sec;
							last_ms = ms;
						}

						/*
						 * When current event is MOUSE RELEASE, and prev event was MOUSE_PRESS
						 * and it is not double click, and there are same position, then we can
						 * ignore this event.
						 */
						if (prev_event_is_mouse_press && !is_double_click &&
								prev_mouse_event_y == event.y && prev_mouse_event_x == event.x)
						{
							no_doupdate = true;
							continue;
						}

						if (event.y >= scrdesc.top_bar_rows && event.y <= scrdesc.fix_rows_rows)
						{
							if (is_double_click)
							{
								/*
								 * protection against unwanted vertical cursor hide,
								 * when cursor was changed by first click of current double click.
								 */
								if (mouse_event - vertical_cursor_changed_mouse_event > 3)
								{
									next_command = cmd_ShowVerticalCursor;
									continue;
								}
							}
						}
						else
							cursor_row = event.y - scrdesc.fix_rows_rows - scrdesc.top_bar_rows + first_row - fix_rows_offset;

						if (cursor_row < 0)
							cursor_row = 0;

						if (cursor_row + fix_rows_offset < first_row)
							first_row = cursor_row + fix_rows_offset;

						max_cursor_row = MAX_CURSOR_ROW;
						if (cursor_row > max_cursor_row)
							cursor_row = max_cursor_row;

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row += 1;

						first_row = adjust_first_row(first_row, &desc, &scrdesc);

						_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);

						/*
						 * Save last x focused point. It will be used for repeated hide/unhide
						 * vertical cursor. But only if cursor is not in footer area.
						 */
						if (!_is_footer_cursor)
							last_x_focus = event.x;

						if (event.bstate & BUTTON_ALT && is_double_click)
						{
							next_command = cmd_ToggleBookmark;
						}
						else if (!(event.bstate & BUTTON_ALT) && opts.vertical_cursor && !_is_footer_cursor)
						{
							int		xpoint = event.x - scrdesc.main_start_x;
							int		vertical_cursor_column_orig = vertical_cursor_column;
							int		i;

							if (xpoint > scrdesc.fix_cols_cols - 1)
								xpoint += cursor_col;

							if (xpoint >= 0)
							{
								for (i = 0; i  < desc.columns; i++)
								{
									if (desc.cranges[i].xmin <= xpoint && desc.cranges[i].xmax > xpoint)
									{
										int		xmin = desc.cranges[i].xmin;
										int		xmax = desc.cranges[i].xmax;

										vertical_cursor_column = i + 1;

										if (vertical_cursor_column != vertical_cursor_column_orig &&
											event.y >= scrdesc.top_bar_rows && event.y <= scrdesc.fix_rows_rows) 
										{
											vertical_cursor_changed_mouse_event = mouse_event;
										}

										if (vertical_cursor_column > (opts.freezed_cols > -1 ? opts.freezed_cols : default_freezed_cols))
										{
											if (xmax > scrdesc.main_maxx + cursor_col)
											{
												cursor_col = xmax - scrdesc.main_maxx;
											}
											else if (xmin < scrdesc.fix_cols_cols + cursor_col)
											{
												cursor_col = xmin - scrdesc.fix_cols_cols + 1;
											}
										}

										last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
										break;
									}
								}
							}
						}
					}
					break;
				}

		} /* end switch */

		if (fresh_found && scrdesc.found)
		{
			int		maxy, maxx;
			bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);

			UNUSED(maxy);

			if (opts.vertical_cursor && !_is_footer_cursor)
			{
				int		i;

				for (i = 0; i < desc.columns; i++)
				{
					if (desc.cranges[i].xmin <= scrdesc.found_start_x &&
						scrdesc.found_start_x < desc.cranges[i].xmax)
					{
						vertical_cursor_column = i + 1;
						last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
						break;
					}
				}
			}

			if (fresh_found && w_fix_cols(&scrdesc) != NULL)
			{
				getmaxyx(w_fix_cols(&scrdesc), maxy, maxx);

				if (scrdesc.found_start_x + scrdesc.searchterm_char_size <= maxx)
					fresh_found = false;
			}

			if (fresh_found && !_is_footer_cursor &&  w_rows(&scrdesc) != NULL)
			{
				getmaxyx(w_rows(&scrdesc), maxy, maxx);

				if (cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						cursor_col + scrdesc.fix_cols_cols + maxx >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (cursor_col + scrdesc.fix_cols_cols > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (cursor_col + scrdesc.fix_cols_cols + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						next_command = cmd_MoveRight;
				}
			}

			if (fresh_found  && _is_footer_cursor && w_footer(&scrdesc) != NULL)
			{
				getmaxyx(w_footer(&scrdesc), maxy, maxx);

				if (footer_cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						footer_cursor_col + maxx >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (footer_cursor_col > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (footer_cursor_col + maxx < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						next_command = cmd_MoveRight;
				}
			}

			if (next_command != 0)
			{
				/* protect agains infinity loop */
				if (fresh_found_cursor_col != -1)
				{
					/* the direction should not be changed */
					if (_is_footer_cursor)
					{
						if ((fresh_found_cursor_col > footer_cursor_col && next_command == cmd_MoveRight) ||
							(fresh_found_cursor_col < footer_cursor_col && next_command == cmd_MoveLeft) ||
							(fresh_found_cursor_col == footer_cursor_col))
							{
								next_command = cmd_Invalid;
								fresh_found = false;
							}
						}
					else
					{
						if ((fresh_found_cursor_col > cursor_col && next_command == cmd_MoveRight) ||
							(fresh_found_cursor_col < cursor_col && next_command == cmd_MoveLeft) ||
							(fresh_found_cursor_col == cursor_col))
						{
							next_command = cmd_Invalid;
							fresh_found = false;
						}
					}
				}
				else
					fresh_found_cursor_col = _is_footer_cursor? footer_cursor_col : cursor_col;
			}
			else
				fresh_found = false;
		}

		print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset, vertical_cursor_column);

		if (first_row != prev_first_row)
		{
			/* now, maybe more/less rows from footer should be displayed */
			if (desc.headline_transl != NULL && desc.footer_row > 0)
			{
				int		rows_rows;

				rows_rows = min_int(desc.footer_row - scrdesc.fix_rows_rows - first_row - desc.title_rows,
									 scrdesc.main_maxy - scrdesc.fix_rows_rows);
				rows_rows = rows_rows > 0 ? rows_rows : 0;

				if (!refresh_scr)
				{
					refresh_scr = scrdesc.rows_rows != rows_rows;
				}
			}
		}

		if (refresh_scr || scrdesc.refresh_scr)
		{
			if (resize_scr)
			{
				/*
				 * Workaround - the variables COLS, LINES are not refreshed
				 * when pager is resized and executed inside psql.
				 */
				if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
				{
					resize_term(size.ws_row, size.ws_col);
					clear();
				}

				resize_scr = false;
			}

refresh:

			getmaxyx(stdscr, maxy, maxx);

			refresh_aux_windows(&opts, &scrdesc);
			create_layout_dimensions(&opts, &scrdesc, &desc, opts.freezed_cols != -1 ? opts.freezed_cols : default_freezed_cols, fixedRows, maxy, maxx);
			create_layout(&opts, &scrdesc, &desc, first_data_row, first_row);

			/* recheck visibility of vertical cursor. now we have fresh fix_cols_cols data */
			if (recheck_vertical_cursor_visibility && vertical_cursor_column > 0)
			{
				int		vminx = desc.cranges[vertical_cursor_column - 1].xmin;
				int		left_border = scrdesc.fix_cols_cols + cursor_col - 1;

				if (vminx < left_border)
					cursor_col = vminx -  scrdesc.fix_cols_cols + 1;
			}

			print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset, vertical_cursor_column);

			if (cmdbar)
				cmdbar = init_cmdbar(cmdbar, &opts);

			refresh_scr = false;
			scrdesc.refresh_scr = false;
		}
	}

	for (i = 0; i < PSPG_WINDOW_COUNT; i++)
	{
		if (scrdesc.wins[i])
			delwin(scrdesc.wins[i]);
	}

	if (cmdbar)
		st_cmdbar_free(cmdbar);
	if (menu)
		st_menu_free(menu);

	endwin();

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "ncurses ended\n");
	}

	active_ncurses = false;

	if (raw_output_quit)
	{
		LineBuffer *lnb = &desc.rows;

		while (lnb)
		{
			int			lnb_row = 0;

			while (lnb_row < lnb->nrows)
				printf("%s\n", lnb->rows[lnb_row++]);
			lnb = lnb->next;
		}
	}
	else if (no_alternate_screen)
	{
		draw_data(&opts, &scrdesc, &desc, first_data_row, first_row, cursor_col,
				  footer_cursor_col, fix_rows_offset);
	}

#ifdef HAVE_READLINE_HISTORY

	write_history(tilde("~/.pspg_history"));

#endif

	if (inotify_fd >= 0)
		close(inotify_fd);

#ifdef DEBUG_PIPE

	/*
	 * Try to release all allocated memory in debug mode, for better
	 * memory leak detection.
	 */
	if (0)
	{
		LineBuffer *lnb = &desc.rows;
		LineBuffer *_lnb;
		int			lnb_row;

		while (lnb)
		{
			for (lnb_row = 0; lnb_row < lnb->nrows; lnb_row++)
			{
				if (lnb->rows[lnb_row])
					free(lnb->rows[lnb_row]);
			}

			if (lnb->lineinfo)
				free(lnb->lineinfo);

			_lnb = lnb;
			lnb = lnb->next;
			if (_lnb != &desc.rows)
				free(_lnb);
		}

		if (desc.cranges)
			free(desc.cranges);

		if (desc.headline_transl)
			free(desc.headline_transl);

		if (opts.pathname)
			free(opts.pathname);
	}

	fclose(debug_pipe);

#endif

	/* close file in streaming mode */
	if (fp)
		fclose(fp);

	if (logfile)
	{
		print_log_prefix(logfile);
		fprintf(logfile, "correct quit\n");
		fclose(logfile);
	}

	return 0;
}
