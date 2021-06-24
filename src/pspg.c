/*-------------------------------------------------------------------------
 *
 * pspg.c
 *	  a terminal pager designed for usage from psql
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
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
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <langinfo.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef GWINSZ_IN_SYS_IOCTL
#include <termios.h>
#endif

#include <time.h>
#include <unistd.h>

#ifdef HAVE_INOTIFY

#include <sys/inotify.h>

#endif

#include "commands.h"
#include "config.h"
#include "pspg.h"
#include "themes.h"
#include "unicode.h"

#ifdef COMPILE_MENU

#include "st_menu.h"

#endif

#ifdef DEBUG_PIPE

/* used for mallinfo */
#include <malloc.h>

#endif


static char		last_row_search[256];
static char		last_col_search[256];
static char		last_line[256];
static char		last_path[1025];
static char		last_rows_number[256];
static char		last_table_name[256];
static char		last_nullstr[256];
static char		cmdline[1024];
static const char *cmdline_ptr = 0;

int		clipboard_application_id = 0;

#define		USE_EXTENDED_NAMES

#ifdef DEBUG_PIPE

FILE *debug_pipe = NULL;
int	debug_eventno = 0;

#endif

static bool	got_sigint = false;

static long	last_watch_ms = 0;
static time_t	last_watch_sec = 0;					/* time when we did last refresh */
static bool	paused = false;							/* true, when watch mode is paused */

static bool active_ncurses = false;
static bool xterm_mouse_mode_was_initialized = false;

static int number_width(int num);
static int get_event(MEVENT *mevent, bool *alt, bool *sigint, bool *timeout, bool *notify, bool *reopen, int timeoutval, int hold_stream);
static void unget_event(int key_code, bool alt, MEVENT *mevent);
static void set_scrollbar(ScrDesc *scrdesc, DataDesc *desc, int first_row);
static bool check_visible_vertical_cursor(DataDesc *desc, Options *opts, int vertical_cursor_column);

StateData *current_state = NULL;

static struct sigaction old_sigsegv_handler;

/*
 * Global error buffer - used for building and storing a error messages
 */
char pspg_errstr_buffer[PSPG_ERRSTR_BUFFER_SIZE];

/*
 * Buffer of events. This buffer is used for two purposes:
 *
 * a) group processing of mouse events - usually pspg redraw
 *    output after processing of any input event, but mouse
 *    mouse events can be generated too frequently, and too
 *    frequent redraw can make latences on slower terminals or
 *    can make flickering on fast terminals (the image is repainted
 *    faster than is refresh rate of display).
 *
 * b) saving event that hides information text
 */
typedef struct
{
	int		key_code;
	bool	alt;
	MEVENT	mevent;
} PspgEvent;

#define		PSPG_EVENT_BUFFER_SIZE			20

static PspgEvent	pspg_event_buffer[PSPG_EVENT_BUFFER_SIZE];

static int pspg_event_buffer_read = 0;
static int pspg_event_buffer_write = 0;

/*
 * Global setting
 */
bool	use_utf8;
bool	quiet_mode = false;

/*
 * Global state variables
 */
bool	handle_sigint = false;
WINDOW *prompt_window = NULL;
attr_t  prompt_window_input_attr = 0;
attr_t  prompt_window_error_attr = 0;
attr_t  prompt_window_info_attr = 0;

/*
 * Own signal handlers
 */
static void
SigintHandler(int sig_num)
{
	UNUSED(sig_num);

	signal(SIGINT, SigintHandler);

	handle_sigint = true;
}

static void
SigtermHandler(int sig_num)
{
	UNUSED(sig_num);

	signal(SIGTERM, SigtermHandler);

	/* force own exit_ncurses routine */
	exit(EXIT_FAILURE);
}

/* Custom SIGSEGV handler. */
void
SigsegvHandler (int sig)
{
	exit_ncurses();

	log_row("pspg crashed by Sig %d\n", sig);

	fprintf(stderr, "pspg crashed by signal %d\n", sig);

	if (logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}

	/* Call old sigsegv handler; may be default exit or third party one (e.g. ASAN) */
	sigaction (SIGSEGV, &old_sigsegv_handler, NULL);
}

/* Set up sigsegv handler. */
static void
setup_sigsegv_handler(void)
{
	struct sigaction act;

	sigemptyset (&act.sa_mask);
	act.sa_flags = (int) SA_RESETHAND;
	act.sa_handler = SigsegvHandler;

	sigaction (SIGSEGV, &act, &old_sigsegv_handler);
}

inline int
min_int(int a, int b)
{
	return a < b ? a : b;
}

/*
 * The argument "b" should be >= "a" or it should be ignored
 * and "a" is used instead "b".
 */
int
trim_to_range(int v, int a, int b)
{
	/*
	 * if second parameter is below a, result should be a,
	 * because the interval is [a, a].
	 */
	if (b <= a)
		return a;

	if (v <= a)
		return a;

	if (v > b)
		return b;

	return v;
}

static void
current_time(time_t *sec, long *ms)
{
	struct timespec spec;

	clock_gettime(CLOCK_MONOTONIC, &spec);
	*ms = roundl(spec.tv_nsec / 1.0e6);
	*sec = spec.tv_sec;
}

inline void
enable_xterm_mouse_mode(bool enable_mode)
{
	if (enable_mode)
	{
		fprintf(stdout, "\033[?1002h");
		fflush(stdout);
		xterm_mouse_mode_was_initialized = true;
		log_row("xterm mouse mode 1002 activated");
	}
	else
	{
		xterm_mouse_mode_was_initialized = false;
		log_row("xterm mouse mode 1002 is not activated");
	}
}

inline bool
disable_xterm_mouse_mode(void)
{
	if (xterm_mouse_mode_was_initialized)
	{
		fprintf(stdout, "\033[?1002l");
		fflush(stdout);
		xterm_mouse_mode_was_initialized = false;
		log_row("xterm mouse mode 1002 is deactivated");
		return true;
	}
	else
	{
		log_row("xterm mouse mode 1002 is not active");
		return false;
	}
}

/*
 * ncurses in RHEL 7 doesn't support wgetdelay function  This function
 * was introduced in ncurses 6.0. See build issue #174.
 *
 */
