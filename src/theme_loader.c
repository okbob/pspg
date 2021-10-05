/*-------------------------------------------------------------------------
 *
 * theme_loader.c
 *	  a routines for loading theme (style) definition
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/theme_loader.c
 *
 *-------------------------------------------------------------------------
 */
#include "pspg.h"
#include "themes.h"

/*
 * Theme loader try to work in tolerant mode, broken lines
 * are ignored. An information about broken lines are printed to log.
 *
 * This function returns true, when theme was loaded. An warning can
 * be raised by setting output argument is_warning to true.
 */
bool
theme_loader(FILE *theme, PspgThemeLoaderElement *tle, int size, bool *is_warning)
{
	*is_warning = false;

	if (size <= PspgTheme_error)
		leave("internal error - the size of theme loader table is too small");



	return true;
}
