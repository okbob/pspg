/*-------------------------------------------------------------------------
 *
 * config.c
 *	  a routines for loading, saving configuration
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/config.c
 *
 *-------------------------------------------------------------------------
 */
#include "config.h"

#include <errno.h>
#include <stdio.h>

bool
save_config(char *path, Options *opts)
{
	FILE *f;

	errno = 0;
	f = fopen(path, "w");
	if (f == NULL)
		return false;

	fprintf(f, "ignore_case = %s\n", opts->ignore_case ? "true" : "false");
	fprintf(f, "ignore_lower_case = %s\n", opts->ignore_lower_case ? "true" : "false");
	fprintf(f, "no_sound = %s\n", opts->no_sound ? "true" : "false");
	fprintf(f, "less_status_bar = %s\n", opts->less_status_bar ? "true" : "false");
	fprintf(f, "no_highlight_search = %s\n", opts->no_highlight_search ? "true" : "false");
	fprintf(f, "no_highlight_lines = %s\n", opts->no_highlight_lines ? "true" : "false");
	fprintf(f, "force_uniborder = %s\n", opts->force_uniborder ? "true" : "false");
	fprintf(f, "theme = %d\n", opts->theme);

	fclose(f);

	return true;
}

bool
load_config(char *path, Options *opts)
{
	return false;
}