static inline int
_wgetdelay(WINDOW *w)
{

#if NCURSES_VERSION_MAJOR >= 6

	return wgetdelay(w);

#else

	return 1000;

#endif

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
 * Multiple used block - searching in string based on configuration
 */
const char *
pspg_search(Options *opts, ScrDesc *scrdesc, const char *str)
{
	bool	ignore_case = opts->ignore_case;
	bool	ignore_lower_case = opts->ignore_lower_case;
	bool	has_upperchr = scrdesc->has_upperchr;
	const char *searchterm = scrdesc->searchterm;
	const char *result;

	if (ignore_case || (ignore_lower_case && !has_upperchr))
	{
		result = use_utf8 ? utf8_nstrstr(str, searchterm) : nstrstr(str, searchterm);
	}
	else if (ignore_lower_case && has_upperchr)
	{
		result = use_utf8 ?
						utf8_nstrstr_ignore_lower_case(str, searchterm) :
						nstrstr_ignore_lower_case(str, searchterm);
	}
	else
		result = strstr(str, searchterm);

	return result;
}

/*
 * Trim footer rows - We should to trim footer rows and calculate footer_char_size
 */
static void
trim_footer_rows(DataDesc *desc)
{
	if (desc->headline_transl != NULL && desc->footer_row != -1)
	{
		LineBufferIter lbi;
		char   *line;

		desc->footer_char_size = 0;

		init_lbi_ddesc(&lbi, desc, desc->footer_row);

		while(lbi_get_line_next(&lbi, &line, NULL, NULL))
		{
			char   *ptr = line;
			char   *last_nspc = NULL;
			int		len;

			/* search last non space char */
			while (*ptr)
			{
				if (*ptr != ' ')
					last_nspc = ptr;

				ptr += 1;
			}

			if (last_nspc)
				*(last_nspc + 1) = '\0';
			else
				*line = '\0';

			len = use_utf8 ? utf8len(line) : (int) strlen(line);
			if (len > desc->footer_char_size)
				desc->footer_char_size = len;
		}
	}
	else
		desc->footer_char_size = desc->maxx;
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

	if (opts->show_scrollbar)
	{
		int		saved_slider_min_y = 0;

		/*
		 * The relayout can be invoked in scrollbar mode too (when we try
		 * to change visibility of footer window (data)). Unfortunatelly,
		 * inside layout initialization we init slider position to. In
		 * this case we want to preserve an slider position.
		 */
		if (scrdesc->scrollbar_mode)
			saved_slider_min_y = scrdesc->slider_min_y;

		/* show scrollbar only when display is less than data */
		if (scrdesc->main_maxy < desc->last_row + 1)
		{
			Theme	   *t;

			scrdesc->main_maxx -= 1;

			t = &scrdesc->themes[WINDOW_VSCROLLBAR];

			scrdesc->scrollbar_x = scrdesc->maxx - 1;
			scrdesc->scrollbar_start_y = scrdesc->main_start_y;
			scrdesc->scrollbar_maxy = scrdesc->main_maxy;

			if (t->scrollbar_slider_symbol)
				scrdesc->slider_size = 1;
			else if (scrdesc->scrollbar_maxy > 4)
			{
				scrdesc->slider_size
					= round(((double) scrdesc->main_maxy - scrdesc->fix_rows_rows) *
							((double) scrdesc->main_maxy - 2) /
							((double) desc->last_row - desc->fixed_rows + 1));

				if (scrdesc->slider_size < 2)
					scrdesc->slider_size = 2;
			}
			else
			{
				scrdesc->slider_size = scrdesc->scrollbar_maxy - 4;
				if (scrdesc->slider_size < 1)
					scrdesc->slider_size = 1;
			}

			scrdesc->slider_min_y = 1;
		}
		else
			scrdesc->scrollbar_x = -1;

		/* restore saved slider position */
		if (scrdesc->scrollbar_mode)
			scrdesc->slider_min_y = saved_slider_min_y;
	}
	else
		scrdesc->scrollbar_x = -1;
}

static void
create_layout(Options *opts,
			  ScrDesc *scrdesc,
			  DataDesc *desc,
			  int first_data_row,
			  int first_row)
{
	int		i;

	for (i = 0; i < PSPG_WINDOW_COUNT; i++)
	{
		if (i != WINDOW_TOP_BAR &&
			i != WINDOW_BOTTOM_BAR)
		{
			if (scrdesc->wins[i])
			{
				delwin(scrdesc->wins[i]);
				scrdesc->wins[i] = NULL;
			}
		}
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

	if (opts->show_scrollbar)
	{
		w_vscrollbar(scrdesc) = subwin(stdscr,
									  scrdesc->scrollbar_maxy,
									  1,
									  scrdesc->scrollbar_start_y,
									  scrdesc->scrollbar_x);
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

	getmaxyx(stdscr, maxy, maxx);

	if (top_bar != NULL)
	{
		delwin(top_bar);
		top_bar = NULL;
		w_top_bar(scrdesc) = NULL;
	}

	if (opts->menu_always)
		scrdesc->top_bar_rows = 1;
	else if (opts->no_topbar)
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

	/*
	 * bottom_bar should be created every time - it can be used
	 * for an alerts, but sometimes, it should not be visible.
	 */
	bottom_bar = subwin(stdscr, 1, 0, maxy - 1, 0);
	w_bottom_bar(scrdesc) = bottom_bar;


	/* When it is visible, clean content and set background */
	if (opts->less_status_bar

#ifdef COMPILE_MENU
		|| !opts->no_commandbar

#endif

		)
	{
		werase(bottom_bar);

		/* data colours are better than default */
		wbkgd(bottom_bar, COLOR_PAIR(3));
		wnoutrefresh(bottom_bar);
	}

	scrdesc->main_maxy = maxy;

	scrdesc->main_maxx = maxx;
	scrdesc->main_start_y = 0;
	scrdesc->main_start_x = 0;

	if (scrdesc->top_bar_rows > 0)
	{
		scrdesc->main_maxy -= scrdesc->top_bar_rows;
		scrdesc->main_start_y = scrdesc->top_bar_rows;
	}

	if (opts->less_status_bar

#ifdef COMPILE_MENU

		|| !opts->no_commandbar

#endif

		)
		scrdesc->main_maxy -= 1;

	/*
	 * Store ref to bottom bar to global variable
	 */
	prompt_window = bottom_bar;
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
print_status(Options *opts,
			 ScrDesc *scrdesc,
			 DataDesc *desc,
			 int cursor_row,
			 int cursor_col,
			 int first_row,
			 int fix_rows_offset,
			 int vertical_cursor_column)
{
	int		maxy, maxx;
	int		smaxy, smaxx;
	char	buffer[200];
	WINDOW *top_bar = w_top_bar(scrdesc);
	WINDOW *bottom_bar = w_bottom_bar(scrdesc);
	Theme  *top_bar_theme = &scrdesc->themes[WINDOW_TOP_BAR];
	Theme  *bottom_bar_theme = &scrdesc->themes[WINDOW_BOTTOM_BAR];

	double	percent;

	/* do nothing when there are not top status bar */
	if (scrdesc->top_bar_rows > 0)
	{
		getmaxyx(top_bar, maxy, maxx);
		getmaxyx(stdscr, smaxy, smaxx);

		UNUSED(maxy);

		wbkgd(top_bar, current_state->errstr ? bottom_bar_theme->error_attr : COLOR_PAIR(2));
		werase(top_bar);

		if ((desc->title[0] != '\0' || desc->filename[0] != '\0') && !current_state->errstr)
		{
			wattron(top_bar, top_bar_theme->title_attr);
			if (desc->title[0] != '\0' && desc->title_rows > 0)
				mvwprintw(top_bar, 0, 0, "%s", desc->title);
			else if (desc->filename[0] != '\0')
				mvwprintw(top_bar, 0, 0, "%s", desc->filename);
			wattroff(top_bar, top_bar_theme->title_attr);
		}

		if (opts->watch_time > 0 || current_state->errstr)
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

				if (!current_state->errstr &&
					(desc->title[0] != '\0' || desc->filename[0] != '\0'))
					x = maxx / 4;

				if (paused)
					mvwprintw(top_bar, 0, x, "paused %ld sec", td / 1000);
				else
					mvwprintw(top_bar, 0, x, "%*ld/%d", w, td/1000 + 1, opts->watch_time);
			}

			if (current_state->errstr)
			{
				int		i;
				char   *ptr = buffer;

				/* copy first row to buffer */
				for (i = 0; i < 200; i++)
					if (current_state->errstr[i] == '\0' ||
						current_state->errstr[i] == '\n')
					{
						*ptr = '\0';
						break;
					}
					else
						*ptr++ = current_state->errstr[i];

				wprintw(top_bar, "   %s", buffer);

				return;
			}
		}

		if (desc->headline_transl)
		{
			/* tabular doc */
			if (opts->no_cursor)
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
		}
		else
		{
			/* txt doc */
			if (opts->no_cursor)
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
			size_t		sz = charlen(str);

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
make_beep(void)
{
	if (!quiet_mode)
		beep();
}

/*
 * It is used for result of action info
 */
void
show_info_wait(const char *fmt,
			   const char *par,
			   bool beep,
			   bool refresh_first,
			   bool applytimeout,
			   bool is_error)
{
	attr_t  att;
	int		c;
	int		timeout = -1;
	bool	press_alt;
	MEVENT	mevent;

	/*
	 * When refresh is required first, then store params and quit immediately.
	 * Only once can be info moved after refresh
	 */
	if (refresh_first && current_state->fmt == NULL)
	{
		if (fmt != NULL)
			current_state->fmt = sstrdup(fmt);
		else
			current_state->fmt = NULL;

		if (par != NULL)
			current_state->par = sstrdup(par);
		else
			current_state->par = NULL;
		current_state->beep = beep;
		current_state->applytimeout = applytimeout;
		current_state->is_error = is_error;

		return;
	}

	att = !is_error ? prompt_window_info_attr : prompt_window_error_attr;

	wattron(prompt_window, att);

	if (par != NULL)
		mvwprintw(prompt_window, 0, 0, fmt, par);
	else
		mvwprintw(prompt_window, 0, 0, "%s", fmt);

	if (!applytimeout)
		wprintw(prompt_window, " (press any key)");

	if (par)
		log_row(fmt, par);
	else
		log_row(fmt);

	wclrtoeol(prompt_window);
	mvwchgat(prompt_window, 0, 0, -1, att, PAIR_NUMBER(att), 0);

	wattroff(prompt_window,  att);
	wnoutrefresh(prompt_window);

	doupdate();

	if (beep)
		make_beep();

	if (applytimeout)
		timeout = strlen(fmt) < 50 ? 2000 : 6000;

	c = get_event(&mevent, &press_alt, &got_sigint, NULL, NULL, NULL, timeout, 0);

	/*
	 * Screen should be refreshed after show any info.
	 */
	current_state->refresh_scr = true;

	/* eat escape if pressed here */
	if (!(c == 27 && press_alt))
		unget_event(c, press_alt, &mevent);
}

#define SEARCH_FORWARD			1
#define SEARCH_BACKWARD			2

static bool
has_upperchr(char *str)
{
	if (use_utf8)
	{
		while (*str != '\0')
		{
			if (utf8_isupper(str))
				return true;

			str += utf8charlen(*str);
		}
	}
	else
	{
		while (*str != '\0')
		{
			if (isupper(*str))
				return true;

			str += 1;
		}
	}

	return false;
}

static void
reset_searching_lineinfo(DataDesc *desc)
{
	SimpleLineBufferIter slbi, *_slbi;
	LineInfo   *linfo;

	_slbi = init_slbi_ddesc(&slbi, desc);

	while (_slbi)
	{
		_slbi = slbi_get_line_next(_slbi, NULL, &linfo);

		if (linfo)
		{
			linfo->mask |= LINEINFO_UNKNOWN;
			linfo->mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);
		}
	}
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
 * Routines for maintaining of buffered events
 */
static void
unget_event(int key_code, bool alt, MEVENT *mevent)
{
	if (pspg_event_buffer_read >= pspg_event_buffer_write)
	{
		pspg_event_buffer_read = 0;
		pspg_event_buffer_write = 0;
	}


	if (key_code == 0 || key_code == ERR)
		return;

	if (pspg_event_buffer_write < PSPG_EVENT_BUFFER_SIZE)
	{
		PspgEvent *event = &pspg_event_buffer[pspg_event_buffer_write++];

		event->key_code = key_code;
		event->alt = alt;

		if (key_code == KEY_MOUSE)
			memcpy(&event->mevent, mevent, sizeof(MEVENT));
	}
	else
		leave("internal event buffer is too short");

#ifdef DEBUG_PIPE

	fprintf(debug_pipe, "event buffer length: %d\n",
				  pspg_event_buffer_write - pspg_event_buffer_read);

#endif

}

static bool
get_buffered_event(int *key_code, bool *alt, MEVENT *mevent)
{
	bool	result = false;

	if (pspg_event_buffer_read < pspg_event_buffer_write)
	{
		PspgEvent *event = &pspg_event_buffer[pspg_event_buffer_read++];

		*key_code = event->key_code;
		*alt = event->alt;

		if (event->key_code == KEY_MOUSE)
			memcpy(mevent, &event->mevent, sizeof(MEVENT));

		result = true;
	}

	if (pspg_event_buffer_read >= pspg_event_buffer_write)
	{
		pspg_event_buffer_read = 0;
		pspg_event_buffer_write = 0;
	}

	return result;
}

static int
event_buffer_count(void)
{
	if (pspg_event_buffer_read < pspg_event_buffer_write)
		return pspg_event_buffer_write - pspg_event_buffer_read;
	else
		return 0;
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
		  int timeoutval,
		  int hold_stream)
{
	bool	first_event = true;
	int		c;
	int		loops = -1;
	int		retry_count = 0;
	bool	hungry_mouse_mode = false;
	int		stdscr_delay = -1;

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	wint_t	ch;
	int		ret;

#endif

#ifdef DEBUG_PIPE

	char	buffer[20];

	if (0)
	{

#ifdef __GNU_LIBRARY__

/*
 * This test doesn't work well. Looks so HAVE_MALLINFO2 is undefined
 * although mallinfo2 function exists, and mallinfo is deprecated.
 * Maybe autoconf issue.
 */
#if (__GLIBC__ == 2 &&  __GLIBC_MINOR__ >= 33) || __GLIBC__ > 2

		struct mallinfo2 mi;

		mi = mallinfo2();

		fprintf(debug_pipe, "Total non-mmapped bytes (arena):       %ld\n", mi.arena);
		fprintf(debug_pipe, "# of free chunks (ordblks):            %ld\n", mi.ordblks);
		fprintf(debug_pipe, "# of free fastbin blocks (smblks):     %ld\n", mi.smblks);
		fprintf(debug_pipe, "# of mapped regions (hblks):           %ld\n", mi.hblks);
		fprintf(debug_pipe, "Bytes in mapped regions (hblkhd):      %ld\n", mi.hblkhd);
		fprintf(debug_pipe, "Max. total allocated space (usmblks):  %ld\n", mi.usmblks);
		fprintf(debug_pipe, "Free bytes held in fastbins (fsmblks): %ld\n", mi.fsmblks);
		fprintf(debug_pipe, "Total allocated space (uordblks):      %ld\n", mi.uordblks);
		fprintf(debug_pipe, "Total free space (fordblks):           %ld\n", mi.fordblks);
		fprintf(debug_pipe, "Topmost releasable block (keepcost):   %ld\n", mi.keepcost);

#else

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

#endif

#endif

	}

#endif

	/* returns buffered events if exists*/
	if (get_buffered_event(&c, alt, mevent))
	{
		*sigint = false;

		if (timeout)
			*timeout = false;

		if (file_event)
			*file_event = false;

		if (reopen_file)
			*reopen_file = false;

		goto throw_event;
	}

retry:

	*alt = false;
	*sigint = false;

	if (timeout)
		*timeout = false;

	/* check file event when it is wanted, and when event is available */
	if (file_event && current_state->fds[1].fd != -1)
	{
		int		poll_num;

		*file_event = false;

		if (reopen_file)
			*reopen_file = false;

		poll_num = poll(current_state->fds, 2, timeoutval);
		if (poll_num == -1)
		{
			/* pool error is expected after sigint */
			if (handle_sigint)
			{
				*sigint = true;
				handle_sigint = false;

				return 0;
			}

			log_row("poll error (%s)", strerror(errno));
		}
		else if (poll_num > 0)
		{
			/* process inotify event, but only when we can process it */
			if (current_state->fds[1].revents)
			{
				short revents = current_state->fds[1].revents;
				char		buff[64];
				ssize_t		len;

				*file_event = true;

				/* handle implicit events */
				if (revents & POLLHUP)
				{
					if (hold_stream == 1)
					{
						*reopen_file = true;
						return 0;
					}
					else
						leave("input stream was closed");
				}

				/* go out when we don't use inotify */
				if (current_state->inotify_fd == -1)
					return 0;

				/* there are a events on monitored file */
				len = read(current_state->inotify_fd, buff, sizeof(buff));

				/*
				 * read to end, it is notblocking IO, only one event and
				 * one file is monitored
				 */
				while (len > 0)
				{

#ifdef HAVE_INOTIFY

					const struct inotify_event *ino_event = (struct inotify_event *) buff;

					while (len > 0 && 0)
					{
						if ((ino_event->mask & IN_CLOSE_WRITE))
						{
							if (reopen_file)
								*reopen_file = true;
						}

						len -= sizeof (struct inotify_event) + ino_event->len;
						ino_event += sizeof (struct inotify_event) + ino_event->len;
					}

#endif

					len = read(current_state->inotify_fd, buff, sizeof(buff));
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
		UNUSED(ret);

		c = ch;

#else

		c = getch();

#endif

		/*
		 * Mouse events can come too fast. The result of too frequent
		 * updates can be unwanted flickering (the terminal is not
		 * synchronized with screen refreshing). If we store too frequent
		 * mouse events to own buffer, and we can skip screen refreshing
		 * when we know so next event will come immediately.
		 */
		if (c == KEY_MOUSE)
		{
			int		ok;

			/* activate unblocking mode */
			if (!hungry_mouse_mode)
			{
				stdscr_delay = _wgetdelay(stdscr);

				timeout(0);

				hungry_mouse_mode = true;
			}

			ok = getmouse(mevent);
			if (ok == OK)
				unget_event(c, false, mevent);

			/*
			 * You can continue in hungry mode until buffer is full */
			if (pspg_event_buffer_write + 1 < PSPG_EVENT_BUFFER_SIZE)
				continue;
			else
				goto return_first_mouse_event;
		}
		else
		{
			if (hungry_mouse_mode)
			{
				/* disable unblocking mode */
				timeout(stdscr_delay);
				hungry_mouse_mode = false;

				if (c != 0 && c != ERR)
					unget_event(c, false, NULL);

				*alt = false;
				*sigint = false;
				if (timeout)
					*timeout = false;

				if (file_event)
					*file_event = false;

				if (reopen_file)
					*reopen_file = false;

return_first_mouse_event:

				if (get_buffered_event(&c, alt, mevent))
					goto throw_event;

				return 0;
			}
		}

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
		if (errno != 0)
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

throw_event:

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
		log_row("ERR input - retry no: %d", retry_count);

		if (++retry_count < 10)
			goto retry;

		log_row("ERR input - touch retry limit, stop");
	}

	return c;
}

#define VISIBLE_DATA_ROWS		(scrdesc.main_maxy - scrdesc.fix_rows_rows - fix_rows_offset)
#define MAX_FIRST_ROW			(desc.last_row - desc.title_rows - scrdesc.main_maxy + 1)
#define MAX_CURSOR_ROW			(desc.last_row - desc.first_data_row)
#define CURSOR_ROW_OFFSET		(scrdesc.fix_rows_rows + desc.title_rows + fix_rows_offset)

void
exit_ncurses(void)
{
	if (active_ncurses)
		endwin();

	(void) disable_xterm_mouse_mode();
}

static void
DataDescFree(DataDesc *desc)
{
	lb_free(desc);
	free(desc->order_map);
	free(desc->headline_transl);
	free(desc->cranges);
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

	new->search_first_row = old->search_first_row;
	new->search_rows = old->search_rows;
	new->search_first_column = old->search_first_column;
	new->search_columns = old->search_columns;
	new->search_selected_mode = old->search_selected_mode;

	memcpy(new->searchcolterm, old->searchcolterm, 255);
	new->searchcolterm_size = old->searchcolterm_size;

	new->has_upperchr = old->has_upperchr;
	new->found = old->found;
	new->found_start_x = old->found_start_x;
	new->found_start_bytes = old->found_start_bytes;
	new->found_row = old->found_row;

	new->selected_first_row = old->selected_first_row;
	new->selected_rows = old->selected_rows;
	new->selected_first_column = old->selected_first_column;
	new->selected_columns = old->selected_columns;
}

/*
 * Ensure so first_row is in correct range
 */
static int
adjust_first_row(int first_row, DataDesc *desc, ScrDesc *scrdesc)
{
	int		max_first_row;

	if (first_row < 0)
		first_row = 0;

	max_first_row = desc->last_row - desc->title_rows - scrdesc->main_maxy + 1;
	max_first_row = max_first_row < 0 ? 0 : max_first_row;

	return first_row > max_first_row ? max_first_row : first_row;
}

/*
 * When error is detected, then better to clean screen
 */
static void
check_clipboard_app(Options *opts, bool *force_refresh)
{
	*force_refresh = false;

	if (opts->clipboard_app)
		clipboard_application_id = opts->clipboard_app;

	if (!clipboard_application_id)
	{
		FILE	   *f;
		char	   *line = NULL;
		size_t		size = 0;
		int			status;
		bool		isokstr = false;
		int			retval;

		/* check wl-clipboard https://github.com/bugaevc/wl-clipboard.git */
		errno = 0;
		f = popen("wl-copy -v 2>/dev/null", "r");
		if (f)
		{
			retval = getline(&line, &size, f);
			if (retval >= 0 && line)
			{
				if (strncmp(line, "wl-clipboard", 12) == 0)
					isokstr = true;

				free(line);
				line = NULL;
				size = 0;
			}

			status = pclose(f);
			if (status == 0 && isokstr)
			{
				clipboard_application_id = 1;
				return;
			}
		}

		*force_refresh = true;

		errno = 0;
		f = popen("xclip -version 2>&1", "r");
		if (f)
		{
			retval = getline(&line, &size, f);
			if (retval >= 0 && line)
			{
				if (strncmp(line, "xclip", 5) == 0)
					isokstr = true;

				free(line);
				line = NULL;
				size = 0;
			}

			status = pclose(f);
			if (status == 0 && isokstr)
			{
				clipboard_application_id = 2;
				return;
			}
		}

		errno = 0;
		f = popen("pbcopy", "r");
		if (f)
		{
			retval = getline(&line, &size, f);
			if (retval >= 0 && line)
			{
				/* there is not version option */
				isokstr = true;

				free(line);
				line = NULL;
				size = 0;
			}

			status = pclose(f);
			if (status == 0 && isokstr)
			{
				clipboard_application_id = 3;
				return;
			}
		}
	}
}

void
export_to_file(PspgCommand command,
			  ClipboardFormat format,
			  Options *opts,
			  ScrDesc *scrdesc,
			  DataDesc *desc,
			  int cursor_row,
			  int cursor_column,
			  int rows,
			  double percent,
			  const char *pipecmd,
			  bool *force_refresh)
{
	char	buffer[MAXPATHLEN + 1024];
	char	number[100];
	char	table_name[255];
	FILE   *fp = NULL;
	char   *path = NULL;
	bool	isok = false;
	int		fin = -1, fout = -1, ferr = -1;
	pid_t	pid;
	bool	copy_to_file = false;
	bool	use_pipe = false;

	*force_refresh = false;

	if (command == cmd_CopyColumn)
	{
		if (!check_visible_vertical_cursor(desc, opts, cursor_column))
			return;
	}

	if ((command == cmd_CopyLine || command == cmd_CopyLineExtended) &&
		opts->no_cursor)
	{
		show_info_wait(" Cursor is not visible",
					   NULL, true, true, true, false);
		return;
	}

	if (command == cmd_CopySelected &&
		!(scrdesc->selected_first_row != -1 ||
		  scrdesc->selected_first_column != -1))
	{
		show_info_wait(" There are not selected data",
					   NULL, true, true, true, false);
		return;
	}

	if (pipecmd)
	{
		use_pipe = true;
	}
	else if (command == cmd_SaveData ||
			 command == cmd_SaveAsCSV ||
			 opts->copy_target == COPY_TARGET_FILE)
	{
		char   *prompt;
		char   *ptr;

		if (format == CLIPBOARD_FORMAT_CSV)
			prompt = "save to CSV file: ";
		else
			prompt = "save to file: ";

		(void) get_string(prompt, buffer, sizeof(buffer) - 1, last_path, 'f');
		if (buffer[0] == '\0')
			return;

		ptr = buffer;
		while (*ptr == ' ')
			ptr++;

		if (*ptr == '|')
			use_pipe = true;
		else
			copy_to_file = true;
	}

	if (INSERT_FORMAT_TYPE(format))
	{
		(void) get_string("target table name: ", table_name, sizeof(table_name) - 1, last_table_name, 'u');
		if (table_name[0] == '\0')
			return;
	}

	if (command == cmd_CopyTopLines ||
		command == cmd_CopyBottomLines)
	{
		if (rows == 0 && percent == 0.0)
		{
			char	   *endptr;

			(void) get_string("rows: ", number, sizeof(number) - 1, last_rows_number, 'u');
			if (number[0] == '\0')
				return;

			errno = 0;
			percent = strtod(number, &endptr);

			if (endptr == number)
			{
				show_info_wait(" Cannot convert input string to number",
							   NULL, true, true, false, true);
				return;
			}
			else if (errno != 0)
			{
				show_info_wait(" Cannot convert input string to number (%s)",
							   strerror(errno), true, true, false, true);
				return;
			}

			if (*endptr != '%')
			{
				rows = (int) percent;
				percent = 0.0;
			}
		}

		strncpy(last_rows_number, number, sizeof(last_rows_number) - 1);
		last_rows_number[sizeof(last_rows_number) - 1] = '\0';
	}

	if (copy_to_file)
	{
		path = tilde(NULL, buffer);

		errno = 0;
		current_state->errstr = NULL;

		fp = fopen(path, "w");
	}
	else if (use_pipe)
	{
		errno = 0;
		current_state->errstr = NULL;

		*force_refresh = true;

		endwin();

		signal(SIGPIPE, SIG_IGN);

		if (pipecmd)
			fp = popen(pipecmd, "w");
		else
		{
			char   *ptr = buffer;

			while (*ptr == ' ')
				ptr++;

			if (*ptr == '|')
				fp = popen(++ptr, "w");
		}
	}
	else
	{
		char   *fmt;
		char	cmdline_clipboard_app[1024];

		check_clipboard_app(opts, force_refresh);

		if (!clipboard_application_id)
		{
			show_info_wait(" Cannot find clipboard application",
						   NULL, true, true, false, true);

				return;
		}

		if (format == CLIPBOARD_FORMAT_TEXT ||
			INSERT_FORMAT_TYPE(format))
		{
			if (use_utf8)
				fmt = "text/plain;charset=utf-8";
			else
				fmt = "text/plain";

		}
		else if (format == CLIPBOARD_FORMAT_CSV)
		{
			if (use_utf8)
				fmt = "text/csv;charset=utf-8";
			else
				fmt = "text/csv";
		}
		else if (format == CLIPBOARD_FORMAT_TSVC)
		{
			fmt = "application/x-libreoffice-tsvc";
		}
		else /* fallback */
		{
			if (use_utf8)
				fmt = "text/plain;charset=utf-8";
			else
				fmt = "text/plain";
		}

		if (clipboard_application_id == 1)
			snprintf(cmdline_clipboard_app, 1024, "wl-copy -t %s", fmt);
		else if (clipboard_application_id == 2)
			snprintf(cmdline_clipboard_app, 1024, "xclip -sel clip");
		else if (clipboard_application_id == 3)
			snprintf(cmdline_clipboard_app, 1024, "pbcopy");

		if ((pid = rwe_popen(cmdline_clipboard_app, &fin, &fout, &ferr)) == -1)
		{
			format_error("%s", strerror(errno));
			log_row("open error (%s)", current_state->errstr);

			show_info_wait(" Cannot to start clipboard application",
						   NULL, true, true, false, true);

			/* err string is saved already, because refresh_first is used */
			current_state->errstr = NULL;
			*force_refresh = true;

			return;
		}

		fcntl(ferr, F_SETFL, O_NONBLOCK);

		fp = fdopen(fin, "w");
	}

	if (fp)
	{
		isok = export_data(opts, scrdesc, desc,
						   cursor_row, cursor_column,
						   fp,
						   rows, percent, table_name,
						   command, format);

		if (use_pipe)
		{
			int		stdscr_delay = _wgetdelay(stdscr);
			int		res;

			res = pclose(fp);

			/*
			 * Ignore broken pipe error, when pclose is ok. It's probably
			 * due closing consument program.
			 */
			if (!isok && current_state->_errno == EPIPE && res == 0)
			{
				isok = true;
				current_state->errstr = NULL;
			}

			signal(SIGPIPE, SIG_DFL);

			fprintf(stderr, "press enter");

			timeout(-1);
			(void) getch();
			timeout(stdscr_delay);
		}
		else if (copy_to_file)
			fclose(fp);
		else
		{
			char	err_buffer[2048];
			int		errsz;
			int		status;

			memset(buffer, 0, sizeof(err_buffer));

			fclose(fp);
			fp = NULL;

			waitpid(pid, &status, 0);
			errsz = read(ferr, err_buffer, 1000);

			close(fin);
			close(fout);
			close(ferr);

			if (errsz != -1)
			{
				format_error("%s", err_buffer);
				log_row("write error (%s)", current_state->errstr);

				show_info_wait(" Cannot write to clipboard (%s)",
							   (char *) current_state->errstr, true, true, false, true);

				/* err string is saved already, because refresh_first is used */
				current_state->errstr = NULL;
				*force_refresh = true;

				return;
			}
		}
	}

	if (!isok)
	{
		if (path)
		{
			if (current_state->errstr != 0)
				snprintf(buffer, sizeof(buffer), "%s (%s)", path, current_state->errstr);
			else
				strcpy(buffer, path);
		}
		else
		{
			if (current_state->errstr != 0)
				snprintf(buffer, sizeof(buffer), "clipboard (%s)", current_state->errstr);
			else
				strcpy(buffer, "clipboard");
		}

		show_info_wait(" Cannot write to %s",
					   buffer, true, false, false, true);
		*force_refresh = true;
	}

	current_state->errstr = NULL;
	current_state->_errno = 0;
}

/*
 * From "first_row" calculate position of slider of vertical scrollbar
 */
static void
set_scrollbar(ScrDesc *scrdesc, DataDesc *desc, int first_row)
{
	int		max_first_row;
	int		max_slider_min_y;

	if (scrdesc->slider_has_position ||
		scrdesc->scrollbar_mode)
	{
		scrdesc->slider_has_position = false;

		return;
	}

	max_first_row = desc->last_row - desc->title_rows - scrdesc->main_maxy + 1;
	max_slider_min_y = scrdesc->scrollbar_maxy - scrdesc->slider_size - 1;

	/*
	 * We want to map
	 *
	 *    first_row (0) -> slider_min_y = 1
	 *    first_row (max_first_row) -> slider_min_y (max_slider_min_y)
	 * ------
	 *    first_row (1) -> slider_min_y = 2
	 *    first_row (max_first_row - 1) -> slider_min_y (max_slider_min_y - 1)
	 *
	 */
	if (first_row == 0)
	{
		scrdesc->slider_min_y = 1;

		return;
	}
	else if (first_row == max_first_row)
	{
		scrdesc->slider_min_y = max_slider_min_y;

		return;
	}

	scrdesc->slider_min_y = ((double) first_row - 1) /
							((double) max_first_row - 2) *
							((double) max_slider_min_y - 3) + 2;
}

typedef enum
{
	MARK_MODE_NONE,
	MARK_MODE_ROWS,			/* activated by F3 */
	MARK_MODE_BLOCK,		/* activated by F15 ~ Shift F3 */
	MARK_MODE_CURSOR,		/* activated by SHIFT + CURSOR */
	MARK_MODE_MOUSE,		/* activated by CTRL + MOUSE */
	MARK_MODE_MOUSE_COLUMNS,/* activated by CTRL + MOUSE on column headers */
	MARK_MODE_MOUSE_BLOCK,	/* activated by ALT + MOUSE */
} MarkModeType;

/*
 * Trivial functions reduce redundant code.
 */
void
throw_searching(ScrDesc *scrdesc, DataDesc *desc)
{
	*scrdesc->searchterm = '\0';
	*scrdesc->searchcolterm = '\0';

	scrdesc->searchterm_size = 0;
	scrdesc->searchterm_char_size = 0;

	scrdesc->search_first_row = -1;
	scrdesc->search_rows = 0;
	scrdesc->search_first_column = -1;
	scrdesc->search_columns = 0;
	scrdesc->search_selected_mode = false;

	scrdesc->found = false;

	reset_searching_lineinfo(desc);
}

static void
throw_selection(ScrDesc *scrdesc, DataDesc *desc, MarkModeType *mark_mode)
{
	scrdesc->selected_first_row = -1;
	scrdesc->selected_rows = 0;
	scrdesc->selected_first_column = -1;
	scrdesc->selected_columns = 0;

	*mark_mode = MARK_MODE_NONE;

	if (scrdesc->search_selected_mode)
		throw_searching(scrdesc, desc);
}

static bool
check_visible_vertical_cursor(DataDesc *desc,
							  Options *opts,
							  int vertical_cursor_column)
{
	if (desc->columns == 0)
	{
		show_info_wait(" Sort is available only for tables.",
					   NULL, true, true, true, false);

		return false;
	}

	if (!opts->vertical_cursor || vertical_cursor_column == 0)
	{
		show_info_wait(" Vertical cursor is not visible",
					   NULL, true, true, true, false);

		return false;
	}

	return true;
}

static int
mousex_get_colno(DataDesc *desc,
				 ScrDesc *scrdesc,
				 Options *opts,
				 int *cursor_col,
				 int default_freezed_cols,
				 int mousex)
{
	int		colno = -1;
	int		xpoint = mousex - scrdesc->main_start_x;

	if (xpoint > scrdesc->fix_cols_cols - 1)
		xpoint += *cursor_col;

	if (xpoint >= 0)
	{
		int		i;

		for (i = 0; i  < desc->columns; i++)
		{
			if (desc->cranges[i].xmin <= xpoint && desc->cranges[i].xmax >= xpoint)
			{
				int		xmin = desc->cranges[i].xmin;
				int		xmax = desc->cranges[i].xmax;

				colno = i + 1;

				if (colno > (opts->freezed_cols != -1 ? opts->freezed_cols : default_freezed_cols))
				{
					if (xmax > scrdesc->main_maxx + *cursor_col)
						*cursor_col = xmax - scrdesc->main_maxx;
					else if (xmin < scrdesc->fix_cols_cols + *cursor_col)
						*cursor_col = xmin - scrdesc->fix_cols_cols + 1;
				}

				break;
			}
		}
	}

	return colno;
}

/*
 * Available modes for processing input.
 *
 *   1. read and close (default)
 *   2. repeated read and close (timer)
 *   3. repeated read and close (inotify)
 *   4. query [postgres]
 *   5. repeated query [postgres] (timer)
 *   6. stream read [pipe]
 *   7. stream read with reopen [fifo]
 *   8. stream read with reopen and seek to end [file] (inotify)
 */
int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		event_keycode = 0;
	int		prev_event_keycode = 0;
	int		next_event_keycode = 0;
	int		command = cmd_Invalid;
	int		nested_command = cmd_Invalid;
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
	DataDesc		desc;
	ScrDesc			scrdesc;
	Options			opts;
	StateData		state;
	int		fixedRows = -1;			/* detect automatically (not yet implemented option) */
	bool	detected_format = false;
	int		fix_rows_offset = 0;

	mmask_t		prev_mousemask = 0;
	int		search_direction = SEARCH_FORWARD;
	bool	redirect_mode;
	bool	noatty;					/* true, when cannot to get keys from stdin */
	bool	fresh_found = false;
	int		fresh_found_cursor_col = -1;
	bool	reinit = false;

	bool	ignore_mouse_release = false;		/* after leave menu by press ignore release too */
	bool	no_doupdate = false;				/* when we sure stdstr refresh is useless */
	bool	raw_output_quit = false;

	bool	mouse_was_initialized = false;

	int		last_ordered_column = -1;			/* order by when watch mode is active */
	bool	last_order_desc = false;			/* true, when sort of data is descend */

	long	mouse_event = 0;
	long	vertical_cursor_changed_mouse_event = 0;

	int		pspg_win_iter;

	WINDOW	   *win = NULL;
	SCREEN	   *term = NULL;

	bool	press_alt;
	MEVENT	event;

	char   *pspgenv;
	bool	result;

#ifdef DEBUG_PIPE

	time_t	start_app_sec;
	long	start_app_ms;
	bool	first_doupdate = true;

#endif

	time_t	last_doupdate_sec = -1;
	long	last_doupdate_ms = -1;

	struct winsize size;
	bool		size_is_valid = false;
	int			ioctl_result;
	bool		handle_timeout = false;

#ifdef COMPILE_MENU

	bool	menu_is_active = false;
	struct ST_MENU		*menu = NULL;
	struct ST_CMDBAR	*cmdbar = NULL;

#endif

#if NCURSES_MOUSE_VERSION > 1

	int		scrollbar_mode_initial_slider_mouse_offset_y = -1;

#endif

	MarkModeType	mark_mode = MARK_MODE_NONE;
	int		mark_mode_start_row = 0;
	int		mark_mode_start_col = 0;

	int		mouse_row = -1;
	int		mouse_col = -1;

	/*
	 * Arguments of forwarded commands
	 */
	char   *string_argument = NULL;
	bool	string_argument_is_valid = false;
	long	long_argument = 1;
	bool	long_argument_is_valid = false;

	/* Name of pspg config file */
	const char *PSPG_CONF;
	const char *PSPG_HISTORY;

	memset(&opts, 0, sizeof(opts));
	opts.theme = 1;
	opts.freezed_cols = -1;				/* default will be 1 if screen width will be enough */
	opts.csv_separator = -1;			/* auto detection */
	opts.csv_header = 'a';				/* auto detection */
	opts.border_type = 2;			/* outer border */

#ifdef HAVE_INOTIFY

	opts.watch_file = true;

#endif

	opts.quit_on_f3 = false;
	opts.no_highlight_lines = false;
	opts.copy_target = COPY_TARGET_CLIPBOARD;
	opts.clipboard_format = CLIPBOARD_FORMAT_CSV;
	opts.empty_string_is_null = true;
	opts.xterm_mouse_mode = true;
	opts.show_scrollbar = true;
	opts.clipboard_app = 0;
	opts.no_sleep = false;
	opts.menu_always = false;
	opts.nullstr = NULL;
	opts.last_row_search = true;

	setup_sigsegv_handler();

	PSPG_CONF = getenv("PSPG_CONF");
	if (!PSPG_CONF)
		PSPG_CONF = "~/.pspgconf";

	load_config(tilde(NULL, PSPG_CONF), &opts);
	if (errno && strcmp(PSPG_CONF, "~/.pspgconf") != 0)
		fprintf(stderr, "cannot to read from config file \"%s\" specified by PSPG_CONF (%s), ignored\n",
				PSPG_CONF,
				strerror(errno));

	PSPG_HISTORY = getenv("PSPG_HISTORY");
	if (!PSPG_HISTORY)
		PSPG_HISTORY = "~/.pspg_history";

	memset(&desc, 0, sizeof(desc));
	memset(&scrdesc, 0, sizeof(scrdesc));

#ifdef DEBUG_PIPE

	debug_pipe = fopen(DEBUG_PIPE, "w");
	setlinebuf(debug_pipe);
	fprintf(debug_pipe, "application start\n");

	current_time(&start_app_sec, &start_app_ms);

#endif

	memset(&state, 0, sizeof(state));

	state.reserved_rows = -1;					/* dbcli has significant number of self reserved lines */
	state.file_format_from_suffix = FILE_UNDEF;	/* input file is not defined */
	state.last_position = -1;					/* unknown position */
	state.inotify_fd = -1;						/* invalid filedescriptor */
	state.inotify_wd = -1;						/* invalid file descriptor */

	state.desc = &desc;							/* global reference used for readline's tabcomplete */

#ifdef HAVE_INOTIFY

	state.has_notify_support = true;

#endif

	current_state = &state;

	pspgenv = getenv("PSPG");
	if (pspgenv)
	{
		int		argc2;
		char  **argv2;

		argv2 = buildargv(pspgenv, &argc2, argv[0]);

		if (!readargs(argv2, argc2, &opts, &state))
		{
			if (state.errstr)
				leave(state.errstr);
			else
				exit(EXIT_SUCCESS);
		}
	}

	if (!readargs(argv, argc, &opts, &state))
	{
		if (state.errstr)
			leave(state.errstr);
		else
			exit(EXIT_SUCCESS);
	}

	if (!args_are_consistent(&opts, &state))
		leave(state.errstr ? state.errstr : "options are not valid");

	/* open log file when user want it */
	if (opts.log_pathname)
	{
		const char *pathname;

		pathname = tilde(NULL, opts.log_pathname);

		logfile = fopen(pathname, "a");
		if (!logfile)
			leave("cannot to open log file \"%s\"", pathname);

		setlinebuf(logfile);
	}

	if (state.boot_wait > 0)
		usleep(1000 * 1000 * state.boot_wait);

	/*
	 * don't use inotify, when user prefer periodic watch time, or when we
	 * have not file for watching
	 */
	if (opts.watch_time || !opts.pathname)
		opts.watch_file = false;

	if (!open_data_file(&opts, &state))
	{
		/* ncurses are not started yet */
		if (state.errstr)
		{
			log_row(state.errstr);
			fprintf(stderr, "%s\n", state.errstr);
		}

		if (!opts.watch_time)
			exit(EXIT_FAILURE);
	}

	if (state.fp && !ferror(state.fp) &&
		state.is_file &&
		(opts.watch_file || state.stream_mode))
	{

#ifdef HAVE_INOTIFY

		state.inotify_fd = inotify_init1(IN_NONBLOCK);
		if (state.inotify_fd == -1)
			leave("cannot initialize inotify (%s)", strerror(errno));

		state.inotify_wd = inotify_add_watch(state.inotify_fd,
											 state.pathname,
											 IN_CLOSE_WRITE |
											 (state.stream_mode ? IN_MODIFY : 0));

		if (state.inotify_wd == -1)
			leave("cannot watch file \"%s\" (%s)", state.pathname, strerror(errno));

#else

		leave("missing inotify support");

#endif

	}

	if (opts.less_status_bar)
		opts.no_topbar = true;

	setlocale(LC_ALL, "");

	/*
	 * Don't use UTF when terminal doesn't use UTF. Without
	 * correct setting of encoding, the ncurses should not
	 * work too.
	 */
	use_utf8 = strcmp(nl_langinfo(CODESET), "UTF-8") == 0;

	log_row("started");
	log_row("%s utf8 support", use_utf8 ? "with" : "without");

	if (opts.csv_format || opts.tsv_format || opts.query)
		result = read_and_format(&opts, &desc, &state);
	else if (opts.querystream)
	{
		result = readfile(&opts, &desc, &state);

		if (result)
			result = read_and_format(&opts, &desc, &state);
	}
	else
		result = readfile(&opts, &desc, &state);

	/* when we can get content later, we can ignore empty dataset */
	if (!result)
	{
		if (state.errstr)
			leave(state.errstr);

		if (!state.stream_mode && !(opts.watch_time > 0))
			leave("No data");
	}

	if (opts.watch_time > 0)
	{
		current_time(&last_watch_sec, &last_watch_ms);
		next_watch = last_watch_sec * 1000 + last_watch_ms + opts.watch_time * 1000;
	}

	if (state.fp && !state.stream_mode)
	{
		fclose(state.fp);
		state.fp = NULL;
	}

	log_row("read input %d rows", desc.total_rows);

	if ((opts.csv_format || opts.tsv_format || opts.query) &&
		(state.no_interactive || (!state.interactive && !isatty(STDOUT_FILENO))))
	{
		lb_print_all_ddesc(&desc, stdout);

		log_row("quit due non interactive mode");

		return 0;
	}

	if (desc.headline)
		(void) translate_headline(&desc);

	detected_format = desc.headline_transl;

	if (detected_format && desc.oid_name_table)
		default_freezed_cols = 2;

	/*
	 * just try to use this function, to check if it is
	 * widely supported.
	 */
	use_tioctl(true);

	/*
	 * The issue #75 - COLUMNS, LINES are not correctly initialized.
	 * Get real terminal size, and refresh ncurses data.
	 *
	 * This issue should be solved by use_tioctl(true).
	 */
	if ((ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size)) >= 0)
	{
		size_is_valid = true;
		log_row("terminal size by TIOCGWINSZ rows: %d, cols: %d", size.ws_row, size.ws_col);
	}
	else
		log_row("cannot to detect terminal size via TIOCGWINSZ: res: %d", ioctl_result);

	/* When we know terminal dimensions */
	if (size_is_valid && state.quit_if_one_screen)
	{
		int		available_rows = size.ws_row;

		if (state.reserved_rows != -1)
			available_rows -= state.reserved_rows;

		/* the content can be displayed in one screen */
		if (available_rows >= desc.last_row && size.ws_col > desc.maxx)
		{
			lb_print_all_ddesc(&desc, stdout);

			log_row("quit due quit_if_one_screen option without ncurses init");

			return 0;
		}
	}

	if (!detected_format && state.only_for_tables)
	{
		const char *pagerprog;
		FILE	   *fout = NULL;

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

		lb_print_all_ddesc(&desc, fout);

		if (fout != stdout)
		{
			pclose(fout);
			signal(SIGPIPE, SIG_DFL);
			signal(SIGINT, SigintHandler);
		}

		log_row("exit without start ncurses");
		if (logfile)
		{
			fclose(logfile);
			logfile = NULL;
		}

		return 0;
	}

	/*
	 * Note: some shorter but for some environments not working
	 * fragment:
	 *
	 * FILE *f = fopen("/dev/tty", "r+");
	 * newterm(termname(), f, f);
	 *
	 */
	if (state.fp == stdin && state.stream_mode)
	{
		/*
		 * Try to protect current stdin, when input is stdin and user
		 * want to stream mode. In this case try to open new tty stream
		 * and start new ncurses terminal with specified input stream.
		 */

#ifndef __APPLE__

		/* macOS can't use poll() on /dev/tty */
		state.tty = fopen("/dev/tty", "r+");

#endif

		if (!state.tty)
			state.tty = fopen(ttyname(fileno(stdout)), "r");

		if (!state.tty)
		{
			if (!isatty(fileno(stderr)))
				leave("missing a access to terminal device");

			state.tty = stderr;
		}

		/* quiet compiler */
		noatty = false;
	}
	else if (!isatty(fileno(stdin)))
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
				leave("missing a access to terminal device");

			noatty = true;
			fclose(stdin);
		}
	}
	else
		noatty = false;

	signal(SIGINT, SigintHandler);
	signal(SIGTERM, SigtermHandler);

	atexit(exit_ncurses);

	if (state.tty)
		/* use created tty device for input */
		term = newterm(termname(), stdout, state.tty);
	else if (noatty)
		/* use stderr like stdin. This is fallback solution used by less */
		term = newterm(termname(), stdout, stderr);
	else
		win = initscr();

	UNUSED(term);
	UNUSED(win);

	state.fds[0].fd = -1;
	state.fds[1].fd = -1;
	state.fds[0].events = POLLIN;
	state.fds[1].events = POLLIN;

	if (opts.watch_file && !state.is_fifo && !state.is_pipe)
	{
		state.fds[0].fd = noatty ? STDERR_FILENO : STDIN_FILENO;
		state.fds[1].fd = state.inotify_fd;
	}
	else if (state.stream_mode)
	{
		if (state.is_pipe)
		{
			state.fds[0].fd = fileno(state.tty);
			state.fds[1].fd = fileno(stdin);
		}
		else
		{
			/* state = is_fifo */
			state.fds[0].fd = noatty ? STDERR_FILENO : STDIN_FILENO;
			state.fds[1].fd = fileno(state.fp);
		}
	}

	log_row("ncurses started");

	active_ncurses = true;

	/* xterm mouse mode recheck */
	if (opts.xterm_mouse_mode)
	{

#if NCURSES_MOUSE_VERSION > 1

#ifdef NCURSES_EXT_FUNCS

		char	   *s;

		s = tigetstr((NCURSES_CONST char *) "kmous");

		if (s != NULL && s != (char *) -1)
		{
			char	   *termval = getenv("TERM");

			/*
			 * Robust identificantion of xterm mouse modes supports should be based on
			 * one valid prereqeusities: TERM has substring "xterm" or kmous string is
			 * "\033[M". But some terminal' emulators support these modes too although
			 * mentioned rules are not valid. So I'll try to use xterm mouse move more
			 * aggresively to detect that terminals doesn't support xterm mouse modes.
			 *
			 * if (strcmp(s, "\033[M") == 0 ||
			 *   (termval && strstr(termval, "xterm")))
			 *
			 * I have a plan to create an list of terminals, where xterm mouse modes
			 * are not supported, and fix possible issues step by step.
			 *
			 * User workaround: using --no_xterm_mouse_mode option.
			 */
			log_row("kmous=\\E%s, TERM=%s", s + 1, termval ? termval : "");

			opts.xterm_mouse_mode = true;
		}
		else
			opts.xterm_mouse_mode = false;

#else

		opts.xterm_mouse_mode = false;

#endif

#else

		opts.xterm_mouse_mode = false;

#endif
	}

	if (opts.xterm_mouse_mode)
		log_row("xterm mouse mode 1002 will be used");
	else
		log_row("without xterm mouse mode support");

	if(!has_colors())
		leave("your terminal does not support color");

	start_color();

reinit_theme:

	initialize_color_pairs(opts.theme, opts.bold_labels, opts.bold_cursor);

	timeout(1000);

	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	leaveok(stdscr, TRUE);

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

		mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED |
				  BUTTON4_PRESSED | BUTTON5_PRESSED |
				  BUTTON_ALT | BUTTON_CTRL |
				  (opts.xterm_mouse_mode ? REPORT_MOUSE_POSITION : 0),
				  NULL);

		enable_xterm_mouse_mode(opts.xterm_mouse_mode);

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

	trim_footer_rows(&desc);

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
	{
		memset(&scrdesc, 0, sizeof(ScrDesc));

		throw_searching(&scrdesc, &desc);
		throw_selection(&scrdesc, &desc, &mark_mode);
	}

	initialize_theme(opts.theme, WINDOW_TOP_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_TOP_BAR]);
	initialize_theme(opts.theme, WINDOW_BOTTOM_BAR, desc.headline_transl != NULL, false, &scrdesc.themes[WINDOW_BOTTOM_BAR]);

	prompt_window_input_attr = scrdesc.themes[WINDOW_BOTTOM_BAR].input_attr;
	prompt_window_error_attr = scrdesc.themes[WINDOW_BOTTOM_BAR].error_attr;
	prompt_window_info_attr = scrdesc.themes[WINDOW_BOTTOM_BAR].bottom_light_attr;

	if (size_is_valid)
		resize_term(size.ws_row, size.ws_col);

	clear();

	refresh_aux_windows(&opts, &scrdesc);

	getmaxyx(stdscr, maxy, maxx);

	log_row("screen size - maxy: %d, maxx: %d", maxy, maxx);

	if (state.quit_if_one_screen)
	{
		int		available_rows = maxy;

		if (state.reserved_rows != -1)
			available_rows -= state.reserved_rows;

		/* the content can be displayed in one screen */
		if (available_rows >= desc.last_row && maxx >= desc.maxx)
		{
			endwin();

			lb_print_all_ddesc(&desc, stdout);

			log_row("ncurses ended and quit due quit_if_one_screen option");

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
					str += charlen(str);
				}
			}
		}
		else
		{
			if (desc.border_type != 2)
			{
				if (desc.border_bottom_row == -1 && desc.footer_row == -1)
				{
					/*
					 * It is hard to detect end of table and start of footer
					 * when border_type != 2. But for border_type = 1 it is
					 * possible. First footer line starting with nonspace.
					 * But some data should not to have footer.
					 */
					if (desc.border_type == 1)
					{
						if (desc.alt_footer_row != -1)
						{
							desc.footer_row = desc.alt_footer_row;
							desc.last_data_row = desc.footer_row - 1;
						}
						else
							desc.last_data_row = desc.last_row;
					}
					else
					{
						/*
						 * fallback - we cannot to distingush tabular data
						 * and footer data in border 0
						 */
						desc.last_data_row = desc.last_row - 1;
						desc.footer_row = desc.last_row;
					}
				}

				trim_footer_rows(&desc);
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

	initialize_theme(opts.theme, WINDOW_VSCROLLBAR, desc.headline_transl != NULL, opts.no_highlight_lines, &scrdesc.themes[WINDOW_VSCROLLBAR]);

	print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, 0, vertical_cursor_column);
	set_scrollbar(&scrdesc, &desc, first_row);

	cmdline[0] = '\0';
	cmdline_ptr = NULL;

	last_row_search[0] = '\0';
	last_col_search[0] = '\0';
	last_line[0] = '\0';
	last_path[0] = '\0';
	last_rows_number[0] = '\0';
	last_nullstr[0] = '\0';

	/* initialize readline if it is active */
	pspg_init_readline(PSPG_HISTORY);

#ifdef COMPILE_MENU

	init_menu_config(&opts);
	if (!opts.less_status_bar && !opts.no_commandbar)
		cmdbar = init_cmdbar(cmdbar, &opts);

	if (opts.menu_always)
	{
		st_menu_set_desktop_window(stdscr);
		menu = init_menu(menu, &opts);
		st_menu_set_focus(menu, ST_MENU_FOCUS_MOUSE_ONLY);
		log_row("init menu bar");
	}

#endif

	while (true)
	{
		bool	refresh_scr = false;
		bool	refresh_clear = false;
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
		 * Try to read command from commandline buffer first
		 */
		if (next_command == cmd_Invalid && cmdline_ptr && *cmdline_ptr)
		{
			while (cmdline_ptr && *cmdline_ptr)
			{
				if (*cmdline_ptr == '\\')
				{
					next_command = cmd_BsCommand;
					break;
				}

				cmdline_ptr += 1;
			}
		}

		/*
		 * Draw windows, only when function (key) redirect was not forced.
		 * Redirect emmit immediate redraw.
		 */
		if (next_command == cmd_Invalid)
		{
			bool	skip_update_after_mouse_event =
						event_buffer_count() > 0

#ifdef COMPILE_MENU

						&& !menu_is_active
#endif
						;

			if (!no_doupdate &&
				!handle_timeout &&
				!skip_update_after_mouse_event)
			{
				int		vcursor_xmin_fix = -1;
				int		vcursor_xmax_fix = -1;
				int		vcursor_xmin_data = -1;
				int		vcursor_xmax_data = -1;
				int		selected_xmin = -1;
				int		selected_xmax = -1;
				int		i;

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

				/* Calculate selected range in mark mode */
				if (mark_mode != MARK_MODE_NONE)
				{
					int		ref_row;
					int		ref_col;

					switch (mark_mode)
					{
						case MARK_MODE_MOUSE:
						case MARK_MODE_MOUSE_BLOCK:
							ref_row = mouse_row;
							break;
						case MARK_MODE_ROWS:
						case MARK_MODE_BLOCK:
						case MARK_MODE_CURSOR:
							ref_row = cursor_row;
							break;

						default:
							ref_row = -1;
					}

					if (mark_mode == MARK_MODE_MOUSE_BLOCK ||
							mark_mode == MARK_MODE_MOUSE_COLUMNS)
						ref_col = mouse_col;
					else if (mark_mode == MARK_MODE_BLOCK)
						ref_col = vertical_cursor_column;
					else
						ref_col = -1;

					if (ref_row != -1)
					{
						if (ref_row > mark_mode_start_row)
						{
							scrdesc.selected_first_row = mark_mode_start_row;
							scrdesc.selected_rows = ref_row - mark_mode_start_row + 1;
						}
						else
						{
							scrdesc.selected_first_row = ref_row;
							scrdesc.selected_rows = mark_mode_start_row - ref_row + 1;
						}
					}

					if (ref_col != -1)
					{
						int		xmin, xmax;

						if (ref_col > mark_mode_start_col)
						{
							xmin = desc.cranges[mark_mode_start_col - 1].xmin;
							xmax = desc.cranges[ref_col - 1].xmax;
						}
						else
						{
							xmax = desc.cranges[mark_mode_start_col - 1].xmax;
							xmin = desc.cranges[ref_col - 1].xmin;
						}

						scrdesc.selected_first_column = xmin;
						scrdesc.selected_columns = xmax - xmin + 1;
					}
				}

				if (scrdesc.selected_first_column != -1)
				{
					selected_xmin = scrdesc.selected_first_column;
					selected_xmax = selected_xmin + scrdesc.selected_columns - 1;
				}

				/*
				 * fix of unwanted visual artefact on an border between
				 * fix_cols window and row window, because there are
				 * overlap of columns on vertical column decoration.
				 */
				if (selected_xmin == scrdesc.fix_cols_cols - 1 &&
						selected_xmax < scrdesc.fix_cols_cols + cursor_col)
					selected_xmin += 1;

				else if (selected_xmin >= scrdesc.fix_cols_cols && 
						 selected_xmin < scrdesc.fix_cols_cols + cursor_col &&
						 selected_xmax >= scrdesc.fix_cols_cols + cursor_col)
					selected_xmin = scrdesc.fix_cols_cols - 1;

#ifdef DEBUG_PIPE

				current_time(&start_draw_sec, &start_draw_ms);

#endif

				window_fill(WINDOW_LUC,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							0,
							-1,
							vcursor_xmin_fix, vcursor_xmax_fix,
							selected_xmin, selected_xmax,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWS,
							first_data_row + first_row - fix_rows_offset,
							scrdesc.fix_cols_cols + cursor_col,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_data, vcursor_xmax_data,
							selected_xmin, selected_xmax,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_COLS,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							vcursor_xmin_fix, vcursor_xmax_fix,
							selected_xmin, selected_xmax,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FIX_ROWS,
							desc.title_rows + desc.fixed_rows - scrdesc.fix_rows_rows,
							scrdesc.fix_cols_cols + cursor_col,
							-1,
							vcursor_xmin_data, vcursor_xmax_data,
							selected_xmin, selected_xmax,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_FOOTER,
							first_data_row + first_row + scrdesc.rows_rows - fix_rows_offset,
							footer_cursor_col,
							cursor_row - first_row - scrdesc.rows_rows + fix_rows_offset,
							-1, -1, -1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM_LUC,
							0,
							0,
							0,
							-1, -1, -1, -1,
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_ROWNUM,
							first_data_row + first_row - fix_rows_offset,
							0,
							cursor_row - first_row + fix_rows_offset,
							-1, -1, -1, -1, 
							&desc, &scrdesc, &opts);

				window_fill(WINDOW_VSCROLLBAR,
							0, 0, cursor_row, -1, -1, -1, -1,
							&desc, &scrdesc, &opts);

				for (i = 0; i < PSPG_WINDOW_COUNT; i++)
				{
					if (i != WINDOW_TOP_BAR &&
						i != WINDOW_BOTTOM_BAR)
					{
						if (scrdesc.wins[i])
							wnoutrefresh(scrdesc.wins[i]);
					}
				}

#ifdef DEBUG_PIPE

				print_duration(start_draw_sec, start_draw_ms, "draw time");

#endif

			} /* !no_doupdate */

#ifdef COMPILE_MENU

			if (cmdbar)
				st_cmdbar_post(cmdbar);

			if (menu != NULL && menu_is_active)
			{
				st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
				st_menu_post(menu);
			}
			else if (opts.menu_always)
				st_menu_post(menu);

#endif

			if (no_doupdate)
				no_doupdate = false;
			else if (next_command == 0 || current_state->fmt != NULL)
			{
				time_t		current_sec;
				long		current_ms;

#ifdef DEBUG_PIPE

				time_t	start_doupdate_sec;
				long	start_doupdate_ms;

				current_time(&start_doupdate_sec, &start_doupdate_ms);

#endif

				if (!opts.no_sleep &&
					!skip_update_after_mouse_event)
				{
					current_time(&current_sec, &current_ms);

					/*
					 * We don't want do UPDATE too quickly.
					 */
					if (

#ifdef COMPILE_MENU

					!menu_is_active &&

#endif

						last_doupdate_sec != -1 &&
						!opts.no_mouse &&
						!(mark_mode == MARK_MODE_MOUSE ||
						 mark_mode == MARK_MODE_MOUSE_BLOCK ||
						 mark_mode == MARK_MODE_MOUSE_COLUMNS))
					{
						long	td = time_diff(current_sec, current_ms,
											   last_doupdate_sec, last_doupdate_ms);

						if (td < 15)
							usleep((15 - td) * 1000);
					}
				}

				if (!skip_update_after_mouse_event)
				{
					doupdate();

					current_time(&current_sec, &current_ms);

					last_doupdate_sec = current_sec;
					last_doupdate_ms = current_ms;


#ifdef DEBUG_PIPE

					print_duration(start_doupdate_sec, start_doupdate_ms, "doupdate");

#endif

				}
				else
					usleep(2 * 1000);

				current_time(&current_sec, &current_ms);

				last_doupdate_sec = current_sec;
				last_doupdate_ms = current_ms;


			}

#ifdef DEBUG_PIPE

				if (first_doupdate)
				{
					first_doupdate = false;
					print_duration(start_app_sec, start_app_ms, "first view");
				}

#endif

			if (current_state->fmt != NULL)
			{
				show_info_wait(current_state->fmt, current_state->par, current_state->beep,
							   false, current_state->applytimeout,
							   current_state->is_error);

				free(current_state->fmt);
				free(current_state->par);

				current_state->fmt = NULL;
				current_state->par = NULL;

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

				event_keycode = get_event(&event,
										  &press_alt,
										  &got_sigint,
										  &handle_timeout,
										  &handle_file_event,
										  &reopen_file,
										  opts.watch_time > 0 ? 1000 : -1,
										  state.hold_stream);

				/*
				 * Immediately clean mouse state attributes when event is not
				 * mouse event.
				 */
				if (event_keycode != KEY_MOUSE)
				{
					mouse_row = -1;
					mouse_col = -1;
				}

				/* Disable mark cursor mode immediately */
				if (mark_mode == MARK_MODE_CURSOR &&
						!(event_keycode == KEY_SF ||
						  event_keycode == KEY_SR ||
						  event_keycode == KEY_SNEXT ||
						  event_keycode == KEY_SPREVIOUS ||
						  event_keycode == KEY_LEFT ||
						  event_keycode == KEY_RIGHT ||
						  is_cmd_RowNumToggle(event_keycode, press_alt)))
					mark_mode = MARK_MODE_NONE;

				/* Disable mark mouse mode immediately */
				if ((mark_mode == MARK_MODE_MOUSE ||
						 mark_mode == MARK_MODE_MOUSE_BLOCK ||
						 mark_mode == MARK_MODE_MOUSE_COLUMNS) &&
						event_keycode != KEY_MOUSE)
					mark_mode = MARK_MODE_NONE;

				if (force_refresh ||
					opts.watch_time ||
					((opts.watch_file || state.stream_mode) && handle_file_event))
				{
					long	ms;
					time_t	sec;
					long	ct;

					current_time(&sec, &ms);
					ct = sec * 1000 + ms;

					if (force_refresh ||
						(ct > next_watch && !paused) ||
						((opts.watch_file || state.stream_mode) && handle_file_event))
					{
						DataDesc	desc2;
						bool		fresh_data = false;

						memset(&desc2, 0, sizeof(desc2));

						if (state.pathname[0])
						{
							if (state.fp)
							{
								/* should be strem mode */
								if (reopen_file)
								{
									fclose(state.fp);
									state.fp = NULL;

									fresh_data = open_data_file(&opts, &state);
								}
								else
								{
									clearerr(state.fp);
									fresh_data = true;
								}
							}
							else
								fresh_data = open_data_file(&opts, &state);
						}
						else if (opts.query)
							fresh_data = true;
						else
							/*
							 * When we have a stream mode without watch file,
							 * then we can read more times from stdin.
							 */
							fresh_data = state.stream_mode;

						/* when we wanted fresh data */
						if (fresh_data)
						{
							if (opts.csv_format || opts.tsv_format || opts.query)
								/* returns false when format is broken */
								fresh_data = read_and_format(&opts, &desc2, &state);
							else if (opts.querystream)
							{
								fresh_data = readfile(&opts, &desc2, &state);
								if (fresh_data)
									fresh_data = read_and_format(&opts, &desc2, &state);
							}
							else
								fresh_data = readfile(&opts, &desc2, &state);

							if (!state.stream_mode && state.fp)
							{
								fclose(state.fp);
								state.fp = NULL;
							}
						}

						/* when we have fresh data */
						if (fresh_data)
						{
							int		max_cursor_row;
							ScrDesc		aux;

							DataDescFree(&desc);
							memcpy(&desc, &desc2, sizeof(desc));

							if (desc.headline)
								(void) translate_headline(&desc);

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

							refresh_aux_windows(&opts, &scrdesc);

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
								update_order_map(&scrdesc, &desc, last_ordered_column, last_order_desc);
						}
						else
							DataDescFree(&desc2);

						if ((ct - next_watch) < (opts.watch_time * 1000))
							next_watch = next_watch + 1000 * opts.watch_time;
						else
							next_watch = ct + 100 * opts.watch_time;
						/*
						 * Force refresh, only when we got fresh data or when
						 * this event was forced by timer.
						 */
						if (fresh_data || opts.watch_time > 0 || state._errno != 0)
						{
							clear();
							refresh_scr = true;
						}

						handle_timeout = false;
					}

					print_status(&opts, &scrdesc, &desc, cursor_row, cursor_col, first_row, fix_rows_offset, vertical_cursor_column);
					if (scrdesc.wins[WINDOW_TOP_BAR])
						wnoutrefresh(scrdesc.wins[WINDOW_TOP_BAR]);

					set_scrollbar(&scrdesc, &desc, first_row);

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
				  (*scrdesc.searchterm || *scrdesc.searchcolterm ||
				   scrdesc.selected_first_row != -1 ||
				   scrdesc.selected_first_column != -1))
			{
				throw_searching(&scrdesc, &desc);
				throw_selection(&scrdesc, &desc, &mark_mode);
			}
			else
			{
				if (opts.on_sigint_exit)
					break;
				else
					show_info_wait(" For quit press \"q\" (or use on-sigint-exit option).",
								   NULL, true, true, true, false);
			}
		}
		else if ((event_keycode == ERR || event_keycode == KEY_F(10)) && !redirect_mode)
		{

			log_row("exit main loop: %s", event_keycode == ERR ? "input error" : "F10");
			break;
		}

#ifndef COMPILE_MENU

		if (!redirect_mode)
		{
			translated_command_history = translated_command;
			command = translate_event(event_keycode, press_alt, &opts, &nested_command);
			translated_command = command;
		}

#else

		/*
		 * Don't send RESIZE to menu. It cannot to handle this event, and it
		 * cannot to translate this event. This event is lost in menu.
		 * So, don't do it. Don't send mouse event to menu, if mouse is
		 * actively used for scrollbar or range marking.
		 */
		if (!redirect_mode && event_keycode != KEY_RESIZE &&
			!(event_keycode == KEY_MOUSE &&
			  (scrdesc.scrollbar_mode ||
							 mark_mode == MARK_MODE_MOUSE ||
							 mark_mode == MARK_MODE_MOUSE_BLOCK ||
							 mark_mode == MARK_MODE_MOUSE_COLUMNS)))
		{
			bool	processed = false;
			bool	activated = false;
			ST_MENU_ITEM		*ami;
			ST_CMDBAR_ITEM		*aci;

			processed = st_menu_driver(menu, event_keycode, press_alt, &event);
			if (processed)
			{
				/*
				 * Read info from pull down menu
				 */
				ami = st_menu_selected_item(&activated);
				if (activated)
				{
					next_command = ami->code;

					/*
					 * Because pspg has not dialogs and use just menu, we use
					 * menu items like options. For export options we want to
					 * hold menu unclosed until command item is selected. So
					 * next switch detects menu's id related to options and for
					 * these items, the pull down menu stays visible.
					 */
					switch (next_command)
					{
						case cmd_UseClipboard_CSV:
						case cmd_UseClipboard_TSVC:
						case cmd_UseClipboard_text:
						case cmd_UseClipboard_INSERT:
						case cmd_UseClipboard_INSERT_with_comments:
						case cmd_UseClipboard_SQL_values:
						case cmd_UseClipboard_pipe_separated:
						case cmd_SetCopyFile:
						case cmd_SetCopyClipboard:
						case cmd_TogleEmptyStringIsNULL:
						case cmd_SetOwnNULLString:
							/* do nothing */
							break;

						default:
							goto hide_menu;
					}
				}

				/*
				 * Read info from bottom command bar
				 */
				if (!activated)
				{
					aci = st_menu_selected_command(&activated);
					if (activated)
					{
						next_command = aci->code;
						goto refresh;
					}
				}
			}

			if (processed &&
					opts.menu_always &&
					st_menu_get_focus(menu) == ST_MENU_FOCUS_FULL)
				menu_is_active = true;

			if (menu_is_active && !processed &&
					(event_keycode == ST_MENU_ESCAPE || event_keycode == KEY_MOUSE))
			{
hide_menu:

				st_menu_unpost(menu, true);
				menu_is_active = false;

				if (!opts.menu_always)
				{
					st_menu_set_focus(menu, ST_MENU_FOCUS_NONE);
				}
				else
				{
					st_menu_set_focus(menu, ST_MENU_FOCUS_MOUSE_ONLY);
					st_menu_post(menu);
				}

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
				command = translate_event(event_keycode, press_alt, &opts, &nested_command);
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
				command = translate_event(event_keycode, press_alt, &opts, &nested_command);
				translated_command = command;
			}
		}

#endif

		prev_first_row = first_row;

		log_row("process command: %s", cmd_string(command));

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
			/* same like sigint handling */
			if (!opts.no_sigint_search_reset &&
				  (*scrdesc.searchterm || *scrdesc.searchcolterm ||
				   scrdesc.selected_first_row != -1 ||
				   scrdesc.selected_first_column != -1))
			{
				throw_searching(&scrdesc, &desc);
				throw_selection(&scrdesc, &desc, &mark_mode);
			}
			else
			{
				if (opts.on_sigint_exit)
					break;
				else
					show_info_wait(" For quit press \"q\" (or use on-sigint-exit option).",
								   NULL, true, true, true, false);
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
						menu = init_menu(menu, &opts);
						log_row("init menu");
					}

					st_menu_set_focus(menu, ST_MENU_FOCUS_FULL);
					post_menu(&opts, menu);

					menu_is_active = true;
					continue;
				}

#else

			case cmd_ShowMenu:
				reuse_event = false;
				break;

#endif

			case cmd_TogleEmptyStringIsNULL:
				opts.empty_string_is_null = !opts.empty_string_is_null;

#ifdef COMPILE_MENU

				st_menu_set_option(menu,
								   cmd_TogleEmptyStringIsNULL,
								   ST_MENU_OPTION_MARKED,
								   opts.empty_string_is_null);

				if (opts.empty_string_is_null)
						st_menu_set_option(menu,
								   cmd_SetOwnNULLString,
								   ST_MENU_OPTION_MARKED,
								   false);

#endif

				break;

			case cmd_SetOwnNULLString:
				{
					char	nullstr[256];
					bool	is_valid;

					if (last_nullstr[0] == '\0')
					{
						if (opts.nullstr)
							strncpy(last_nullstr, opts.nullstr, sizeof(256));
					}

					is_valid = get_string("nullstr: ", nullstr, sizeof(nullstr) - 1, last_nullstr, 'u');
					if (nullstr[0] != '\0')
					{
						free(opts.nullstr);
						opts.nullstr = sstrdup(nullstr);
						opts.empty_string_is_null = false;
					}
					else if (is_valid)
						opts.empty_string_is_null = true;

#ifdef COMPILE_MENU

					if (nullstr[0] != '\0')
					{
						st_menu_set_option(menu,
								   cmd_SetOwnNULLString,
								   ST_MENU_OPTION_MARKED,
								   true);

						st_menu_set_option(menu,
								   cmd_TogleEmptyStringIsNULL,
								   ST_MENU_OPTION_MARKED,
								   false);
					}
					else
					{
						st_menu_set_option(menu,
								   cmd_SetOwnNULLString,
								   ST_MENU_OPTION_MARKED,
								   false);

						st_menu_set_option(menu,
								   cmd_TogleEmptyStringIsNULL,
								   ST_MENU_OPTION_MARKED,
								   opts.empty_string_is_null);
					}

#endif

					break;
				}

			case cmd_SetCopyFile:
				opts.copy_target = COPY_TARGET_FILE;

#ifdef COMPILE_MENU

				refresh_copy_target_options(&opts, menu);

#endif
				break;

			case cmd_SetCopyClipboard:
				opts.copy_target = COPY_TARGET_CLIPBOARD;

#ifdef COMPILE_MENU

				refresh_copy_target_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_CSV:
				opts.clipboard_format = CLIPBOARD_FORMAT_CSV;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_TSVC:
				opts.clipboard_format = CLIPBOARD_FORMAT_TSVC;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_SQL_values:
				opts.clipboard_format = CLIPBOARD_FORMAT_SQL_VALUES;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_pipe_separated:
				opts.clipboard_format = CLIPBOARD_FORMAT_PIPE_SEPARATED;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_text:
				opts.clipboard_format = CLIPBOARD_FORMAT_TEXT;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_INSERT:
				opts.clipboard_format = CLIPBOARD_FORMAT_INSERT;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

			case cmd_UseClipboard_INSERT_with_comments:
				opts.clipboard_format = CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS;

#ifdef COMPILE_MENU

				refresh_clipboard_options(&opts, menu);

#endif

				break;

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
				throw_searching(&scrdesc, &desc);
				break;

			case cmd_ShowTopBar:
				opts.no_topbar = !opts.no_topbar;
				refresh_scr = true;
				break;

#ifdef COMPILE_MENU

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

#endif

			case cmd_ShowScrollbar:
				opts.show_scrollbar = !opts.show_scrollbar;
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
				quiet_mode = !quiet_mode;
				break;

			case cmd_SaveSetup:
				if (!save_config(tilde(NULL, PSPG_CONF), &opts))
				{
					if (errno != 0)
					{
						char	buffer1[1000];

						snprintf(buffer1, 1000, "Cannot write to \"%.800s\" (%s)",
								PSPG_CONF, strerror(errno));

						show_info_wait(buffer1, strerror(errno), true, true, false, true);
					}
					else
						show_info_wait(" Cannot write to \"%s\"", PSPG_CONF,
									   true, true, false, true);
				}
				else
					show_info_wait(" Setup saved to \"%s\"", PSPG_CONF,
								   true, true, true, false);
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
			case cmd_SetTheme_Flatwhite:
			case cmd_SetTheme_RelationalPipes:
			case cmd_SetTheme_PaperColor:
				long_argument = cmd_get_theme(command);
				long_argument_is_valid = true;
				next_command = cmd_SetTheme;
				break;

			case cmd_SetTheme:
				{
					int		theme_num = -1;

					if (long_argument_is_valid)
					{
						theme_num = (int) long_argument;
						long_argument_is_valid = false;
					}
					else
					{
						char	theme_num_str[256];

						get_string("theme number: ", theme_num_str, sizeof(theme_num_str) - 1, last_line, 'u');
						if (theme_num_str[0] != '\0')
						{
							char   *endptr;

							errno = 0;
							theme_num = strtol(theme_num_str, &endptr, 10);

							if (endptr == theme_num_str)
							{
								show_info_wait(" Cannot convert input string to number",
											   NULL, true, true, false, true);
								break;
							}
							else if (errno != 0)
							{
								show_info_wait(" Cannot convert input string to number (%s)",
											   strerror(errno), true, true, false, true);
								break;
							}
						}
						else
							break;
					}

					if (theme_num != -1)
					{
						if (theme_num < 0 || theme_num > MAX_STYLE)
						{
							char	buffer[256];

							sprintf(buffer, "only color schemas 0..%d are supported", MAX_STYLE);

							show_info_wait(buffer, NULL, true, true, false, true);
							break;
						}

						opts.theme = theme_num;
						reinit = true;
						goto reinit_theme;
					}

					break;
				}

			case cmd_BoldLabelsToggle:
				opts.bold_labels = !opts.bold_labels;
				reinit = true;
				goto reinit_theme;

			case cmd_BoldCursorToggle:
				opts.bold_cursor = !opts.bold_cursor;
				reinit = true;
				goto reinit_theme;

			case cmd_Mark:
				if (mark_mode != MARK_MODE_ROWS &&
					mark_mode != MARK_MODE_BLOCK)
				{
					throw_selection(&scrdesc, &desc, &mark_mode);

					mark_mode = MARK_MODE_ROWS;
					mark_mode_start_row = cursor_row;
				}
				else
					mark_mode = MARK_MODE_NONE;
				break;

			case cmd_MarkColumn:
				{
					if (!check_visible_vertical_cursor(&desc, &opts,
													   vertical_cursor_column))
						break;

					if (mark_mode != MARK_MODE_BLOCK)
					{
						throw_selection(&scrdesc, &desc, &mark_mode);

						mark_mode = MARK_MODE_BLOCK;
						mark_mode_start_row = cursor_row;
						mark_mode_start_col = vertical_cursor_column;
					}
					else
						mark_mode = MARK_MODE_NONE;
				}

				break;

			case cmd_Mark_NestedCursorCommand:
				if (mark_mode != MARK_MODE_CURSOR)
				{
					throw_selection(&scrdesc, &desc, &mark_mode);

					mark_mode = MARK_MODE_CURSOR;
					mark_mode_start_row = cursor_row;
				}
				next_command = nested_command;
				break;

			case cmd_Unmark:
				throw_selection(&scrdesc, &desc, &mark_mode);
				break;

			case cmd_MarkAll:
				throw_selection(&scrdesc, &desc, &mark_mode);

				mark_mode = MARK_MODE_NONE;
				scrdesc.selected_first_row = 0;
				scrdesc.selected_rows = MAX_CURSOR_ROW + 1;
				scrdesc.selected_first_column = -1;
				scrdesc.selected_columns = -1;
				break;

			case cmd_MouseToggle:
				{
					if (!opts.no_mouse)
					{
						mousemask(0, &prev_mousemask);
						opts.no_mouse = true;

						(void) disable_xterm_mouse_mode();
					}
					else
					{
						if (!mouse_was_initialized)
						{
							mouseinterval(0);

#if NCURSES_MOUSE_VERSION > 1


							mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED |
									  BUTTON4_PRESSED | BUTTON5_PRESSED |
									  BUTTON_ALT | BUTTON_CTRL |
									  (opts.xterm_mouse_mode ? REPORT_MOUSE_POSITION : 0),
									  NULL);

#else

							mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED, NULL);

#endif

							mouse_was_initialized = true;
						}
						else
							mousemask(prev_mousemask, NULL);

						enable_xterm_mouse_mode(opts.xterm_mouse_mode);

						opts.no_mouse= false;
					}

					show_info_wait(" mouse handling: %s ",
								   opts.no_mouse ? "off" : "on", false, true, true, false);
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
						show_info_wait(" Vertical cursor is available only for tables.",
									   NULL, true, true, true, false);
						break;
					}

					opts.vertical_cursor = !opts.vertical_cursor;

					if (opts.vertical_cursor)
					{
						int		xpoint;
						int		prev_command = translated_command_history;
						int			i;

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
					SimpleLineBufferIter slbi, *_slbi;

					_slbi = init_slbi_ddesc(&slbi, &desc);

					while (_slbi)
					{
						LineInfo *linfo;

						_slbi = slbi_get_line_next(_slbi, NULL, &linfo);

						if (linfo && (linfo->mask & LINEINFO_BOOKMARK))
							linfo->mask ^= LINEINFO_BOOKMARK;
					}
				}
				break;

			case cmd_ToggleBookmark:
				{
					LineBufferMark lbm;
					int		_cursor_row;

					if (mouse_row != -1)
					{
						_cursor_row = mouse_row + CURSOR_ROW_OFFSET;
						mouse_row = -1;
						mouse_col = -1;
					}
					else
						_cursor_row = cursor_row + CURSOR_ROW_OFFSET;

					if (ddesc_set_mark(&lbm, &desc, _cursor_row))
						lbm_xor_mask(&lbm, LINEINFO_BOOKMARK);
				}
				break;

			case cmd_PrevBookmark:
				{
					int		lineno;
					bool	found = false;

					/* start from previous line before cursor */
					lineno = cursor_row + CURSOR_ROW_OFFSET - 1;

					if (lineno > 0)
					{
						LineBufferIter lbi;
						LineInfo *linfo;

						init_lbi_ddesc(&lbi, &desc, lineno);

						while (lbi_get_line_prev(&lbi, NULL, &linfo, &lineno))
						{
							if (linfo && linfo->mask & LINEINFO_BOOKMARK)
							{
								cursor_row = lineno - CURSOR_ROW_OFFSET;
								if (cursor_row < first_row)
									first_row = cursor_row;

								found = true;
								break;
							}
						}
					}

					if (!found)
						make_beep();
				}
				break;

			case cmd_NextBookmark:
				{
					LineBufferIter lbi;
					LineInfo *linfo;
					int		lineno;
					bool	found = false;

					/* start after (next line) cursor line */
					lineno = cursor_row + CURSOR_ROW_OFFSET + 1;

					init_lbi_ddesc(&lbi, &desc, lineno);

					while (lbi_get_line_next(&lbi, NULL, &linfo, &lineno))
					{
						if (linfo && linfo->mask & LINEINFO_BOOKMARK)
						{
							cursor_row = lineno - CURSOR_ROW_OFFSET;

							if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
								first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

							first_row = adjust_first_row(first_row, &desc, &scrdesc);
							found = true;
							break;
						}
					}

					if (!found)
						make_beep();
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
						make_beep();

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
						make_beep();
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
						int		prev_vertical_cursor_column = vertical_cursor_column;

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
								char   *str = &desc.headline_transl[scrdesc.fix_cols_cols + cursor_col];
								int		i;

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

							if (prev_vertical_cursor_column == fixed_columns &&
								vertical_cursor_column == fixed_columns + 1)
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
						make_beep();
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
						make_beep();
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
					int max_cursor_row;
					long lineno;

					if (long_argument_is_valid)
					{
						lineno = long_argument;
						long_argument_is_valid = false;
					}
					else
					{
						char	linenotxt[256];

						get_string("line: ", linenotxt, sizeof(linenotxt) - 1, last_line, 'u');
						if (linenotxt[0] != '\0')
						{
							char   *endptr;

							errno = 0;
							lineno = strtol(linenotxt, &endptr, 10);

							if (endptr == linenotxt)
							{
								show_info_wait(" Cannot convert input string to number",
											   NULL, true, true, false, true);
								break;
							}
							else if (errno != 0)
							{
								show_info_wait(" Cannot convert input string to number (%s)",
											   strerror(errno), true, true, false, true);
								break;
							}
						}
						else
							break;
					}

					if (lineno < 0)
						lineno = MAX_CURSOR_ROW + lineno + 2;

					cursor_row = lineno - 1;
					if (cursor_row < 0)
						cursor_row = 0;

					max_cursor_row = MAX_CURSOR_ROW;
  					if (cursor_row > max_cursor_row)
					{
						cursor_row = max_cursor_row;
						make_beep();
					}

					if (cursor_row < first_row || cursor_row - first_row > VISIBLE_DATA_ROWS)
					{
						first_row = cursor_row - VISIBLE_DATA_ROWS / 2;
						first_row = adjust_first_row(first_row, &desc, &scrdesc);
					}

					snprintf(last_line, sizeof(last_line), "%ld", lineno);
					break;
				}

			case cmd_GotoLineRel:
				{
					if (long_argument_is_valid)
					{
						long_argument = long_argument + cursor_row + 1;
						next_command = cmd_GotoLine;
					}
					else
						show_info_wait(" Internal error - expected valid internal long argument",
									   NULL, true, true, false, true);

					break;
				}

			case cmd_OriginalSort:
				if (desc.order_map)
				{
					free(desc.order_map);
					desc.order_map = NULL;
					last_ordered_column = -1;

					throw_selection(&scrdesc, &desc, &mark_mode);
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
					int		sortedby_colno;

					if (long_argument_is_valid)
					{
						sortedby_colno = (int) long_argument;
						long_argument_is_valid = false;
					}
					else
					{
						if (!check_visible_vertical_cursor(&desc, &opts, vertical_cursor_column))
							break;

						sortedby_colno = vertical_cursor_column;
					}

					update_order_map(&scrdesc,
									 &desc,
									 sortedby_colno,
									 command == cmd_SortDesc);

					last_ordered_column = sortedby_colno;
					last_order_desc = command == cmd_SortDesc;

					throw_selection(&scrdesc, &desc, &mark_mode);

					break;
				}

			case cmd_SaveData:
				{
					export_to_file(cmd_SaveData,
								   CLIPBOARD_FORMAT_TEXT,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_SaveAsCSV:
				{
					export_to_file(cmd_SaveAsCSV,
								   CLIPBOARD_FORMAT_CSV,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_Copy:
				{
					export_to_file(cmd_Copy,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   cursor_row, vertical_cursor_column,
								   0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyLine:
				{
					export_to_file(cmd_CopyLine,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   cursor_row, 0,
								   0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyLineExtended:
				{
					ClipboardFormat fmt;

					if (DSV_FORMAT_TYPE(opts.clipboard_format))
						fmt = opts.clipboard_format;
					else
						fmt = CLIPBOARD_FORMAT_CSV;

					export_to_file(cmd_CopyLineExtended,
								   fmt,
								   &opts, &scrdesc, &desc,
								   cursor_row, 0,
								   0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyColumn:
				{
					export_to_file(cmd_CopyColumn,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, vertical_cursor_column,
								   0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopySelected:
				{
					export_to_file(cmd_CopySelected,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   cursor_row, 0,
								   0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyAllLines:
				{
					export_to_file(cmd_CopyAllLines,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyTopLines:
				{
					export_to_file(cmd_CopyTopLines,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyBottomLines:
				{
					export_to_file(cmd_CopyBottomLines,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopyMarkedLines:
				{
					export_to_file(cmd_CopyMarkedLines,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_CopySearchedLines:
				{
					export_to_file(cmd_CopySearchedLines,
								   opts.clipboard_format,
								   &opts, &scrdesc, &desc,
								   0, 0, 0, 0.0, NULL,
								   &refresh_clear);
					break;
				}

			case cmd_ForwardSearch:
				{
					char	locsearchterm[256];
					bool	isSelSearch;

					search_direction = SEARCH_FORWARD;
					isSelSearch = scrdesc.search_rows > 0 || scrdesc.search_columns > 0;

					if (string_argument_is_valid)
					{
						strncpy(locsearchterm, string_argument, sizeof(locsearchterm) - 1);
						locsearchterm[sizeof(locsearchterm) - 1] = '\0';
						memcpy(last_row_search, locsearchterm, sizeof(last_row_search));

						free(string_argument);
						string_argument = NULL;
						string_argument_is_valid = false;
					}
					else
						get_string(isSelSearch ? "^/" : "/",
								   locsearchterm, sizeof(locsearchterm) - 1,
								   (opts.last_row_search ? last_row_search : NULL),
								   'u');

					/*
					 * If we don't automatically use the last pattern, use the
					 * last pattern if no pattern was provided.
					 */
					if (!opts.last_row_search)
					{
						if (locsearchterm[0] != '\0')
							memcpy(last_row_search, locsearchterm, sizeof(last_row_search));
						else if (last_row_search[0] != '\0')
							memcpy(locsearchterm, last_row_search, sizeof(last_row_search));
					}

					if (locsearchterm[0] != '\0')
					{
						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = use_utf8 ?  utf8len(scrdesc.searchterm) : (int) strlen(scrdesc.searchterm);

						search_direction = SEARCH_FORWARD;

						reset_searching_lineinfo(&desc);

						/* continue to find next: */
						next_command = cmd_SearchNext;
					}
					else
						throw_searching(&scrdesc, &desc);

					break;
				}

			case cmd_SearchNext:
				{
					LineBufferIter lbi;
					int		lineno;
					char   *line;
					int		skip_bytes = 0;

					if (!*scrdesc.searchterm)
						break;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchNext && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchPrev;
						break;
					}

					lineno = cursor_row + CURSOR_ROW_OFFSET;

					if (scrdesc.found && lineno == scrdesc.found_row)
						skip_bytes = scrdesc.found_start_bytes + scrdesc.searchterm_size;

					scrdesc.found = false;

					init_lbi_ddesc(&lbi, &desc, lineno);

					while (lbi_get_line_next(&lbi, &line, NULL, &lineno))
					{
						const char   *pttrn;

						if (scrdesc.search_rows > 0)
						{
							if (lineno - CURSOR_ROW_OFFSET < scrdesc.search_first_row ||
								lineno - CURSOR_ROW_OFFSET >= scrdesc.search_first_row + scrdesc.search_rows)
							{
								continue;
							}
						}

						pttrn = pspg_search(&opts, &scrdesc, line + skip_bytes);
						while (pttrn)
						{
							/* apply column selection filtr */
							if (scrdesc.search_columns > 0)
							{
								int		bytes = pttrn - line;
								int		pos = use_utf8 ? utf_string_dsplen(line, bytes) : bytes;

								if (pos < scrdesc.search_first_column)
								{
									pttrn += charlen(pttrn);
									pttrn = pspg_search(&opts, &scrdesc, pttrn);

									continue;
								}

								if (pos > scrdesc.search_first_column + scrdesc.search_columns - 1)
									pttrn = NULL;
							}

							break;
						}

						if (pttrn)
						{
							int		found_start_bytes = pttrn - line;

							scrdesc.found_start_x =
								use_utf8 ? utf_string_dsplen(line, found_start_bytes) : (int) (found_start_bytes);

							scrdesc.found_start_bytes = found_start_bytes;
							scrdesc.found_row = lineno;

							fresh_found = true;
							fresh_found_cursor_col = -1;

							cursor_row = lineno - CURSOR_ROW_OFFSET;

							if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
								first_row = cursor_row - VISIBLE_DATA_ROWS + 1;

							first_row = adjust_first_row(first_row, &desc, &scrdesc);

							scrdesc.found = true;
							break;
						}

						skip_bytes = 0;
					}

					if (!scrdesc.found)
						show_info_wait(" Not found", NULL, true, true, false, false);
					break;
				}

			case cmd_BackwardSearch:
				{
					char	locsearchterm[256];
					bool	isSelSearch;

					search_direction = SEARCH_BACKWARD;
					isSelSearch = scrdesc.search_rows > 0 || scrdesc.search_columns > 0;

					if (string_argument_is_valid)
					{
						strncpy(locsearchterm, string_argument, sizeof(locsearchterm) - 1);
						locsearchterm[sizeof(locsearchterm) - 1] = '\0';
						memcpy(last_row_search, locsearchterm, sizeof(last_row_search));

						free(string_argument);
						string_argument = NULL;
						string_argument_is_valid = false;
					}
					else
						get_string(isSelSearch ? "^?" : "?",
								   locsearchterm, sizeof(locsearchterm) - 1,
								   (opts.last_row_search ? last_row_search : NULL),
								   'u');

					reset_searching_lineinfo(&desc);

					/*
					 * If we don't automatically use the last pattern, use the
					 * last pattern if no pattern was provided.
					 */
					if (!opts.last_row_search)
					{
						if (locsearchterm[0] != '\0')
							memcpy(last_row_search, locsearchterm, sizeof(last_row_search));
						else if (last_row_search[0] != '\0')
							memcpy(locsearchterm, last_row_search, sizeof(last_row_search));
					}

					if (locsearchterm[0] != '\0')
					{

						strncpy(scrdesc.searchterm, locsearchterm, sizeof(scrdesc.searchterm));
						scrdesc.has_upperchr = has_upperchr(scrdesc.searchterm);
						scrdesc.searchterm_size = strlen(scrdesc.searchterm);
						scrdesc.searchterm_char_size = utf8len(scrdesc.searchterm);

						reset_searching_lineinfo(&desc);

						/* continue to find next: */
						next_command = cmd_SearchPrev;
					}
					else
						throw_searching(&scrdesc, &desc);

					break;
				}

			case cmd_SearchPrev:
				{
					LineBufferIter lbi;
					int		lineno;
					char   *line, *_line;
					int		cut_bytes = 0;

					if (!*scrdesc.searchterm)
						break;

					/* call inverse command when search direction is SEARCH_BACKWARD */
					if (command == cmd_SearchPrev && search_direction == SEARCH_BACKWARD && !redirect_mode)
					{
						next_command = cmd_SearchNext;
						break;
					}

					lineno = cursor_row + CURSOR_ROW_OFFSET;

					/*
					 * when we can search on found line, the use it,
					 * else try start searching from previous row.
					 */
					if (scrdesc.found && lineno == scrdesc.found_row &&
							scrdesc.found_start_bytes > 0)
						cut_bytes = scrdesc.found_start_bytes;
					else
						lineno -= 1;

					scrdesc.found = false;

					init_lbi_ddesc(&lbi, &desc, lineno);

					while (lbi_get_line_prev(&lbi, &line, NULL, &lineno))
					{
						const char   *ptr;
						const char   *most_right_pttrn = NULL;

						/* inside table don't try search below first data row */
						if (desc.headline_transl)
						{
							if (lineno < desc.first_data_row)
								break;
						}

						if (scrdesc.search_rows > 0)
						{
							if (lineno - CURSOR_ROW_OFFSET < scrdesc.search_first_row ||
								lineno - CURSOR_ROW_OFFSET >= scrdesc.search_first_row + scrdesc.search_rows)
							{
								continue;
							}
						}

						_line = cut_bytes > 0 ? sstrndup(line, cut_bytes) : line;
						ptr = _line;

						/* try to find most right pattern */
						while (ptr)
						{
							ptr = pspg_search(&opts, &scrdesc, ptr);

							if (ptr)
							{
								/* apply column selection filtr */
								if (scrdesc.search_columns > 0)
								{
									int		bytes = ptr - _line;
									int		pos = use_utf8 ? utf_string_dsplen(_line, bytes) : bytes;

									if (pos < scrdesc.search_first_column)
									{
										ptr += charlen(ptr);
										continue;
									}

									if (pos > scrdesc.search_first_column + scrdesc.search_columns - 1)
										break;
								}

								most_right_pttrn = ptr;
								ptr += scrdesc.searchterm_size;
							}
						}

						if (most_right_pttrn)
						{
							int		found_start_bytes = most_right_pttrn - _line;

							cursor_row = lineno - CURSOR_ROW_OFFSET;
							if (first_row > cursor_row)
								first_row = cursor_row;

							scrdesc.found_start_x =
								use_utf8 ? utf8len_start_stop(_line, most_right_pttrn) : (size_t) (found_start_bytes);

							scrdesc.found_start_bytes = found_start_bytes;
							scrdesc.found_row = lineno;

							fresh_found = true;
							fresh_found_cursor_col = -1;

							if (line != _line)
								free(_line);

							scrdesc.found = true;
							break;
						}

						if (line != _line)
							free(_line);

						cut_bytes = 0;
					}

					if (!scrdesc.found)
						show_info_wait(" Not found", NULL, true, true, false, false);

					break;
				}

			case cmd_ForwardSearchInSelection:
			case cmd_BackwardSearchInSelection:
				{
					if (scrdesc.selected_first_row == -1 &&
						scrdesc.selected_first_column == -1)
					{
						show_info_wait(" There are not selected area",
									   NULL, true, true, true, false);
						break;
					}

					next_command = command == cmd_ForwardSearchInSelection ? cmd_ForwardSearch : cmd_BackwardSearch;

					scrdesc.search_first_row = scrdesc.selected_first_row;
					scrdesc.search_rows = scrdesc.selected_rows;
					scrdesc.search_first_column = scrdesc.selected_first_column;
					scrdesc.search_columns = scrdesc.selected_columns;
					scrdesc.search_selected_mode = true;

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

						get_string("c:", locsearchterm, sizeof(locsearchterm) - 1, last_col_search, 'u');

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
								if (use_utf8)
								{
									if (utf8_nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_offset,
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
									if (nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_offset,
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
									if (nstrstr_with_sizes(desc.namesline + desc.cranges[colnum - 1].name_offset,
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
									show_info_wait(" Search from first column",
												   NULL, true, true, true, false);

								opts.vertical_cursor = true;
								vertical_cursor_column = colnum;

								cursor_col = get_cursor_col_for_vertical_column(vertical_cursor_column, cursor_col, &desc, &scrdesc);
								last_x_focus = get_x_focus(vertical_cursor_column, cursor_col, &desc, &scrdesc);
							}
							else
								show_info_wait(" Not found",
											   NULL, true, true, false, false);
						}
						else
							show_info_wait(" Search pattern is a empty string",
										   NULL, true, true, true, false);
					}
					else
						show_info_wait(" Columns names are not detected",
									   NULL, true, true, true, false);

					break;
				}

			case cmd_TogglePause:
				paused = !paused;
				break;

			case cmd_Refresh:
				refresh_clear = true;
				break;

			case cmd_BsCommand:
				{
					if (!cmdline_ptr || *cmdline_ptr == '\0')
					{
						/*
						 * When previous command batch is processed, read new
						 */
						get_string("", cmdline, sizeof(cmdline) - 1, NULL, 'c');
						cmdline_ptr = cmdline;
					}

					cmdline_ptr = parse_and_eval_bscommand(cmdline_ptr,
														   &opts, &scrdesc, &desc,
														   &next_command,
														   &long_argument, &long_argument_is_valid,
														   &string_argument, &string_argument_is_valid,
														   &refresh_clear);
					break;
				}

			case cmd_MOUSE_EVENT:
				{
					mouse_event += 1;

					/*
					 * ensure, so transformated mouse position is explicitly marked as
					 * unknown. So transformated position will be from frash mouse event
					 * always.
					 */
					mouse_row = -1;
					mouse_col = -1;

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
						int		prev_row = first_row;

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
							make_beep();
						}

						if (cursor_row - first_row + 1 > VISIBLE_DATA_ROWS)
							first_row += 1;

						first_row = first_row > max_first_row ? max_first_row : first_row;

						if (first_row != prev_row)
							if (!opts.no_sleep)
								usleep(30 * 1000);
					}
					else if (event.bstate & BUTTON4_PRESSED)
					{
						int		offset = 1;
						int		prev_row = first_row;

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
							make_beep();

						/*
						 * Without extra sleep time, an usage of mouse wheel can generate
						 * events peak (too much frequent doupdate(s)) with unwanted
						 * terminal flickering. We can limit a number of processed events
						 * just by sleeping.
						 */
						if (first_row != prev_row)
							if (!opts.no_sleep)
								usleep(30 * 1000);
					}
					else

#endif

					if (event.bstate & BUTTON1_PRESSED || event.bstate & BUTTON1_RELEASED

#if NCURSES_MOUSE_VERSION > 1

						|| event.bstate & REPORT_MOUSE_POSITION

#endif

						)
					{
						bool	is_double_click = false;
						bool	_is_footer_cursor;
						long	ms;
						time_t	sec;

						/*
						 * Own double click implentation. We need it, because we need
						 * waster BUTTON1_PRESSED event.
						 */
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
						 * leave modes, that needs holding mouse (scrollbar mode and
						 * mark mode) when mouse button is released
						 */
						if (event.bstate & BUTTON1_RELEASED &&
							!is_double_click &&
							(scrdesc.scrollbar_mode ||
							 mark_mode == MARK_MODE_MOUSE ||
							 mark_mode == MARK_MODE_MOUSE_BLOCK ||
							 mark_mode == MARK_MODE_MOUSE_COLUMNS))
						{
							mark_mode = MARK_MODE_NONE;
							scrdesc.scrollbar_mode = false;
							scrdesc.slider_has_position = true;
							break;
						}

#if NCURSES_MOUSE_VERSION > 1

						/* mouse move breaks double click cycle */
						if (event.bstate & REPORT_MOUSE_POSITION)
							last_sec = 0;

#endif

						/* scrollbar events implementation */
						if (scrdesc.scrollbar_mode ||
							(event.x == scrdesc.scrollbar_x &&
							 event.y >= scrdesc.scrollbar_start_y &&
							 event.y <= scrdesc.scrollbar_start_y + scrdesc.scrollbar_maxy))
						{
							if (!scrdesc.scrollbar_mode)
							{
								if (event.bstate & BUTTON1_PRESSED)
								{
									if (event.y == scrdesc.scrollbar_start_y)
									{
										next_command = cmd_CursorUp;
										last_sec = 0;
										break;
									}
									else if (event.y == scrdesc.scrollbar_start_y + scrdesc.scrollbar_maxy - 1)
									{
										next_command = cmd_CursorDown;
										last_sec = 0;
										break;
									}
									else
									{
										if (scrdesc.slider_min_y != -1)
										{
											if (event.y  < scrdesc.slider_min_y + scrdesc.scrollbar_start_y)
											{
												next_command = cmd_PageUp;
												last_sec = 0;
												break;
											}
											else if (event.y  > scrdesc.slider_min_y + scrdesc.slider_size)
											{
												next_command = cmd_PageDown;
												last_sec = 0;
												break;
											}
										}

										scrdesc.scrollbar_mode = true;
										scrdesc.scrollbar_mode = true;

#if NCURSES_MOUSE_VERSION > 1

										scrollbar_mode_initial_slider_mouse_offset_y =
														  event.y - scrdesc.scrollbar_start_y - scrdesc.slider_min_y;

#endif

									}
								}
							}

#if NCURSES_MOUSE_VERSION > 1


							if (scrdesc.scrollbar_mode && event.bstate & REPORT_MOUSE_POSITION &&
								scrdesc.scrollbar_maxy - 2 > scrdesc.slider_size)
							{
								int		new_slider_min_y;
								int		max_first_row;
								int		max_slider_min_y;
								int		page_cursor_row;

								max_first_row = MAX_FIRST_ROW;
								max_slider_min_y = scrdesc.scrollbar_maxy - scrdesc.slider_size - 1;
								page_cursor_row = cursor_row - first_row;

								new_slider_min_y =
										trim_to_range(
													  event.y - scrdesc.scrollbar_start_y
															  - scrollbar_mode_initial_slider_mouse_offset_y,
													  1, max_slider_min_y);

								/*
								 * set scrollbar does mapping first_row -> slider_min_y
								 *
								 *    first_row (0) -> slider_min_y = 1
								 *    first_row (max_first_row) -> slider_min_y (max_slider_min_y)
								 * ------
								 *    first_row (1) -> slider_min_y = 2
								 *    first_row (max_first_row - 1) -> slider_min_y (max_slider_min_y - 1)
								 *
								 * Here we should to do inverse mapping - slider_min_y -> first_row
								 *
								 *    slider_min_y (1) -> first_row = 0
								 *    slider_min_y (max_slider_min_y)) -> first_row (max_first_row)
								 * ------
								 *    slider_min_y (2) -> first_row = 1
								 *    slider_min_y (max_slider_min_y - 1) = first_row (max_first_row - 1)
								 *
								 */
								if (new_slider_min_y == 1)
									first_row = 0;
								else if (new_slider_min_y == max_slider_min_y)
									first_row = max_first_row;
								else
									first_row = ceil(((double) new_slider_min_y - 2) /
													 ((double) max_slider_min_y - 3) *
													 ((double) max_first_row - 2) + 1);

								cursor_row = first_row + page_cursor_row;

								scrdesc.slider_min_y = new_slider_min_y;

								/* slower processing reduce flickering */
								if (!opts.no_sleep)
									usleep(10 * 1000);
							}

#endif

							break;
						}

#if NCURSES_MOUSE_VERSION > 1

						if (mark_mode == MARK_MODE_MOUSE_COLUMNS &&
							event.bstate & REPORT_MOUSE_POSITION &&
							event.bstate & BUTTON_CTRL)
						{
							mouse_col  = mousex_get_colno(&desc, &scrdesc, &opts,
														  &cursor_col, default_freezed_cols,
														  event.x);
							break;
						}

#endif

						if (event.y == 0 && scrdesc.top_bar_rows > 0)
						{
							/* Activate menu only on BUTTON1_PRESS event */
							if (event.bstate & BUTTON1_PRESSED)
							{
								next_command = cmd_ShowMenu;
								reuse_event = true;
								prev_event_keycode = 0;
								break;
							}
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
									break;
								}
							}

							if (event.bstate & BUTTON_CTRL && event.bstate & BUTTON1_PRESSED)
							{
								mouse_col  = mousex_get_colno(&desc, &scrdesc, &opts,
															  &cursor_col, default_freezed_cols,
															  event.x);

								throw_selection(&scrdesc, &desc, &mark_mode);

								mark_mode = MARK_MODE_MOUSE_COLUMNS;
								mark_mode_start_col = mouse_col;

								break;
							}

							_is_footer_cursor = false;
							last_x_focus = event.x;
						}
						else
						{
							mouse_row = event.y - scrdesc.fix_rows_rows - scrdesc.top_bar_rows + first_row - fix_rows_offset;

							/* ignore invalid event */
							if (mouse_row < 0 ||
									mouse_row > MAX_CURSOR_ROW ||
									mouse_row - first_row + 1 > VISIBLE_DATA_ROWS)
							{
								mouse_row = -1;
								mouse_col = -1;
								break;
							}

							_is_footer_cursor = is_footer_cursor(mouse_row, &scrdesc, &desc);

							/*
							 * Save last x focused point. It will be used for repeated hide/unhide
							 * vertical cursor. But only if cursor is not in footer area.
							 */
							if (!_is_footer_cursor)
								last_x_focus = event.x;
						}

						if (event.bstate & BUTTON_ALT && is_double_click)
						{
							next_command = cmd_ToggleBookmark;
							throw_selection(&scrdesc, &desc, &mark_mode);
							break;
						}
						else if (!(event.bstate & BUTTON_ALT || event.bstate & BUTTON_CTRL) &&
								 opts.vertical_cursor && !_is_footer_cursor)
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
									if (desc.cranges[i].xmin <= xpoint && desc.cranges[i].xmax >= xpoint)
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

#if NCURSES_MOUSE_VERSION > 1

						if (event.bstate & BUTTON_CTRL &&
							event.bstate & BUTTON1_PRESSED)
						{
							throw_selection(&scrdesc, &desc, &mark_mode);

							mark_mode = MARK_MODE_MOUSE;
							mark_mode_start_row = mouse_row;
						}

						if (!_is_footer_cursor &&
							event.bstate & BUTTON_ALT &&
							(event.bstate & BUTTON1_PRESSED ||
							 event.bstate & REPORT_MOUSE_POSITION))
						{
							int		colno;

							colno  = mousex_get_colno(&desc, &scrdesc, &opts,
													  &cursor_col, default_freezed_cols,
													  event.x);

							if (colno != -1)
							{
								mouse_col = colno;

								if (event.bstate & BUTTON1_PRESSED)
								{
									throw_selection(&scrdesc, &desc, &mark_mode);

									mark_mode = MARK_MODE_MOUSE_BLOCK;
									mark_mode_start_row = mouse_row;
									mark_mode_start_col = colno;
								}

								last_x_focus = get_x_focus(colno, cursor_col, &desc, &scrdesc);
							}
						}

#endif

						/*
						 * Synchronize cursor with mouse point when mouse was not
						 * used for selection.
						 */
						if (mark_mode != MARK_MODE_MOUSE &&
								mark_mode != MARK_MODE_MOUSE_BLOCK &&
								mark_mode != MARK_MODE_MOUSE_COLUMNS &&
								mouse_row != -1)
							cursor_row = mouse_row;
					}
					break;
				}

		} /* end switch */

		if (fresh_found && scrdesc.found)
		{
			int		maxy_loc, maxx_loc;
			bool	_is_footer_cursor = is_footer_cursor(cursor_row, &scrdesc, &desc);

			UNUSED(maxy_loc);

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
				getmaxyx(w_fix_cols(&scrdesc), maxy_loc, maxx_loc);

				if (scrdesc.found_start_x + scrdesc.searchterm_char_size <= maxx_loc)
					fresh_found = false;
			}

			if (fresh_found && !_is_footer_cursor &&  w_rows(&scrdesc) != NULL)
			{
				getmaxyx(w_rows(&scrdesc), maxy_loc, maxx_loc);

				if (cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						cursor_col + scrdesc.fix_cols_cols + maxx_loc >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (cursor_col + scrdesc.fix_cols_cols > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (cursor_col + scrdesc.fix_cols_cols + maxx_loc < scrdesc.found_start_x + scrdesc.searchterm_char_size)
						next_command = cmd_MoveRight;
				}
			}

			if (fresh_found  && _is_footer_cursor && w_footer(&scrdesc) != NULL)
			{
				getmaxyx(w_footer(&scrdesc), maxy_loc, maxx_loc);

				if (footer_cursor_col + scrdesc.fix_cols_cols <= scrdesc.found_start_x &&
						footer_cursor_col + maxx_loc >= scrdesc.found_start_x + scrdesc.searchterm_char_size)
				{
					fresh_found = false;
				}
				else
				{
					/* we would to move cursor_col to left or right to be partially visible */
					if (footer_cursor_col > scrdesc.found_start_x)
						next_command = cmd_MoveLeft;
					else if (footer_cursor_col + maxx_loc < scrdesc.found_start_x + scrdesc.searchterm_char_size)
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
		set_scrollbar(&scrdesc, &desc, first_row);

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

		if (refresh_scr ||
			current_state->refresh_scr ||
			refresh_clear)
		{
			if (resize_scr)
			{
				/*
				 * Workaround - the variables COLS, LINES are not refreshed
				 * when pager is resized and executed inside psql.
				 */
				if (ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size) >= 0)
				{
					resize_term(size.ws_row, size.ws_col);
					clear();
				}

				resize_scr = false;
			}

#ifdef COMPILE_MENU

refresh:

#endif

			getmaxyx(stdscr, maxy, maxx);

			if (refresh_clear)
			{
				log_row("clear screen");
				clear();
				refresh_clear = false;
			}

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
			set_scrollbar(&scrdesc, &desc, first_row);

#ifdef COMPILE_MENU

			if (cmdbar)
				cmdbar = init_cmdbar(cmdbar, &opts);

#endif

			refresh_scr = false;
			current_state->refresh_scr = false;
		}
	}

	for (pspg_win_iter = 0; pspg_win_iter < PSPG_WINDOW_COUNT; pspg_win_iter++)
	{
		if (scrdesc.wins[pspg_win_iter])
			delwin(scrdesc.wins[pspg_win_iter]);
	}

#ifdef COMPILE_MENU

	if (cmdbar)
	{
		st_cmdbar_free(cmdbar);
		log_row("releasing cmd bar before end");
	}

	if (menu)
	{
		st_menu_free(menu);
		log_row("releasing menu bar before end");
	}

#endif

	endwin();

	log_row("ncurses ended");

	active_ncurses = false;

	if (raw_output_quit)
	{
		lb_print_all_ddesc(&desc, stdout);
	}
	else if (state.no_alternate_screen)
	{
		draw_data(&opts, &scrdesc, &desc, first_data_row, first_row, cursor_col,
				  footer_cursor_col, fix_rows_offset);
	}

	pspg_save_history(PSPG_HISTORY);

	if (state.inotify_fd >= 0)
		close(state.inotify_fd);

#ifdef DEBUG_PIPE

	/*
	 * Try to release all allocated memory in debug mode, for better
	 * memory leak detection.
	 */
	lb_free(&desc);
	free(desc.cranges);
	free(desc.headline_transl);
	free(opts.pathname);

	free(string_argument);

#endif

	/* close file in streaming mode */
	if (state.fp && !state.is_pipe)
		fclose(state.fp);

	if (state.tty)
	{
		/* stream mode against stdin */
		fclose(state.tty);

		/* close input too, send SIGPIPE to producent */
		fclose(stdin);
	}

	if (logfile)
	{
		log_row("correct quit\n");

		fclose(logfile);
		logfile = NULL;
	}


#ifdef DEBUG_PIPE

	/*
	 * Attention - this statement can raise warning "free(): invalid pointer"
	 * although is all ok. I think so it is due setlinebuf usage. Because this
	 * is only sometimes and only in debug mode, we can live with it. Maybe it
	 * is glibc bug.
	 */
	fclose(debug_pipe);
	debug_pipe = NULL;

#endif


	return 0;
}
