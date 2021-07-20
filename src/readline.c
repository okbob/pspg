/*-------------------------------------------------------------------------
 *
 * readline.c
 *	  a routines for using readline library
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/readline.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * Uncomment for test without readline
 *
#undef HAVE_LIBREADLINE
#undef HAVE_READLINE_HISTORY
 */

#include "pspg.h"
#include "unicode.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBREADLINE

#if defined(HAVE_READLINE_READLINE_H)

#include <readline/readline.h>

#elif defined(HAVE_READLINE_H)

#include <readline.h>

#endif /* #if defined(HAVE_READLINE_READLINE_H) */

#if RL_VERSION_MAJOR < 6

#define rl_display_prompt rl_prompt

#endif

#ifdef HAVE_READLINE_HISTORY
#if defined(HAVE_READLINE_HISTORY_H)

#include <readline/history.h>

#elif defined(HAVE_HISTORY_H)

#include <history.h>

#endif
#endif

static char		readline_buffer[1024];
static bool		editation_completed;
static unsigned char	readline_ncurses_proxy_char;
static bool		readline_ncurses_proxy_char_is_available = false;

static bool		forward_complete;
static char	   *readline_prompt;
static int		tabcomplete_mode;
static const char **possible_tokens = NULL;

#ifdef HAVE_READLINE_HISTORY

static char		last_history[256];

static const char *saved_histfile = NULL;
static bool		history_loaded = false;

#endif

static const char *bscommands[] = {
	"save",
	"copy",
	"theme",
	"quit",
	"order",
	"orderd",
	"search",
	"sort",
	"sortd",
	"rsort",
	"dsort",
	NULL
};

static const char *export_opts[] = {
	"all",
	"top",
	"bottom",
	"selected",
	"searched",
	"marked",
	"csv",
	"tsvc",
	"text",
	"pipesep",
	"insert",
	"cinsert",
	"nullstr",
	"sqlvalues",
	NULL
};

static const char *search_opts[] = {
	"backward",
	"selected",
	"column",
	NULL
};


/*
 * Save history to hist file
 */
void
pspg_save_history(const char *histfile, Options *opts)
{

#ifdef HAVE_READLINE_HISTORY

	if (history_loaded)
	{
		if (opts->hist_size >= 0)
			stifle_history(opts->hist_size);

		write_history(tilde(NULL, histfile));

#if RL_READLINE_VERSION >= 0x0603

		rl_clear_history();

#else

		clear_history();

#endif

	}

#endif

}

/*
 * Simple functions for using readline from ncurses
 */

#if RL_READLINE_VERSION >= 0x0603

static int
readline_input_avail(void)
{
    return readline_ncurses_proxy_char_is_available;
}

#endif

static int
readline_getc(FILE *dummy)
{
	UNUSED(dummy);

    readline_ncurses_proxy_char_is_available = false;
    return readline_ncurses_proxy_char;
}

static void
set_readline_ncurses_proxy_char(char c)
{
    readline_ncurses_proxy_char = c;
    readline_ncurses_proxy_char_is_available = true;
    rl_callback_read_char();
}

/*
 * Does copy of readline's result string to persistent buffer.
 */
static void
readline_callback(char *line)
{
	if (line)
	{
		strncpy(readline_buffer, line, sizeof(readline_buffer) - 1);
		*(readline_buffer + sizeof(readline_buffer) - 1) = '\0';
		free(line);
	}
	else
		readline_buffer[0] = '\0';

	editation_completed = true;
}

/*
 * Display readline's result string in ncurses window
 */
static void
readline_redisplay()
{
	size_t cursor_col;

	if (use_utf8)
	{
		size_t prompt_dsplen = utf_string_dsplen(rl_display_prompt, INT_MAX);

		cursor_col = prompt_dsplen
					  + readline_utf_string_dsplen(rl_line_buffer, rl_point, prompt_dsplen);
	}
	else
	{
		cursor_col = strlen(rl_display_prompt) + min_int(strlen(rl_line_buffer), rl_point);
	}

	wbkgd(prompt_window, prompt_window_input_attr);
	werase(prompt_window);
	mvwprintw(prompt_window, 0, 0, "%s%s", rl_display_prompt, rl_line_buffer);
	mvwchgat(prompt_window, 0, 0, -1, prompt_window_input_attr, PAIR_NUMBER(prompt_window_input_attr), 0);

	if (cursor_col >= (size_t) COLS)
		curs_set(0);
	else
	{
		wmove(prompt_window, 0, cursor_col);
		curs_set(2);
	}

	wrefresh(prompt_window);
}

