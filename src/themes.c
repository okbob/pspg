/*-------------------------------------------------------------------------
 *
 * themes.c
 *	  themes initialization
 *
 * Portions Copyright (c) 2017-2017 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/themes.c
 *
 *-------------------------------------------------------------------------
 */

#include "themes.h"
#include <string.h>

static int
if_in_int(int v, const int *s, int v1, int v2)
{
	while(*s != -1)
	{
		if (v == *s)
			return v1;
		s += 1;
	}
	return v2;
}

static int
if_notin_int(int v, const int *s, int v1, int v2)
{
	while(*s != -1)
	{
		if (v == *s)
			return v2;
		s += 1;
	}
	return v1;
}

/*
 * Set color pairs based on style
 */
void
initialize_color_pairs(int theme)
{
	switch (theme)
	{
		case 0:
			use_default_colors();

			init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
			init_pair(3, -1, -1);							/* data alphanumeric */
			init_pair(4, -1, -1);							/* fix rows, columns */
			init_pair(5, COLOR_BLACK, COLOR_WHITE);			/* active cursor over fixed cols */
			init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
			init_pair(7, COLOR_BLACK, COLOR_WHITE);			/* title color */
			init_pair(8, COLOR_BLACK, COLOR_WHITE);			/* expanded header */
			init_pair(9, -1, -1);							/* footer */
			init_pair(10, COLOR_BLACK, COLOR_WHITE);		/* footer cursor */
			init_pair(11, COLOR_BLACK, COLOR_WHITE);		/* cursor over decoration */
			init_pair(12, COLOR_BLACK, COLOR_WHITE);		/* bottom bar colors */
			init_pair(13, COLOR_BLACK, COLOR_WHITE);		/* light bottom bar colors */
			init_pair(14, COLOR_BLACK, COLOR_WHITE);		/* color of bookmark lines */
			init_pair(15, COLOR_WHITE, COLOR_BLACK);		/* color of marked search pattern */
			init_pair(16, -1, -1);							/* color of line with pattern */
			init_pair(17, -1, -1);							/* color of line art with pattern */
			init_pair(18, -1, -1);		/* color of marked search pattern in no-hl line mode */

			break;
		case 1:
			assume_default_colors(COLOR_WHITE, COLOR_BLUE);

			init_pair(2, COLOR_BLACK, COLOR_CYAN);
			init_pair(3, COLOR_WHITE, COLOR_BLUE);
			init_pair(4, COLOR_YELLOW, COLOR_BLUE);
			init_pair(5, COLOR_YELLOW, COLOR_CYAN);
			init_pair(6, COLOR_WHITE, COLOR_CYAN);
			init_pair(7, COLOR_BLACK, COLOR_CYAN);
			init_pair(8, COLOR_RED, COLOR_BLUE);
			init_pair(9, COLOR_CYAN, COLOR_BLUE);
			init_pair(10, COLOR_WHITE, COLOR_CYAN);
			init_pair(11, COLOR_WHITE, COLOR_CYAN);
			init_pair(12, COLOR_WHITE, COLOR_CYAN);
			init_pair(13, COLOR_YELLOW, COLOR_CYAN);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_GREEN, COLOR_BLUE);

			break;
		case 2:
			assume_default_colors(COLOR_WHITE, COLOR_CYAN);

			init_pair(2, COLOR_BLACK, COLOR_WHITE);
			init_pair(3, COLOR_WHITE, COLOR_CYAN);
			init_pair(4, COLOR_WHITE, COLOR_CYAN);
			init_pair(5, COLOR_WHITE, COLOR_BLUE);
			init_pair(6, COLOR_WHITE, COLOR_BLUE);
			init_pair(7, COLOR_YELLOW, COLOR_WHITE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_BLUE, COLOR_CYAN);
			init_pair(10, COLOR_WHITE, COLOR_BLUE);
			init_pair(11, COLOR_WHITE, COLOR_BLUE);
			init_pair(12, COLOR_WHITE, COLOR_BLUE);
			init_pair(13, COLOR_WHITE, COLOR_BLUE);
			init_pair(14, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_GREEN);

			break;
		case 3:
			assume_default_colors(COLOR_BLACK, COLOR_CYAN);

			init_pair(2, COLOR_BLACK, COLOR_WHITE);
			init_pair(3, COLOR_BLACK, COLOR_CYAN);
			init_pair(4, COLOR_WHITE, COLOR_CYAN);
			init_pair(5, COLOR_WHITE, COLOR_BLACK);
			init_pair(6, COLOR_CYAN, COLOR_BLACK);
			init_pair(7, COLOR_BLACK, COLOR_WHITE);
			init_pair(8, COLOR_WHITE, COLOR_CYAN);
			init_pair(9, COLOR_BLACK, COLOR_CYAN);
			init_pair(10, COLOR_CYAN, COLOR_BLACK);
			init_pair(11, COLOR_CYAN, COLOR_BLACK);
			init_pair(12, COLOR_CYAN, COLOR_BLACK);
			init_pair(13, COLOR_WHITE, COLOR_BLACK);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_WHITE, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_BLACK, COLOR_GREEN);
			init_pair(18, COLOR_WHITE, COLOR_GREEN);

			break;
		case 4:
			assume_default_colors(COLOR_BLACK, COLOR_WHITE);

			init_pair(2, COLOR_BLACK, COLOR_CYAN);
			init_pair(3, COLOR_BLACK, COLOR_WHITE);
			init_pair(4, COLOR_BLACK, COLOR_WHITE);
			init_pair(5, COLOR_WHITE, COLOR_BLUE);
			init_pair(6, COLOR_WHITE, COLOR_BLUE);
			init_pair(7, COLOR_BLACK, COLOR_CYAN);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_BLACK, COLOR_WHITE);
			init_pair(10, COLOR_WHITE, COLOR_BLUE);
			init_pair(11, COLOR_WHITE, COLOR_BLUE);
			init_pair(12, COLOR_WHITE, COLOR_BLUE);
			init_pair(13, COLOR_WHITE, COLOR_BLUE);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_BLACK, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_GREEN);

			break;
		case 5:
			use_default_colors();

			init_pair(2, COLOR_GREEN, COLOR_BLUE);
			init_pair(3, -1, -1);
			init_pair(4, COLOR_CYAN, -1);
			init_pair(5, COLOR_BLACK, COLOR_CYAN);
			init_pair(6, COLOR_BLACK, COLOR_CYAN);
			init_pair(7, COLOR_GREEN, COLOR_BLUE);
			init_pair(8, COLOR_BLACK, COLOR_BLUE);
			init_pair(9, COLOR_BLACK, COLOR_CYAN);
			init_pair(10, COLOR_BLACK, COLOR_CYAN);
			init_pair(11, -1, COLOR_CYAN);
			init_pair(12, COLOR_BLACK, COLOR_CYAN);
			init_pair(13, COLOR_BLACK, COLOR_CYAN);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, -1, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_GREEN);

			break;
		case 6:
			assume_default_colors(COLOR_WHITE, COLOR_BLACK);

			init_pair(2, COLOR_BLACK, COLOR_CYAN);
			init_pair(3, COLOR_WHITE, COLOR_BLACK);
			init_pair(4, COLOR_CYAN, COLOR_BLACK);
			init_pair(5, COLOR_WHITE, COLOR_BLUE);
			init_pair(6, COLOR_WHITE, COLOR_BLUE);
			init_pair(7, COLOR_BLACK, COLOR_CYAN);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_CYAN, COLOR_BLACK);
			init_pair(10, COLOR_WHITE, COLOR_BLUE);
			init_pair(11, COLOR_WHITE, COLOR_BLUE);
			init_pair(12, COLOR_WHITE, COLOR_BLUE);
			init_pair(13, COLOR_WHITE, COLOR_BLUE);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_BLACK);

			break;
		case 7:
			assume_default_colors(COLOR_GREEN, COLOR_BLACK);

			init_pair(2, COLOR_CYAN, COLOR_BLACK);
			init_pair(3, COLOR_GREEN, COLOR_BLACK);
			init_pair(4, COLOR_GREEN, COLOR_BLACK);
			init_pair(5, COLOR_WHITE, COLOR_GREEN);
			init_pair(6, COLOR_WHITE, COLOR_GREEN);
			init_pair(7, COLOR_CYAN, COLOR_BLACK);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_CYAN, COLOR_BLACK);
			init_pair(10, COLOR_WHITE, COLOR_GREEN);
			init_pair(11, COLOR_WHITE, COLOR_GREEN);
			init_pair(12, COLOR_WHITE, COLOR_GREEN);
			init_pair(13, COLOR_WHITE, COLOR_GREEN);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_WHITE, COLOR_CYAN);
			init_pair(16, COLOR_BLACK, COLOR_CYAN);
			init_pair(17, COLOR_GREEN, COLOR_CYAN);
			init_pair(18, COLOR_CYAN, COLOR_BLACK);

			break;
		case 8:
			assume_default_colors(COLOR_CYAN, COLOR_BLUE);

			init_pair(2, COLOR_WHITE, COLOR_BLUE);
			init_pair(3, COLOR_WHITE, COLOR_BLUE);
			init_pair(4, COLOR_WHITE, COLOR_BLUE);
			init_pair(5, COLOR_WHITE, COLOR_CYAN);
			init_pair(6, COLOR_WHITE, COLOR_CYAN);
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_WHITE, COLOR_BLUE);
			init_pair(10, COLOR_WHITE, COLOR_CYAN);
			init_pair(11, COLOR_BLUE, COLOR_CYAN);
			init_pair(12, COLOR_WHITE, COLOR_CYAN);
			init_pair(13, COLOR_WHITE, COLOR_CYAN);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_CYAN, COLOR_GREEN);
			init_pair(18, COLOR_CYAN, COLOR_BLUE);

			break;
		case 9:
			assume_default_colors(COLOR_WHITE, COLOR_BLUE);

			init_pair(2, COLOR_BLACK, COLOR_WHITE);
			init_pair(3, COLOR_WHITE, COLOR_BLUE);
			init_pair(4, COLOR_CYAN, COLOR_BLUE);
			init_pair(5, COLOR_WHITE, COLOR_CYAN);
			init_pair(6, COLOR_WHITE, COLOR_CYAN);
			init_pair(7, COLOR_BLACK, COLOR_WHITE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_WHITE, COLOR_BLUE);
			init_pair(10, COLOR_WHITE, COLOR_CYAN);
			init_pair(11, COLOR_WHITE, COLOR_CYAN);
			init_pair(12, COLOR_WHITE, COLOR_CYAN);
			init_pair(13, COLOR_WHITE, COLOR_CYAN);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_BLUE);

			break;
		case 10:
			assume_default_colors(COLOR_BLUE, COLOR_CYAN);

			init_pair(2, COLOR_BLUE, COLOR_CYAN);
			init_pair(3, COLOR_BLUE, COLOR_CYAN);
			init_pair(4, COLOR_WHITE, COLOR_CYAN);
			init_pair(5, COLOR_WHITE, COLOR_BLUE);
			init_pair(6, COLOR_WHITE, COLOR_BLUE);
			init_pair(7, COLOR_BLUE, COLOR_CYAN);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_BLUE, COLOR_CYAN);
			init_pair(10, COLOR_WHITE, COLOR_BLUE);
			init_pair(11, COLOR_CYAN, COLOR_BLUE);
			init_pair(12, COLOR_WHITE, COLOR_BLUE);
			init_pair(13, COLOR_WHITE, COLOR_BLUE);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_MAGENTA);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_BLUE, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_CYAN);

			break;
		case 11:
			assume_default_colors(COLOR_CYAN, COLOR_BLACK);

			init_pair(2, COLOR_WHITE, COLOR_BLUE);
			init_pair(3, COLOR_CYAN, COLOR_BLACK);
			init_pair(4, COLOR_CYAN, COLOR_BLACK);
			init_pair(5, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(6, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_WHITE, COLOR_BLACK);
			init_pair(10, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(11, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(12, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(13, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_CYAN, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_BLACK);

			break;
		case 12:
			assume_default_colors(COLOR_BLUE, COLOR_CYAN);

			init_pair(2, COLOR_BLUE, COLOR_CYAN);
			init_pair(3, COLOR_WHITE, COLOR_CYAN);
			init_pair(4, COLOR_BLUE, COLOR_CYAN);
			init_pair(5, COLOR_WHITE, COLOR_BLUE);
			init_pair(6, COLOR_WHITE, COLOR_BLUE);
			init_pair(7, COLOR_BLUE, COLOR_CYAN);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_BLUE, COLOR_CYAN);
			init_pair(10, COLOR_WHITE, COLOR_BLUE);
			init_pair(11, COLOR_CYAN, COLOR_BLUE);
			init_pair(12, COLOR_WHITE, COLOR_BLUE);
			init_pair(13, COLOR_WHITE, COLOR_BLUE);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_MAGENTA);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_BLUE, COLOR_GREEN);
			init_pair(18, COLOR_YELLOW, COLOR_CYAN);

			break;
		case 13:
			assume_default_colors(COLOR_WHITE, COLOR_BLUE);

			init_pair(2, COLOR_WHITE, COLOR_BLUE);
			init_pair(3, COLOR_WHITE, COLOR_BLUE);
			init_pair(4, COLOR_WHITE, COLOR_BLUE);
			init_pair(5, COLOR_BLACK, COLOR_CYAN);
			init_pair(6, COLOR_BLACK, COLOR_CYAN);
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_WHITE, COLOR_BLUE);
			init_pair(10, COLOR_BLACK, COLOR_CYAN);
			init_pair(11, COLOR_WHITE, COLOR_CYAN);
			init_pair(12, COLOR_WHITE, COLOR_BLACK);
			init_pair(13, COLOR_WHITE, COLOR_BLACK);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_CYAN, COLOR_BLUE);

			break;
		case 14:
			assume_default_colors(COLOR_WHITE, COLOR_BLUE);

			init_pair(2, COLOR_WHITE, COLOR_BLUE);
			init_pair(3, COLOR_WHITE, COLOR_BLUE);
			init_pair(4, COLOR_MAGENTA, COLOR_BLUE);
			init_pair(5, COLOR_BLACK, COLOR_CYAN);
			init_pair(6, COLOR_BLACK, COLOR_CYAN);
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_BLUE);
			init_pair(9, COLOR_WHITE, COLOR_BLUE);
			init_pair(10, COLOR_BLACK, COLOR_CYAN);
			init_pair(11, COLOR_WHITE, COLOR_CYAN);
			init_pair(12, COLOR_WHITE, COLOR_BLACK);
			init_pair(13, COLOR_WHITE, COLOR_BLACK);
			init_pair(14, COLOR_WHITE, COLOR_RED);
			init_pair(15, COLOR_YELLOW, COLOR_GREEN);
			init_pair(16, COLOR_BLACK, COLOR_GREEN);
			init_pair(17, COLOR_WHITE, COLOR_GREEN);
			init_pair(18, COLOR_CYAN, COLOR_BLUE);

		break;
	}
}

