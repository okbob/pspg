/*-------------------------------------------------------------------------
 *
 * config.c
 *	  a routines for loading, saving configuration
 *
 * Portions Copyright (c) 2017-2019 Pavel Stehule
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

bool
save_config(char *path, Options *opts)
{
	FILE	   *f;
	int			result;

	errno = 0;
	f = fopen(path, "w");
	if (f == NULL)
		return false;

	result = fprintf(f, "ascii_menu = %s\n", opts->force_ascii_art ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "bold_labels = %s\n", opts->bold_labels ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "bold_cursor = %s\n", opts->bold_cursor ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "ignore_case = %s\n", opts->ignore_case ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "ignore_lower_case = %s\n", opts->ignore_lower_case ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "no_cursor = %s\n", opts->no_cursor ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "no_sound = %s\n", opts->no_sound ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "no_mouse = %s\n", opts->no_mouse ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "less_status_bar = %s\n", opts->less_status_bar ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "no_highlight_search = %s\n", opts->no_highlight_search ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "no_highlight_lines = %s\n", opts->no_highlight_lines ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "force_uniborder = %s\n", opts->force_uniborder ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "show_rownum = %s\n", opts->show_rownum ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "theme = %d\n", opts->theme);
	if (result < 0)
		return false;

	result = fprintf(f, "without_commandbar = %s\n", opts->no_commandbar ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "without_topbar = %s\n", opts->no_topbar ? "true" : "false");
	if (result < 0)
		return false;

	result = fprintf(f, "vertical_cursor = %s\n", opts->vertical_cursor ? "true" : "false");
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

			free(line);
			line = NULL;
		}
	}

	fclose(f);

	return true;
}