static char *
completion_generator(const char *text, int state)
{
	static int list_index, len;
	const char *name;

	if (!state)
	{
		list_index = 0;
		len = strlen(text);
	}

	while ((name = possible_tokens[list_index++]))
	{
		if (strncmp(name, text, len) == 0)
			return strdup(name);
	}

	return NULL;
}

static char *
tablename_generator(const char *text, int state)
{
	static int list_index, len;
	const char *name;
	int		name_len;

	if (!current_state->desc->namesline)
		return NULL;

	if (!state)
	{
		list_index = 0;
		len = strlen(text);
	}

	while (list_index < current_state->desc->columns)
	{
		name = current_state->desc->namesline + current_state->desc->cranges[list_index].name_offset;
		name_len = current_state->desc->cranges[list_index].name_size;

		list_index += 1;

		if (use_utf8)
		{
			if (utf8_nstarts_with_with_sizes(name, name_len, text, len))
				return sstrndup(name, name_len);
		}
		else
		{
			if (nstarts_with_with_sizes(name, name_len, text, len))
				return sstrndup(name, name_len);
		}
	}

	return NULL;
}

static void
get_prev_token(int start, const char **token, int *size)
{
	*size = 0;
	*token = NULL;

	if (start > 0)
	{
		char *ptr = &rl_line_buffer[start - 1];

		/* check previous token */
		if (*ptr == '"')
			ptr -= 1;

		while (ptr >= rl_line_buffer && *ptr == ' ')
			ptr -= 1;

		while (ptr > rl_line_buffer)
		{
			if (isalnum(*(ptr - 1)))
				ptr -= 1;
			else
				break;
		}

		if (isalnum(*ptr))
			get_token(ptr, token, size);
	}
}

static char **
pspg_complete(const char *text, int start, int end)
{
	UNUSED(end);

	if (tabcomplete_mode == 'f')
	{
		rl_completion_suppress_append = 1;
		rl_attempted_completion_over = 0;
	}
	else
	{
		rl_completion_suppress_append = 0;
		rl_attempted_completion_over = 1;

		if (tabcomplete_mode == 'c')
		{
			if (start > 0)
			{
				if (rl_line_buffer[start - 1] == '\\')
				{
					possible_tokens = bscommands;
					return rl_completion_matches(text, completion_generator);
				}
				else
				{
					int		i;
					char   *bscommand = NULL;

					for (i = start - 1; i >= 0; i--)
					{
						if (rl_line_buffer[i] == '\\')
						{
							bscommand = &rl_line_buffer[i+1];
							break;
						}
					}

					if (bscommand)
					{
						const char   *token;
						int		n;

						get_token(bscommand, &token, &n);
						if (n > 0)
						{
							if (IS_TOKEN(token, n, "save") ||
								IS_TOKEN(token, n, "copy"))
							{
								possible_tokens = export_opts;
								return rl_completion_matches(text, completion_generator);
							}
							else if (IS_TOKEN(token, n, "ordd") ||
									 IS_TOKEN(token, n, "orderd") ||
									 IS_TOKEN(token, n, "ord") ||
									 IS_TOKEN(token, n, "order") ||
									 IS_TOKEN(token, n, "sort") ||
									 IS_TOKEN(token, n, "sortd") ||
									 IS_TOKEN(token, n, "dsort") ||
									 IS_TOKEN(token, n, "rsort") ||
									 IS_TOKEN(token, n, "rs"))
							{
								return rl_completion_matches(text, tablename_generator);
							}
							else if (IS_TOKEN(token, n, "search"))
							{
								const char	   *prev_token;
								int			prev_token_size;
								bool		prev_token_is_column = false;

								get_prev_token(start, &prev_token, &prev_token_size);

								if (prev_token_size > 0)
								{
									prev_token_is_column = IS_TOKEN(prev_token, prev_token_size, "colum") ||
														  IS_TOKEN(prev_token, prev_token_size, "column");
								}

								if (prev_token_is_column)
									return rl_completion_matches(text, tablename_generator);
								else
								{
									possible_tokens = search_opts;
									return rl_completion_matches(text, completion_generator);
								}
							}
						}
					}
				}
			}
		}
	}

	return (char**) NULL;
}