#define _in			if_in_int
#define _notin		if_notin_int

void
initialize_theme(int theme, int window_identifier, bool is_tabular_fmt, bool no_highlight_lines, Theme *t)
{
	memset(t, 0, sizeof(Theme));

	switch (window_identifier)
	{
		case WINDOW_LUC:
		case WINDOW_FIX_ROWS:
			t->data_attr = COLOR_PAIR(4);
			t->data_attr |= _notin(theme, (int[]) {12, -1}, A_BOLD, 0);
			break;

		case WINDOW_TOP_BAR:
			t->title_attr = COLOR_PAIR(7);
			t->title_attr |= _in(theme, (int[]) {2, -1}, A_BOLD, 0);
			break;

		case WINDOW_BOTTOM_BAR:
			t->prompt_attr = _in(theme, (int[]) {0, 1, -1}, COLOR_PAIR(2), COLOR_PAIR(13));
			t->bottom_attr = COLOR_PAIR(12);
			t->bottom_light_attr = COLOR_PAIR(13);

			t->prompt_attr |= _notin(theme, (int[]) {0, 1, -1}, A_BOLD, 0);
			t->bottom_attr |= _notin(theme, (int[]) {13, 14, -1}, A_BOLD, 0);
			t->bottom_light_attr |= A_BOLD;
			break;

		case WINDOW_FIX_COLS:
			t->data_attr = COLOR_PAIR(4);
			t->line_attr = 0;
			t->expi_attr = COLOR_PAIR(8);
			t->cursor_data_attr = COLOR_PAIR(5);
			t->cursor_line_attr = COLOR_PAIR(11);
			t->cursor_expi_attr = COLOR_PAIR(6);
			t->bookmark_data_attr = COLOR_PAIR(14);
			t->bookmark_line_attr = COLOR_PAIR(14);
			t->cursor_bookmark_attr = COLOR_PAIR(14);
			t->found_str_attr = !no_highlight_lines ? COLOR_PAIR(15) : COLOR_PAIR(18);
			t->pattern_data_attr = COLOR_PAIR(16);
			t->pattern_line_attr = COLOR_PAIR(17);

			t->data_attr |= _notin(theme, (int[]) { 12, -1}, A_BOLD, 0);
			t->line_attr |= 0;
			t->expi_attr |= A_BOLD;
			t->cursor_data_attr |= _notin(theme, (int[]) { 13, 14, -1}, A_BOLD, 0);
			t->cursor_line_attr |= 0;
			t->cursor_expi_attr |= A_BOLD;
			t->bookmark_data_attr |= A_BOLD;
			t->bookmark_line_attr |= 0;
			t->cursor_bookmark_attr |= A_REVERSE | A_BOLD;
			t->found_str_attr |= no_highlight_lines ? (_in(theme, (int[]){0, -1}, A_REVERSE, A_BOLD)) : A_BOLD;
			t->pattern_data_attr |= _in(theme, (int[]) {0, -1}, A_REVERSE, 0);
			t->pattern_line_attr |= _in(theme, (int[]) {11, 7, 8, -1}, A_BOLD, 0) | _in(theme, (int[]) {0, -1}, A_REVERSE, 0);
			break;

		case WINDOW_ROWS:
			t->data_attr = COLOR_PAIR(3);
			t->line_attr = 0;
			t->expi_attr = COLOR_PAIR(8);
			t->cursor_data_attr = COLOR_PAIR(6);
			t->cursor_line_attr = COLOR_PAIR(11);
			t->cursor_expi_attr = COLOR_PAIR(6);
			t->bookmark_data_attr = COLOR_PAIR(14);
			t->bookmark_line_attr = COLOR_PAIR(14);
			t->cursor_bookmark_attr = COLOR_PAIR(14);
			t->found_str_attr = !no_highlight_lines ? COLOR_PAIR(15) : COLOR_PAIR(18);
			t->pattern_data_attr = COLOR_PAIR(16);
			t->pattern_line_attr = COLOR_PAIR(17);

			t->data_attr |= _in(theme, (int[]) { 2, 12, 13, 14, -1}, A_BOLD, 0);
			t->line_attr |= !is_tabular_fmt ? (_in(theme, (int[]) { 2, -1}, A_BOLD, 0)) : 0;
			t->expi_attr |= A_BOLD;
			t->cursor_data_attr |= _notin(theme, (int[]) { 13, 14, -1}, A_BOLD, 0);
			t->cursor_line_attr |= !is_tabular_fmt ? A_BOLD : 0;
			t->cursor_expi_attr |= A_BOLD;
			t->bookmark_data_attr |= A_BOLD;
			t->bookmark_line_attr |= 0;
			t->cursor_bookmark_attr |= A_REVERSE | A_BOLD;
			t->found_str_attr |= no_highlight_lines ? (_in(theme, (int[]){0, -1}, A_REVERSE, A_BOLD)) : A_BOLD;
			t->pattern_data_attr |= _in(theme, (int[]) {0, -1}, A_REVERSE, 0);
			t->pattern_line_attr |= _in(theme, (int[]) {11, 7, 8, -1}, A_BOLD, 0) | _in(theme, (int[]) {0, -1}, A_REVERSE, 0);
			break;

		case WINDOW_FOOTER:
			t->data_attr = is_tabular_fmt ? COLOR_PAIR(9) : COLOR_PAIR(3);
			t->line_attr = 0;
			t->expi_attr = 0;
			t->cursor_data_attr = COLOR_PAIR(10);
			t->cursor_line_attr = 0;
			t->cursor_expi_attr = 0;
			t->bookmark_data_attr = COLOR_PAIR(14);
			t->bookmark_line_attr = COLOR_PAIR(14);
			t->cursor_bookmark_attr = COLOR_PAIR(14);
			t->found_str_attr = !no_highlight_lines ? COLOR_PAIR(15) : COLOR_PAIR(18);
			t->pattern_data_attr = COLOR_PAIR(16);
			t->pattern_line_attr = COLOR_PAIR(17);

			t->data_attr |= !is_tabular_fmt ? (_in(theme, (int[]) { 2, 12, 13, 14, -1}, A_BOLD, 0)) : 0;
			t->line_attr |= 0,
			t->expi_attr |= 0;
			t->cursor_data_attr |= _notin(theme, (int[]) { 13, 14, -1}, A_BOLD, 0);
			t->cursor_line_attr |= 0;
			t->cursor_expi_attr |= 0;
			t->bookmark_data_attr |= A_BOLD;
			t->bookmark_line_attr |= 0;
			t->cursor_bookmark_attr |= A_BOLD | A_REVERSE;
			t->found_str_attr |= no_highlight_lines ? (_in(theme, (int[]){0, -1}, A_REVERSE, A_BOLD)) : A_BOLD;
			t->pattern_data_attr |= 0;
			t->pattern_line_attr |= 0;
			break;
	}

	if (no_highlight_lines)
	{
		t->pattern_data_attr = t->data_attr;
		t->pattern_line_attr = t->line_attr;
	}
}
