/*-------------------------------------------------------------------------
 *
 * config.c
 *	  a routines for loading, saving configuration
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/config.c
 *
 *-------------------------------------------------------------------------
 */
#include "pspg.h"
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int
parse_cfg(char *line, char *key, bool *bool_val, int *int_val, char **str_val)
{
	int		key_length = 0;
	int		len = strlen(line);

	if (len > 0 && line[len - 1] == '\n')
		line[len-1] = '\0';

	/* skip initial spaces */
	while (*line == ' ')
		line++;

	/* skip comments */
	if (*line == '#')
		return 0;

	/* copy key to key array */
	while (*line != ' ' && *line != '=' && *line != '\0')
	{
		if (key_length < 99)
		{
			key[key_length++] = *line++;
		}
		else
			break;
	}

	key[key_length] = '\0';

	/* search '=' */
	while (*line != '=' && *line != '\0')
		line++;

	if (key_length > 0 && *line == '=')
	{
		line += 1;

		/* skip spaces */
		while (*line == ' ')
			line++;

		if (*line == '-' || (*line >= '0' && *line <= '9'))
		{
			*int_val = atoi(line);
			return 1;
		}
		else if (strncmp(line, "true", 4) == 0)
		{
			*bool_val = true;
			return 2;
		}
		else if (strncmp(line, "false", 5) == 0)
		{
			*bool_val = false;
			return 2;
		}
		else
		{
			int		size;
			char   *str;

			size = strlen(line);
			str = trim_quoted_str(line, &size);
			*str_val = str ? sstrndup(str, size) : NULL;
			return 3;
		}
	}

	if (key_length > 0)
		return -1;

	return 0;
}

#define SAFE_SAVE_BOOL_OPTION(name, opt)		\
do { \
	result = fprintf(f, "%s = %s\n", (name), (opt) ? "true" : "false"); \
	if (result < 0) \
		return false; \
} while (0)

bool
save_config(char *path, Options *opts)
{
	FILE	   *f;
	int			result;

	errno = 0;
	f = fopen(path, "w");
	if (f == NULL)
		return false;

	if (chmod(path, 0644) != 0)
	{
		fclose(f);
		return false;
	}

	SAFE_SAVE_BOOL_OPTION("ascii_menu", opts->force_ascii_art);
	SAFE_SAVE_BOOL_OPTION("bold_labels", opts->bold_labels);
	SAFE_SAVE_BOOL_OPTION("bold_cursor", opts->bold_cursor);
	SAFE_SAVE_BOOL_OPTION("ignore_case", opts->ignore_case);
	SAFE_SAVE_BOOL_OPTION("ignore_lower_case", opts->ignore_lower_case);
	SAFE_SAVE_BOOL_OPTION("no_cursor", opts->no_cursor);
	SAFE_SAVE_BOOL_OPTION("no_sound", quiet_mode);
	SAFE_SAVE_BOOL_OPTION("no_mouse", opts->no_mouse);
	SAFE_SAVE_BOOL_OPTION("less_status_bar", opts->less_status_bar);
	SAFE_SAVE_BOOL_OPTION("no_highlight_search", opts->no_highlight_search);
	SAFE_SAVE_BOOL_OPTION("no_highlight_lines", opts->no_highlight_lines);
	SAFE_SAVE_BOOL_OPTION("force_uniborder", opts->force_uniborder);
	SAFE_SAVE_BOOL_OPTION("show_rownum", opts->show_rownum);
	SAFE_SAVE_BOOL_OPTION("without_commandbar", opts->no_commandbar);
	SAFE_SAVE_BOOL_OPTION("without_topbar", opts->no_topbar);
	SAFE_SAVE_BOOL_OPTION("vertical_cursor", opts->vertical_cursor);
	SAFE_SAVE_BOOL_OPTION("on_sigint_exit", opts->on_sigint_exit);
	SAFE_SAVE_BOOL_OPTION("no_sigint_search_reset", opts->no_sigint_search_reset);
	SAFE_SAVE_BOOL_OPTION("double_header", opts->double_header);
	SAFE_SAVE_BOOL_OPTION("quit_on_f3", opts->quit_on_f3);
	SAFE_SAVE_BOOL_OPTION("pgcli_fix", opts->pgcli_fix);
	SAFE_SAVE_BOOL_OPTION("xterm_mouse_mode", opts->xterm_mouse_mode);
	SAFE_SAVE_BOOL_OPTION("show_scrollbar", opts->show_scrollbar);
	SAFE_SAVE_BOOL_OPTION("menu_always", opts->menu_always);
	SAFE_SAVE_BOOL_OPTION("empty_string_is_null", opts->empty_string_is_null);
	SAFE_SAVE_BOOL_OPTION("last_row_search", opts->last_row_search);
	SAFE_SAVE_BOOL_OPTION("progressive_load_mode", opts->progressive_load_mode);
	SAFE_SAVE_BOOL_OPTION("highlight_odd_rec", opts->highlight_odd_rec);
	SAFE_SAVE_BOOL_OPTION("hide_header_line", opts->hide_header_line);
	SAFE_SAVE_BOOL_OPTION("on_exit_reset", opts->on_exit_reset);
	SAFE_SAVE_BOOL_OPTION("on_exit_clean", opts->on_exit_clean);
	SAFE_SAVE_BOOL_OPTION("on_exit_erase_line", opts->on_exit_erase_line);
	SAFE_SAVE_BOOL_OPTION("on_exit_sgr0", opts->on_exit_sgr0);
	SAFE_SAVE_BOOL_OPTION("direct_color", opts->direct_color);

	result = fprintf(f, "theme = %d\n", opts->theme);
	if (result < 0)
		return false;

	result = fprintf(f, "border_type = %d\n", opts->border_type);
	if (result < 0)
		return false;

	result = fprintf(f, "default_clipboard_format = %d\n", opts->clipboard_format);
	if (result < 0)
		return false;

	result = fprintf(f, "clipboard_app = %d\n", opts->clipboard_app);
	if (result < 0)
		return false;

	result = fprintf(f, "hist_size = %d\n", opts->hist_size);
	if (result < 0)
		return false;

	if (opts->nullstr)
	{
		result = fprintf(f, "nullstr = \"%s\"\n", opts->nullstr);
		if (result < 0)
			return false;
	}

	if (opts->custom_theme_name)
	{
		result = fprintf(f, "custom_theme_name = \"%s\"\n", opts->custom_theme_name);
		if (result < 0)
			return false;
	}

	result = fprintf(f, "esc_delay = %d\n", opts->esc_delay);
	if (result < 0)
		return false;

	result = fclose(f);
	if (result != 0)
		return false;

	return true;
}