/*
 * Default readline rutine for printing list of possible matches breaks ncurses output.
 * More we have only one row for editing. I don't want to implement dynamic window for
 * completion. So pspg implements own method for printing completion menu.
 */
static void
pspg_display_match(char **matches, int num_matches, int max_length)
{
	int		common_length;
	int		pos = 1;
	char	c = 0;

	UNUSED(max_length);

	forward_complete = false;

	common_length = strlen(matches[0]);

	wbkgd(prompt_window, prompt_window_input_attr);
	werase(prompt_window);

	while (1)
	{
		if (handle_sigint)
			break;

		werase(prompt_window);
		wmove(prompt_window, 0, 0);
		wprintw(prompt_window, "%s%s", readline_prompt, rl_line_buffer);

		wattron(prompt_window, A_BOLD);
		wprintw(prompt_window, "%s", matches[pos] + common_length);
		wattroff(prompt_window, A_BOLD);

		wrefresh(prompt_window);

		noecho();
		c = getch();
		echo();

		if (c == 13)
		{
			rl_insert_text(matches[pos] + common_length);
			ungetch(13);
			break;
		}
		else if (c == 7)
		{
			rl_insert_text(matches[pos] + common_length);
			ungetch(127);
			break;
		}
		else if (c == 3)
			pos -= 1;
		else if (c == 2 || c == 9)
			pos += 1;
		else if (c == 4 || c == 27)
			break;
		else if (c == 5)
		{
			rl_insert_text(matches[pos] + common_length);
			break;
		}
		else if (tabcomplete_mode == 'c' && c == ' ')
		{
			rl_insert_text(matches[pos] + common_length);
			rl_insert_text(" ");
			break;
		}
		else if (tabcomplete_mode == 'c' && c == '"')
		{
			rl_insert_text(matches[pos] + common_length);
			rl_insert_text("\" ");
			break;
		}
		else
		{
			if (c != 9 && c != -1)
			{
				char	   str[2];
				str[0] = c;
				str[1] = '\0';

				rl_insert_text(str);
				forward_complete = true;
				break;
			}
		}

		if (pos > num_matches)
			pos = 1;
		if (pos < 1)
			pos = num_matches;
	}
}

bool
get_string(char *prompt,
		   char *buffer,
		   int maxsize,
		   char *defstr,
		   char _tabcomplete_mode)
{
	bool	result_is_ok = true;
	int		c, prev_c = 0;
	mmask_t		prev_mousemask = 0;
	bool	prev_xterm_mouse_mode;

#ifdef HAVE_READLINE_HISTORY

	/*
	 * Lazy history loading. Loading history can be slow, so use it, only
	 * when string is wanted.
	 */
	if (!history_loaded)
	{
		read_history(tilde(NULL, saved_histfile));
		history_loaded = true;

		last_history[0] = '\0';
	}

#endif

	log_row("input string prompt - \"%s\"", prompt);

	editation_completed = false;
	tabcomplete_mode = _tabcomplete_mode;

	wattron(prompt_window, prompt_window_input_attr);
	mvwprintw(prompt_window, 0, 0, "");
	wclrtoeol(prompt_window);

	curs_set(1);
	echo();

	readline_prompt = prompt;

	rl_getc_function = readline_getc;

#if RL_READLINE_VERSION >= 0x0603

	rl_input_available_hook = readline_input_avail;

#endif

	rl_redisplay_function = readline_redisplay;

	rl_callback_handler_install(prompt, readline_callback);

	if (tabcomplete_mode == 'c')
	{
		rl_completer_word_break_characters = "\\ ";
		rl_completer_quote_characters = "\"'";
	}
	else
	{
		rl_completer_word_break_characters = (char *) rl_basic_word_break_characters;
		rl_completer_quote_characters = NULL;
	}

	mousemask(0, &prev_mousemask);
	prev_xterm_mouse_mode = disable_xterm_mouse_mode();

	if (tabcomplete_mode == 'c')
	{
		rl_inhibit_completion = 0;

		rl_insert_text("\\");
		rl_forced_update_display();
		wrefresh(prompt_window);
	}
	else if (tabcomplete_mode == 'f')
		rl_inhibit_completion = 0;
	else if (tabcomplete_mode == 'u')
		rl_inhibit_completion = 1;

	/* use default value from buffer */
	if (defstr && *defstr)
	{
		rl_insert_text(defstr);
		rl_forced_update_display();
		wrefresh(prompt_window);
	}

	while (!editation_completed)
	{
		do
		{
			if (forward_complete)
			{
				c = 9;
				forward_complete = false;
			}
			else
				c = wgetch(prompt_window);

			if (c == ERR && errno == EINTR)
			{
				if (handle_sigwinch)
				{
					handle_sigwinch = false;

					refresh_terminal_size();
					refresh_layout_after_terminal_resize();
					redraw_screen();

					wattron(prompt_window, prompt_window_input_attr);
					mvwprintw(prompt_window, 0, 0, "");
					wclrtoeol(prompt_window);

					rl_forced_update_display();

					wrefresh(prompt_window);
				}

				continue;
			}

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
			result_is_ok = false;
		}

		prev_c = c;
		set_readline_ncurses_proxy_char(c);

		wrefresh(prompt_window);

		if (!result_is_ok)
			break;
	}

finish_read:

	if (handle_sigint)
	{
		handle_sigint = false;
		result_is_ok = false;
	}

	mousemask(prev_mousemask, NULL);
	enable_xterm_mouse_mode(prev_xterm_mouse_mode);

	rl_callback_handler_remove();

	curs_set(0);
	noecho();

	/* don't allow alt chars (garbage) in input string */
	if (result_is_ok)
	{
		char   *ptr = readline_buffer;

		while (*ptr)
			if (*ptr++ == 27)
			{
				result_is_ok = false;
				break;
			}
	}

	if (result_is_ok)
	{
		if (tabcomplete_mode == 'f')
		{
			char	*tstr;
			int		bytes;

			bytes = strlen(readline_buffer);
			tstr = trim_quoted_str(readline_buffer, &bytes);

			bytes = bytes < (maxsize - 1) ? bytes : (maxsize - 1);
			memcpy(buffer, tstr, bytes);
			buffer[bytes] = '\0';
		}
		else
		{
			strncpy(buffer, readline_buffer, maxsize - 1);
			buffer[maxsize] = '\0';
		}

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

		if (defstr)
			strcpy(defstr, buffer);
	}
	else
	{
		if (defstr)
			*defstr = '\0';
		buffer[0] = '\0';
	}

	/*
	 * Screen should be refreshed after show any info.
	 */
	current_state->refresh_scr = true;

	log_row("input string - \"%s\"", buffer);

	return result_is_ok;
}

