/*-------------------------------------------------------------------------
 *
 * config.c
 *	  a routines for loading, saving configuration
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/config.c
 *
 *-------------------------------------------------------------------------
 */
#include "config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
parse_cfg(char *line, char *key, bool *bool_val, int *int_val)
{
	int		key_length = 0;

	/* skip initial spaces */
	while (*line == ' ')
		line++;

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

		if (*line >= '0' && *line <= '9')
		{
			*int_val = atoi(line);
			return true;
		}
		else if (strncmp(line, "true", 4) == 0)
		{
			*bool_val = true;
			return true;
		}
		else if (strncmp(line, "false", 5) == 0)
		{
			*bool_val = false;
			return true;
		}
	}

	return false;
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

	SAFE_SAVE_BOOL_OPTION("ascii_menu", opts->force_ascii_art);
	SAFE_SAVE_BOOL_OPTION("bold_labels", opts->bold_labels);
	SAFE_SAVE_BOOL_OPTION("bold_cursor", opts->bold_cursor);
	SAFE_SAVE_BOOL_OPTION("ignore_case", opts->ignore_case);
	SAFE_SAVE_BOOL_OPTION("ignore_lower_case", opts->ignore_lower_case);
	SAFE_SAVE_BOOL_OPTION("no_cursor", opts->no_cursor);
	SAFE_SAVE_BOOL_OPTION("no_sound", opts->no_sound);
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

	result = fprintf(f, "theme = %d\n", opts->theme);
	if (result < 0)
		return false;

	result = fprintf(f, "border_type = %d\n", opts->border_type);
	if (result < 0)
		return false;

	result = fclose(f);
	if (result != 0)
		return false;

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

	errno = 0;
	f = fopen(path, "r");
	if (f == NULL)
		return false;

	while ((read = getline(&line, &len, f)) != -1)
	{
		char	key[100];
		bool	bool_val = false;
		int		int_val = -1;

		if (parse_cfg(line, key, &bool_val, &int_val))
		{
			if (strcmp(key, "ascii_menu") == 0)
				opts->force_ascii_art = bool_val;
			else if (strcmp(key, "bold_labels") == 0)
				opts->bold_labels = bool_val;
			else if (strcmp(key, "bold_cursor") == 0)
				opts->bold_cursor = bool_val;
			else if (strcmp(key, "ignore_case") == 0)
				opts->ignore_case = bool_val;
			else if (strcmp(key, "ignore_lower_case") == 0)
				opts->ignore_lower_case = bool_val;
			else if (strcmp(key, "no_sound") == 0)
				opts->no_sound = bool_val;
			else if (strcmp(key, "no_cursor") == 0)
				opts->no_cursor = bool_val;
			else if (strcmp(key, "no_mouse") == 0)
				opts->no_mouse = bool_val;
			else if (strcmp(key, "less_status_bar") == 0)
				opts->less_status_bar = bool_val;
			else if (strcmp(key, "no_highlight_search") == 0)
				opts->no_highlight_search = bool_val;
			else if (strcmp(key, "no_highlight_lines") == 0)
				opts->no_highlight_lines = bool_val;
			else if (strcmp(key, "force_uniborder") == 0)
				opts->force_uniborder = bool_val;
			else if (strcmp(key, "show_rownum") == 0)
				opts->show_rownum = bool_val;
			else if (strcmp(key, "theme") == 0)
				opts->theme = int_val;
			else if (strcmp(key, "without_commandbar") == 0)
				opts->no_commandbar = bool_val;
			else if (strcmp(key, "without_topbar") == 0)
				opts->no_topbar = bool_val;
			else if (strcmp(key, "vertical_cursor") == 0)
				opts->vertical_cursor = bool_val;
			else if (strcmp(key, "border_type") == 0)
				opts->border_type = int_val;
			else if (strcmp(key, "double_header") == 0)
				opts->double_header = bool_val;
			else if (strcmp(key, "on_sigint_exit") == 0)
				opts->on_sigint_exit = bool_val;
			else if (strcmp(key, "no_sigint_search_reset") == 0)
				opts->no_sigint_search_reset = bool_val;
			else if (strcmp(key, "quit_on_f3") == 0)
				opts->quit_on_f3 = bool_val;

			free(line);
			line = NULL;
		}
	}

	fclose(f);

	return true;
}