static bool
assign_bool(char *key, bool *target, bool value, int type)
{
	if (type != 2)
	{
		log_row("The value of key \"%s\" is not boolean value", key);
		return false;
	}

	*target = value;

	return true;
}

static bool
assign_int(char *key, int *target, int value, int type, int min, int max)
{
	if (type != 1)
	{
		log_row("The value of key \"%s\" is not integer value", key);
		return false;
	}

	if (value < min && value > max)
	{
		log_row("value of key \"%s\" is out of range [%d, %d]", key, min, max);
		return false;
	}

	*target = value;

	return true;
}

static bool
assign_str(char *key, char **target, char *value, int type)
{
	if (type != 3)
	{
		log_row("The value of key \"%s\" is not string value", key);
		return false;
	}

	*target = value;

	return true;
}

/*
 * Simple parser of config file. I don't expect too much fields, so performance is
 * not significant.
 */
bool
load_config(char *path, Options *opts)
{
	FILE *f;
	char 		*line = NULL;
	ssize_t		read;
	size_t		len;
	bool		is_valid = true;

	errno = 0;
	f = fopen(path, "r");
	if (f == NULL)
		return false;

	while ((read = getline(&line, &len, f)) != -1)
	{
		char	key[100];
		bool	bool_val = false;
		int		int_val = -1;
		char   *str_val = NULL;
		int		res;

		if ((res = parse_cfg(line, key, &bool_val, &int_val, &str_val)) > 0)
		{
			if (strcmp(key, "ascii_menu") == 0)
				is_valid = assign_bool(key, &opts->force_ascii_art, bool_val, res);
			else if (strcmp(key, "bold_labels") == 0)
				is_valid = assign_bool(key, &opts->bold_labels, bool_val, res);
			else if (strcmp(key, "bold_cursor") == 0)
				is_valid = assign_bool(key, &opts->bold_cursor, bool_val, res);
			else if (strcmp(key, "ignore_case") == 0)
				is_valid = assign_bool(key, &opts->ignore_case, bool_val, res);
			else if (strcmp(key, "ignore_lower_case") == 0)
				is_valid = assign_bool(key, &opts->ignore_lower_case, bool_val, res);
			else if (strcmp(key, "no_sound") == 0)
				is_valid = assign_bool(key, &quiet_mode, bool_val, res);
			else if (strcmp(key, "no_cursor") == 0)
				is_valid = assign_bool(key, &opts->no_cursor, bool_val, res);
			else if (strcmp(key, "no_mouse") == 0)
				is_valid = assign_bool(key, &opts->no_mouse, bool_val, res);
			else if (strcmp(key, "less_status_bar") == 0)
				is_valid = assign_bool(key, &opts->less_status_bar, bool_val, res);
			else if (strcmp(key, "no_highlight_search") == 0)
				is_valid = assign_bool(key, &opts->no_highlight_search, bool_val, res);
			else if (strcmp(key, "no_highlight_lines") == 0)
				is_valid = assign_bool(key, &opts->no_highlight_lines, bool_val, res);
			else if (strcmp(key, "force_uniborder") == 0)
				is_valid = assign_bool(key, &opts->force_uniborder, bool_val, res);
			else if (strcmp(key, "show_rownum") == 0)
				is_valid = assign_bool(key, &opts->show_rownum, bool_val, res);
			else if (strcmp(key, "theme") == 0)
				is_valid = assign_int(key, &opts->theme, int_val, res, 0, MAX_STYLE);
			else if (strcmp(key, "without_commandbar") == 0)
				is_valid = assign_bool(key, &opts->no_commandbar, bool_val, res);
			else if (strcmp(key, "without_topbar") == 0)
				is_valid = assign_bool(key, &opts->no_topbar, bool_val, res);
			else if (strcmp(key, "vertical_cursor") == 0)
				is_valid = assign_bool(key, &opts->vertical_cursor, bool_val, res);
			else if (strcmp(key, "border_type") == 0)
				is_valid = assign_int(key, &opts->border_type, int_val, res, 0, 2);
			else if (strcmp(key, "double_header") == 0)
				is_valid = assign_bool(key, &opts->double_header, bool_val, res);
			else if (strcmp(key, "on_sigint_exit") == 0)
				is_valid = assign_bool(key, &opts->on_sigint_exit, bool_val, res);
			else if (strcmp(key, "no_sigint_search_reset") == 0)
				is_valid = assign_bool(key, &opts->no_sigint_search_reset, bool_val, res);
			else if (strcmp(key, "quit_on_f3") == 0)
				is_valid = assign_bool(key, &opts->quit_on_f3, bool_val, res);
			else if (strcmp(key, "pgcli_fix") == 0)
				is_valid = assign_bool(key, &opts->pgcli_fix, bool_val, res);
			else if (strcmp(key, "default_clipboard_format") == 0)
				is_valid = assign_int(key, (int *) &opts->clipboard_format, int_val, res, 0, CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS);
			else if (strcmp(key, "clipboard_app") == 0)
				is_valid = assign_int(key, &opts->clipboard_app, int_val, res, 0, 3);
			else if (strcmp(key, "xterm_mouse_mode") == 0)
				is_valid = assign_bool(key, &opts->xterm_mouse_mode, bool_val, res);
			else if (strcmp(key, "show_scrollbar") == 0)
				is_valid = assign_bool(key, &opts->show_scrollbar, bool_val, res);
			else if (strcmp(key, "menu_always") == 0)
				is_valid = assign_bool(key, &opts->menu_always, bool_val, res);
			else if (strcmp(key, "nullstr") == 0)
				is_valid = assign_str(key, &opts->nullstr, str_val, res);
			else if (strcmp(key, "empty_string_is_null") == 0)
				is_valid = assign_bool(key, &opts->empty_string_is_null, bool_val, res);
			else if (strcmp(key, "last_row_search") == 0)
				is_valid = assign_bool(key, &opts->last_row_search, bool_val, res);
			else if (strcmp(key, "hist_size") == 0)
				is_valid = assign_int(key, (int *) &opts->hist_size, int_val, res, 0, INT_MAX);
			else if (strcmp(key, "progressive_load_mode") == 0)
				is_valid = assign_bool(key, &opts->progressive_load_mode, bool_val, res);
			else if (strcmp(key, "custom_theme_name") == 0)
				is_valid = assign_str(key, &opts->custom_theme_name, str_val, res);
			else if (strcmp(key, "highlight_odd_rec") == 0)
				is_valid = assign_bool(key, &opts->highlight_odd_rec, bool_val, res);
			else if (strcmp(key, "hide_header_line") == 0)
				is_valid = assign_bool(key, &opts->hide_header_line, bool_val, res);
			else if (strcmp(key, "esc_delay") == 0)
				is_valid = assign_int(key, &opts->esc_delay, int_val, res, -1, INT_MAX);
			else if (strcmp(key, "on_exit_reset") == 0)
				is_valid = assign_bool(key, &opts->on_exit_reset, bool_val, res);
			else if (strcmp(key, "on_exit_clean") == 0)
				is_valid = assign_bool(key, &opts->on_exit_clean, bool_val, res);
			else if (strcmp(key, "on_exit_erase_line") == 0)
				is_valid = assign_bool(key, &opts->on_exit_erase_line, bool_val, res);
			else if (strcmp(key, "on_exit_sgr0") == 0)
				is_valid = assign_bool(key, &opts->on_exit_sgr0, bool_val, res);
			else if (strcmp(key, "direct_color") == 0)
				is_valid = assign_bool(key, &opts->direct_color, bool_val, res);

			if (!is_valid || res == -1)
				break;
		}
	}

	free(line);

	fclose(f);

	return is_valid;
}