/*
 * Initialize global readline variables
 */
void
pspg_init_readline(const char *histfile)
{
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;

#if RL_READLINE_VERSION > 0x0603

	rl_change_environment = 0;

#endif

	rl_inhibit_completion = 0;

	rl_completion_display_matches_hook = pspg_display_match;
	rl_attempted_completion_function = pspg_complete;

#ifdef HAVE_READLINE_HISTORY

	saved_histfile = histfile;
	history_loaded = false;
	last_history[0] = '\0';

#endif

}

#else

/*
 * Empty functions when readline is not available
 */
void
pspg_init_readline(const char *histfile)
{
	return;
}

void
pspg_save_history(const char *histfile, Options *opts)
{
	return;
}

bool
get_string(char *prompt,
		   char *buffer,
		   int maxsize,
		   char *defstr,
		   char _tabcomplete_mode)
{
	mmask_t		prev_mousemask = 0;
	bool	prev_xterm_mouse_mode;

	log_row("input string prompt - \"%s\"", prompt);

	wbkgd(prompt_window, prompt_window_input_attr);
	werase(prompt_window);
	mvwprintw(prompt_window, 0, 0, "%s", prompt);
	curs_set(1);
	echo();

	mousemask(0, &prev_mousemask);
	prev_xterm_mouse_mode = disable_xterm_mouse_mode();

	wgetnstr(prompt_window, buffer, maxsize);

	if (_tabcomplete_mode == 'f')
	{
		char	*tstr;
		int		bytes;

		bytes = strlen(buffer);
		tstr = trim_quoted_str(buffer, &bytes);

		memcpy(buffer, tstr, bytes);
		buffer[bytes] = '\0';
	}

	/* reset ctrlc, wgetnstr doesn't handle this signal now */
	handle_sigint = false;

	curs_set(0);
	noecho();

	mousemask(prev_mousemask, NULL);
	enable_xterm_mouse_mode(prev_xterm_mouse_mode);

	if (defstr)
		strcpy(defstr, buffer);

	/*
	 * Screen should be refreshed after show any info.
	 */
	current_state->refresh_scr = true;

	log_row("input string - \"%s\"", buffer);

	return true;
}

#endif
