/*-------------------------------------------------------------------------
 *
 * themes.h
 *	  themes initialization
 *
 * Portions Copyright (c) 2017-2017 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/themes.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PSPG_THEMES_H
#define PSPG_THEMES_H

#include <curses.h>

typedef struct
{
	attr_t data_attr;				/* colors for data (alphanums) */
	attr_t line_attr;				/* colors for borders */
	attr_t expi_attr;				/* colors for expanded headers */
	attr_t cursor_data_attr;		/* colors for cursor on data positions */
	attr_t cursor_line_attr;		/* colors for cursor on border position */
	attr_t cursor_expi_attr;		/* colors for cursor on expanded headers */
	attr_t bookmark_data_attr;		/* colors for bookmark */
	attr_t bookmark_line_attr;		
	attr_t cursor_bookmark_attr;	/* colors for cursor on bookmark line */
	attr_t found_str_attr;			/* colors for marked string */
	attr_t pattern_data_attr;		/* colors for pattern line data */
	attr_t pattern_line_attr;		/* colors for pattern lide art */
} Theme;

#define		WINDOW_LUC				0
#define		WINDOW_FIX_ROWS			1
#define		WINDOW_FIX_COLS			2
#define		WINDOW_ROWS				3
#define		WINDOW_FOOTER			4
#define		WINDOW_TOP_BAR			5
#define		WINDOW_BOTTOM_BAR		6

int if_in_int(int v, const int *s, int v1, int v2);
int if_notin_int(int v, const int *s, int v1, int v2);
void initialize_color_pairs(int theme);
void initialize_theme(int theme, int window_identifier, bool is_tabular_fmt, Theme *t);

#endif