/*-------------------------------------------------------------------------
 *
 * themes.c
 *	  themes initialization
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/themes.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"
#include "themes.h"
#include <string.h>
#include <stdbool.h>

attr_t		theme_attrs[50];

#define theme_attr(id)		(COLOR_PAIR(id) | theme_attrs[id])

#ifndef A_ITALIC
#define A_ITALIC	A_DIM
#endif

int		ncurses_colorpair_index = 0;

static void
set_colour(short id, short foreground, short background, bool light, attr_t attrs)
{
	if (COLORS == 8 || foreground == -1)
	{
		init_pair(id, foreground, background);
		theme_attrs[id] = attrs | (light ? A_BOLD : 0);
	}
	else if (foreground < 8)
	{
		init_pair(id, foreground + (light ? 8 : 0), background);
		theme_attrs[id] = attrs;
	}
	else
	{
		init_pair(id, foreground, background);
		theme_attrs[id] = attrs;
	}
}

/* 0..255 rgb based colors */
static void
init_color_rgb_ff(short color, short r, short g, short b)
{
	init_color(color,
			   (r / 255.0) * 1000.0,
			   (g / 255.0) * 1000.0,
			   (b / 255.0) * 1000.0);
}

typedef enum
{
	PSPG_BLACK_COLOR,
	PSPG_RED_COLOR,
	PSPG_GREEN_COLOR,
	PSPG_BROWN_COLOR,
	PSPG_BLUE_COLOR,
	PSPG_MAGENTA_COLOR,
	PSPG_CYAN_COLOR,
	PSPG_LIGHT_GRAY_COLOR,
	PSPG_GRAY_COLOR,
	PSPG_BRIGHT_RED_COLOR,
	PSPG_BRIGHT_GREEN_COLOR,
	PSPG_YELLOW_COLOR,
	PSPG_BRIGHT_BLUE_COLOR,
	PSPG_BRIGHT_MAGENTA_COLOR,
	PSPG_BRIGHT_CYAN_COLOR,
	PSPG_WHITE_COLOR,
	PSPG_DEFAULT_COLOR
} PspgBasicColor;

typedef enum
{
	PSPG_COLOR_BASIC,
	PSPG_COLOR_256,
	PSPG_COLOR_RGB
} PspgColorPallet;

typedef enum
{
	PSPG_INDEPENDENT = 0,
	PSPG_CURSOR_BOLD,
	PSPG_LABEL_BOLD,
} PspgStyleDependency;

typedef struct
{
	PspgColorPallet	cp;
	PspgBasicColor	bc;
	unsigned int	rgb;
} PspgColor;

typedef struct
{
	int		fg;
	int		bg;
	int		color_pair_number;
} ColorPairCacheItem;

ColorPairCacheItem ColorPairCache[255];
int		nColorPairCache;

const PspgColor PspgBlack = {PSPG_COLOR_BASIC, PSPG_BLACK_COLOR, 0};
const PspgColor PspgRed = {PSPG_COLOR_BASIC, PSPG_RED_COLOR, 0};
const PspgColor PspgGreen = {PSPG_COLOR_BASIC, PSPG_GREEN_COLOR, 0};
const PspgColor PspgBrown = {PSPG_COLOR_BASIC, PSPG_BROWN_COLOR, 0};
const PspgColor PspgBlue = {PSPG_COLOR_BASIC, PSPG_BLUE_COLOR, 0};
const PspgColor PspgMagenta = {PSPG_COLOR_BASIC, PSPG_MAGENTA_COLOR, 0};
const PspgColor PspgCyan = {PSPG_COLOR_BASIC, PSPG_CYAN_COLOR, 0};
const PspgColor PspgLightGray = {PSPG_COLOR_BASIC, PSPG_LIGHT_GRAY_COLOR, 0};
const PspgColor PspgGray = {PSPG_COLOR_BASIC, PSPG_GRAY_COLOR, 0};
const PspgColor PspgBrightRed = {PSPG_COLOR_BASIC, PSPG_BRIGHT_RED_COLOR, 0};
const PspgColor PspgBrightGreen = {PSPG_COLOR_BASIC, PSPG_BRIGHT_GREEN_COLOR, 0};
const PspgColor PspgYellow = {PSPG_COLOR_BASIC, PSPG_YELLOW_COLOR, 0};
const PspgColor PspgBrightBlue = {PSPG_COLOR_BASIC, PSPG_BRIGHT_BLUE_COLOR, 0};
const PspgColor PspgBrightMagenta = {PSPG_COLOR_BASIC, PSPG_BRIGHT_MAGENTA_COLOR, 0};
const PspgColor PspgBrightCyan = {PSPG_COLOR_BASIC, PSPG_BRIGHT_CYAN_COLOR, 0};
const PspgColor PspgWhite = {PSPG_COLOR_BASIC, PSPG_WHITE_COLOR, 0};
const PspgColor PspgDefault = {PSPG_COLOR_BASIC, PSPG_DEFAULT_COLOR, 0};

typedef struct
{
	PspgColor		fg;
	PspgColor		bg;
	int				attr;
} PspgThemeElement;

typedef enum
{
	PspgTheme_background,
	PspgTheme_data,
	PspgTheme_border,
	PspgTheme_label,
	PspgTheme_rownum,
	PspgTheme_recnum,
	PspgTheme_selection,
	PspgTheme_footer,
	PspgTheme_cursor_data,
	PspgTheme_cursor_border,
	PspgTheme_cursor_label,
	PspgTheme_cursor_rownum,
	PspgTheme_cursor_recnum,
	PspgTheme_cursor_selection,
	PspgTheme_cursor_footer,
	PspgTheme_scrollbar_arrows,
	PspgTheme_scrollbar_background,
	PspgTheme_scrollbar_slider,
	PspgTheme_scrollbar_active_slider,
	PspgTheme_title,
	PspgTheme_status_bar,
	PspgTheme_prompt_bar,
	PspgTheme_info_bar,
	PspgTheme_error_bar,
	PspgTheme_input_bar,
	PspgTheme_bookmark,
	PspgTheme_bookmark_border,
	PspgTheme_cursor_bookmark,
	PspgTheme_cross_cursor,
	PspgTheme_cross_cursor_border,
	PspgTheme_pattern,
	PspgTheme_pattern_nohl,
	PspgTheme_pattern_line,
	PspgTheme_pattern_line_border,
	PspgTheme_pattern_cursor,
	PspgTheme_pattern_line_vertical_cursor,
	PspgTheme_pattern_line_vertical_cursor_border,

	PspgTheme_error
} PspgThemeElements;

static PspgStyleDependency
styledep(PspgThemeElements el)
{
	switch (el)
	{
		case PspgTheme_cursor_data:
		case PspgTheme_cursor_label:
		case PspgTheme_cursor_rownum:
		case PspgTheme_cursor_recnum:
		case PspgTheme_cursor_selection:
		case PspgTheme_cursor_footer:
		case PspgTheme_cursor_bookmark:
		case PspgTheme_cross_cursor:
			return PSPG_CURSOR_BOLD;

		case PspgTheme_label:
			return PSPG_LABEL_BOLD;

		default:
			return PSPG_INDEPENDENT;
	}
}



PspgThemeElement themedef[50];


void
deftheme(PspgThemeElements idx, PspgColor fg, PspgColor bg, int attr)
{
	memcpy(&themedef[idx].fg, &fg, sizeof(PspgColor));
	memcpy(&themedef[idx].bg, &bg, sizeof(PspgColor));
	themedef[idx].attr = attr;
}

int
ncurses_color(PspgBasicColor cv, bool *isbright)
{
	*isbright = false;

	switch (cv)
	{
		case PSPG_BLACK_COLOR:
			return COLOR_BLACK;
		case PSPG_RED_COLOR:
			return COLOR_RED;
		case PSPG_GREEN_COLOR:
			return COLOR_GREEN;
		case PSPG_BROWN_COLOR:
			return COLOR_YELLOW;
		case PSPG_BLUE_COLOR:
			return COLOR_BLUE;
		case PSPG_MAGENTA_COLOR:
			return COLOR_MAGENTA;
		case PSPG_CYAN_COLOR:
			return COLOR_CYAN;
		case PSPG_LIGHT_GRAY_COLOR:
			return COLOR_WHITE;
		case PSPG_GRAY_COLOR:
			*isbright = true;
			return COLOR_BLACK;
		case PSPG_BRIGHT_RED_COLOR:
			*isbright = true;
			return COLOR_RED;
		case PSPG_BRIGHT_GREEN_COLOR:
			*isbright = true;
			return COLOR_GREEN;
		case PSPG_YELLOW_COLOR:
			*isbright = true;
			return COLOR_YELLOW;
		case PSPG_BRIGHT_BLUE_COLOR:
			*isbright = true;
			return COLOR_BLUE;
		case PSPG_BRIGHT_MAGENTA_COLOR:
			*isbright = true;
			return COLOR_MAGENTA;
		case PSPG_BRIGHT_CYAN_COLOR:
			*isbright = true;
			return COLOR_CYAN;
		case PSPG_WHITE_COLOR:
			*isbright = true;
			return COLOR_WHITE;
		case PSPG_DEFAULT_COLOR:
			return -1;
	}

	return -1;
}

attr_t
ncurses_theme_attr(PspgThemeElements idx)
{
	int		bgcolor;
	int		fgcolor;
	bool	bgcolorbright;
	bool	fgcolorbright;
	attr_t	result;
	int		i;
	PspgStyleDependency dep;

	PspgThemeElement *te = &themedef[idx];

	result = te->attr;

	dep = styledep(idx);

	if (dep != PSPG_INDEPENDENT)
	{
		if (current_state && current_state->opts)
		{
			bool bold_cursor = current_state->opts->bold_cursor;
			bool bold_labels = current_state->opts->bold_labels;

			if ((dep == PSPG_CURSOR_BOLD && bold_cursor) ||
				(dep == PSPG_LABEL_BOLD && bold_labels))
				result |= A_BOLD;
		}
	}

	if (te->fg.cp == PSPG_COLOR_BASIC &&
		te->bg.cp == PSPG_COLOR_BASIC)
	{
		fgcolor = ncurses_color(te->fg.bc, &fgcolorbright);
		bgcolor = ncurses_color(te->bg.bc, &bgcolorbright);

		if (COLORS == 8)
		{
			/*
			 * cannot to use bright color on background and
			 * foreground together
			 */
			if (fgcolorbright && bgcolorbright)
				bgcolorbright = false;

			if (bgcolorbright && (result & A_REVERSE))
				bgcolorbright = false;

			if (fgcolorbright)
				result |= A_BOLD;
			else if (bgcolorbright)
			{
				int		aux = fgcolor;

				fgcolor = bgcolor;
				bgcolor = aux;
				result |= A_BOLD | A_REVERSE;
			}
		}
		else
		{
			if (fgcolorbright)
				fgcolor += 8;
			if (bgcolorbright)
				bgcolor += 8;
		}

		/* try to find color pair in cache */
		for (i = 0; i < nColorPairCache; i++)
		{
			if (ColorPairCache[i].fg == fgcolor &&
				ColorPairCache[i].bg == bgcolor)
			{
				result |= COLOR_PAIR(ColorPairCache[i].color_pair_number);
				return result;
			}
		}

		/*
		 * The number of color pairs can be limmited, so try
		 * to reuse it.
		 */
		init_pair(ncurses_colorpair_index, fgcolor, bgcolor);
		result |= COLOR_PAIR(ncurses_colorpair_index);

		ColorPairCache[nColorPairCache].fg = fgcolor;
		ColorPairCache[nColorPairCache].bg = bgcolor;
		ColorPairCache[nColorPairCache++].color_pair_number = ncurses_colorpair_index++;
	}

	return result;
}

/*
 * Set color pairs based on style
 */
void
initialize_color_pairs(int theme, bool bold_labels, bool bold_cursor)
{
	attr_t labels_attr = bold_labels ? A_BOLD : 0;
	attr_t cursor_attr = bold_cursor ? A_BOLD : 0;

	ncurses_colorpair_index = 1;
	nColorPairCache = 0;

	memset(theme_attrs, 0, sizeof(theme_attrs));

	switch (theme)
	{
		case 0:
			/* mc black theme */
			use_default_colors();

			deftheme(PspgTheme_background, PspgDefault, PspgDefault, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_border, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_label, PspgDefault, PspgDefault, A_BOLD);
			deftheme(PspgTheme_rownum, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_recnum, PspgBlack, PspgLightGray, A_BOLD);
			deftheme(PspgTheme_footer, PspgDefault, PspgDefault, 0);

			deftheme(PspgTheme_cursor_data, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_cursor_border, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_cursor_label, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_cursor_footer, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_scrollbar_background, PspgDefault, PspgDefault, A_DIM);
			deftheme(PspgTheme_scrollbar_slider, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgDefault, PspgDefault, A_REVERSE);

			deftheme(PspgTheme_title, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_info_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgDefault, PspgDefault, 0);

			deftheme(PspgTheme_bookmark, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_bookmark_border, PspgBlack, PspgLightGray, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgLightGray, PspgBlack, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cross_cursor_border, PspgDefault, PspgDefault, A_REVERSE);

			deftheme(PspgTheme_selection, PspgLightGray, PspgBlack, A_DIM | A_BOLD | A_REVERSE);
			deftheme(PspgTheme_cursor_selection, PspgLightGray, PspgGray, A_BOLD);

			deftheme(PspgTheme_pattern, PspgLightGray, PspgBlack, A_BOLD | A_REVERSE);
			deftheme(PspgTheme_pattern_nohl, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_pattern_line, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_pattern_line_border, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_pattern_cursor, PspgLightGray, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlack, PspgLightGray, 0);
			break;

		case 1:
			/* mc theme */
			deftheme(PspgTheme_background, PspgLightGray, PspgBlue, 0);

			ncurses_theme_attr(PspgTheme_background);

			/* 3 */ deftheme(PspgTheme_data, PspgLightGray, PspgBlue, 0);
			/* -1- */ deftheme(PspgTheme_border, PspgLightGray, PspgBlue, 0);
			/* 4 */ deftheme(PspgTheme_label, PspgYellow, PspgBlue, 0);
			/* 21 */ deftheme(PspgTheme_rownum, PspgWhite, PspgCyan, 0);
			/* 8 */ deftheme(PspgTheme_recnum, PspgRed, PspgBlue, A_BOLD);
			/* 9 */ deftheme(PspgTheme_footer, PspgCyan, PspgBlue, 0);

			/* 6 */ deftheme(PspgTheme_cursor_data, PspgBlack, PspgCyan, 0);
			/* 11 */ deftheme(PspgTheme_cursor_border, PspgLightGray, PspgCyan, 0);
			/* 5 */ deftheme(PspgTheme_cursor_label, PspgYellow, PspgCyan, 0);
			/* -10- */ deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0);
			/* -6- */ deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgCyan, 0);
			/* 10 */ deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0);

			/* 30 */ deftheme(PspgTheme_scrollbar_arrows, PspgLightGray, PspgBlue, 0);
			/* 31 */ deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0);
			/* 32 */ deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0);
			/* 33 */ deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			/* 7 */ deftheme(PspgTheme_title, PspgBlack, PspgCyan, 0);
			/* 2 */ deftheme(PspgTheme_status_bar, PspgBlack, PspgCyan, 0);
			/* -2- */ deftheme(PspgTheme_prompt_bar, PspgBlack, PspgCyan, 0);
			/* 13 */ deftheme(PspgTheme_info_bar, PspgBlack, PspgGreen, 0);
			/* 26 */ deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			/* 27 */ deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			/* 14 */ deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, 0);
			/* 28 */ deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			/* 14 */ deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			/* 22 */ deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			/* 23 */ deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0);

			/* 34 */ deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			/* 35 */ deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			/* 15 */ deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			/* 18 */ deftheme(PspgTheme_pattern_nohl, PspgGreen, PspgBlue, 0);
			/* 16 */ deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			/* 17 */ deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0);
			/* 20 */ deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			/* 24 */ deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			/* 25 */ deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 2:
			/* FoxPro theme */
			deftheme(PspgTheme_background, PspgLightGray, PspgCyan, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_border, PspgLightGray, PspgCyan, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgCyan, 0);
			deftheme(PspgTheme_recnum, PspgWhite, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlue, PspgCyan, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgYellow, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_background, PspgLightGray, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgYellow, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgYellow, PspgLightGray, A_BOLD);

			deftheme(PspgTheme_title, PspgYellow, PspgLightGray, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_error_bar, PspgYellow, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgMagenta, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_bookmark, PspgWhite, PspgMagenta, A_BOLD | A_REVERSE);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightBlue, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightBlue, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 3:
			/* PD Menu theme */
			deftheme(PspgTheme_background, PspgBlack, PspgCyan, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_border, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_rownum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_recnum, PspgWhite, PspgCyan, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_cursor_data, PspgBrightCyan, PspgBlack, 0);
			deftheme(PspgTheme_cursor_border, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBrightCyan, PspgBlack, 0);
			deftheme(PspgTheme_cursor_recnum, PspgBrightCyan, PspgBlack, 0);
			deftheme(PspgTheme_cursor_footer, PspgBrightCyan, PspgBlack, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgWhite, PspgBlack, A_REVERSE);
			deftheme(PspgTheme_scrollbar_background, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlack, PspgBlue, A_REVERSE);
			deftheme(PspgTheme_scrollbar_active_slider, PspgWhite, PspgBlack, A_REVERSE);

			deftheme(PspgTheme_title, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgCyan, PspgWhite, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgWhite, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlack, PspgBrightGreen, 0);
			break;

		case 4:
			/* White theme */
			deftheme(PspgTheme_background, PspgBlack, PspgLightGray, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_border, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_label, PspgBlack, PspgLightGray, A_BOLD);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_background, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBlack, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlack, PspgBrightGreen, 0);
			break;

		case 5:
			/* Mutt theme */
			use_default_colors();

			deftheme(PspgTheme_background, PspgDefault, PspgDefault, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_border, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_label, PspgBrightCyan, PspgDefault, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgBlack, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_cursor_data, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgDefault, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgLightGray, PspgBlack, A_REVERSE);
			deftheme(PspgTheme_scrollbar_background, PspgLightGray, PspgGray, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBrightGreen, PspgBlue, 0);
			deftheme(PspgTheme_status_bar, PspgBrightGreen, PspgBlue, 0);
			deftheme(PspgTheme_prompt_bar, PspgBrightGreen, PspgBlue, 0);
			deftheme(PspgTheme_info_bar, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBlack, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgDefault, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgCyan, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgWhite, PspgBrightGreen, 0);
			break;

		case 6:
			/* PC Fand theme */
			deftheme(PspgTheme_background, PspgLightGray, PspgBlack, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_border, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_label, PspgBrightCyan, PspgBlack, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgCyan, PspgBlack, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgLightGray, PspgBlack, A_REVERSE);
			deftheme(PspgTheme_scrollbar_background, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgCyan, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgBlack, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgBrightCyan, PspgBlue, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 7:
			/* Green theme */
			deftheme(PspgTheme_background, PspgGreen, PspgBlack, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgGreen, PspgBlack, 0);
			deftheme(PspgTheme_border, PspgGreen, PspgBlack, 0);
			deftheme(PspgTheme_label, PspgBrightGreen, PspgBlack, 0);
			deftheme(PspgTheme_rownum, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgCyan, PspgBlack, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_cursor_border, PspgBrightGreen, PspgGreen, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgGreen, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBrightGreen, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_background, PspgGreen, PspgBlack, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgBrightGreen, 0);

			deftheme(PspgTheme_title, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_status_bar, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_prompt_bar, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgBrightGreen, PspgRed, 0);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgGreen, PspgBrightGreen, 0);

			deftheme(PspgTheme_selection, PspgBrightGreen, PspgBlack, A_DIM | A_REVERSE);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgBrightGreen, 0);

			deftheme(PspgTheme_pattern, PspgWhite, PspgCyan, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_pattern_line_border, PspgGreen, PspgCyan, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgGreen, PspgBrightCyan, 0);
			break;

		case 8:
			/* Blue theme */
			deftheme(PspgTheme_background, PspgCyan, PspgBlue, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_border, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_rownum, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgLightGray, PspgBlue, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgBrightCyan, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_scrollbar_background, PspgLightGray, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgWhite, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgBlack, 0);

			deftheme(PspgTheme_title, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_status_bar, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_prompt_bar, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgCyan, PspgBrightCyan, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgBrightCyan, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgYellow, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgCyan, PspgBrightGreen, 0);
			break;

		case 9:
			/* Word Perfect theme */
			deftheme(PspgTheme_background, PspgLightGray, PspgBlue, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_label, PspgBrightCyan, PspgBlue, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBrightGreen, PspgBlue, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_background, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgLightGray, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgYellow, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 10:
			/* low contrast theme */
			deftheme(PspgTheme_background, PspgBlue, PspgCyan, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_border, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_rownum, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlue, PspgCyan, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_border, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_scrollbar_background, PspgGray, PspgCyan, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgBrightCyan, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_status_bar, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgCyan, PspgBlack, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgGreen, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgGreen, PspgCyan, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgBlue, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlue, PspgBrightGreen, 0);
			break;

		case 11:
			/* Dark cyan theme */
			deftheme(PspgTheme_background, PspgCyan, PspgBlack, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_border, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_label, PspgBrightCyan, PspgBlack, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgWhite, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgLightGray, PspgBlack, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgMagenta, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_status_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_prompt_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgMagenta, PspgWhite, A_DIM | A_REVERSE);
			deftheme(PspgTheme_cross_cursor_border, PspgMagenta, PspgLightGray, A_DIM | A_REVERSE);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgBrightMagenta, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgYellow, PspgBlack, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgCyan, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgCyan, PspgBrightGreen, 0);
			break;

		case 12:
			/* Paradox like theme */
			deftheme(PspgTheme_background, PspgBlue, PspgCyan, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_border, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_label, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_rownum, PspgBrightCyan, PspgCyan, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlue, PspgCyan, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_border, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgBrightCyan, 0);

			deftheme(PspgTheme_title, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_status_bar, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlue, PspgCyan, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgCyan, PspgBlack, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBrightCyan, PspgBlack, A_DIM| A_REVERSE);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgGreen, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlue, PspgBrightGreen, 0);
			break;

		case 13:
			/* DBase retro theme */
			deftheme(PspgTheme_background, PspgWhite, PspgBlue, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgLightGray, PspgBlue, 0);

			deftheme(PspgTheme_cursor_data, PspgYellow, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgYellow, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgYellow, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_status_bar, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_prompt_bar, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0);

			deftheme(PspgTheme_selection, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgBrightMagenta, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 14:
			/* DBase retro magenta */
			deftheme(PspgTheme_background, PspgLightGray, PspgBlue, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_border, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_label, PspgBrightMagenta, PspgBlue, 0);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_recnum, PspgLightGray, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgLightGray, PspgBlue, 0);

			deftheme(PspgTheme_cursor_data, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgLightGray, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgWhite, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_status_bar, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_prompt_bar, PspgLightGray, PspgBlue, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgWhite, PspgBlue, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0);

			deftheme(PspgTheme_selection, PspgWhite, PspgMagenta, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgBrightMagenta, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgWhite, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 15:
			/* Red theme */
			deftheme(PspgTheme_background, PspgBlack, PspgLightGray, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_border, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_label, PspgRed, PspgLightGray, A_DIM);
			deftheme(PspgTheme_rownum, PspgLightGray, PspgBlack, 0);
			deftheme(PspgTheme_recnum, PspgWhite, PspgBlue, A_BOLD);
			deftheme(PspgTheme_footer, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_cursor_data, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_border, PspgBlack, PspgRed, 0);
			deftheme(PspgTheme_cursor_label, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_rownum, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_recnum, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_cursor_footer, PspgWhite, PspgRed, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_background, PspgGray, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_slider, PspgBlack, PspgBlack, A_DIM | A_REVERSE);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgRed, 0);

			deftheme(PspgTheme_title, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_status_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_prompt_bar, PspgBlack, PspgLightGray, 0);
			deftheme(PspgTheme_info_bar, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_bookmark, PspgRed, PspgYellow, A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgBlack, PspgYellow, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgYellow, PspgRed, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgRed, PspgBlack, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgYellow, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgBrightRed, 0);

			deftheme(PspgTheme_pattern, PspgBlack, PspgYellow, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgCyan, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgWhite, A_BOLD);
			deftheme(PspgTheme_pattern_line_border, PspgBlack, PspgWhite, 0);
			deftheme(PspgTheme_pattern_cursor, PspgLightGray, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightRed, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgBlack, PspgBrightRed, 0);
			break;

		case 16:
			/* Simple theme */
			use_default_colors();

			deftheme(PspgTheme_background, PspgDefault, PspgDefault, 0);

			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_border, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_label, PspgDefault, PspgDefault, A_ITALIC);
			deftheme(PspgTheme_rownum, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_recnum, PspgDefault, PspgDefault, A_BOLD);
			deftheme(PspgTheme_footer, PspgDefault, PspgDefault, 0);

			deftheme(PspgTheme_cursor_data, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cursor_border, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cursor_label, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cursor_rownum, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cursor_recnum, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_cursor_footer, PspgDefault, PspgDefault, A_REVERSE);

			deftheme(PspgTheme_scrollbar_arrows, PspgDefault, PspgDefault, A_BOLD);
			deftheme(PspgTheme_scrollbar_background, PspgDefault, PspgDefault, A_DIM);
			deftheme(PspgTheme_scrollbar_slider, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_scrollbar_active_slider, PspgDefault, PspgDefault, A_REVERSE);

			deftheme(PspgTheme_title, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_status_bar, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_prompt_bar, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_info_bar, PspgDefault, PspgDefault, A_BOLD);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, A_REVERSE);
			deftheme(PspgTheme_input_bar, PspgDefault, PspgDefault, 0);

			deftheme(PspgTheme_bookmark, PspgDefault, PspgDefault, A_UNDERLINE | A_BOLD);
			deftheme(PspgTheme_bookmark_border, PspgDefault, PspgDefault, A_UNDERLINE | A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgDefault, PspgDefault, A_UNDERLINE | A_BOLD | A_REVERSE);

			deftheme(PspgTheme_cross_cursor, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgDefault, PspgDefault, 0);

			deftheme(PspgTheme_selection, PspgDefault, PspgDefault, A_DIM | A_REVERSE);
			deftheme(PspgTheme_cursor_selection, PspgDefault, PspgDefault, A_REVERSE | A_UNDERLINE);

			deftheme(PspgTheme_pattern, PspgDefault, PspgDefault, A_UNDERLINE | A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgDefault, PspgDefault, A_UNDERLINE);
			deftheme(PspgTheme_pattern_line, PspgDefault, PspgDefault, A_BOLD);
			deftheme(PspgTheme_pattern_line_border, PspgDefault, PspgDefault, 0);
			deftheme(PspgTheme_pattern_cursor, PspgDefault, PspgDefault, A_UNDERLINE | A_REVERSE | A_BOLD);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgDefault, PspgDefault, A_REVERSE);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgDefault, PspgDefault, A_REVERSE);
			break;

		case 17:
			/* Solar Dark theme */
			init_color(235, 27, 212, 259);
			init_color(234, 0, 169, 212);
			init_color(240, 345, 431, 459);
			init_color(244, 557, 616, 624);
			init_color(245, 576, 631, 631);
			init_color(254, 933, 910, 835);
			init_color(136, 710, 537, 0);
			init_color(137, 900, 627, 0);
			init_color(138, 800, 627, 0);
			init_color(33, 149, 545, 824);
			init_color(160, 863, 196, 184);

			init_pair(1, 245, 234);

			init_pair(2, 245, 235);
			init_pair(3, 244, 234);
			init_pair(4, 33, 234);
			init_pair(5, 235, 136);
			init_pair(6, 235, 136);
			init_pair(7, 33, 235);
			init_pair(8, 33, 235);
			init_pair(9, 61, 234);
			init_pair(10, 235, 136);
			init_pair(11, 235, 136);
			init_pair(12, -1, -1);
			init_pair(13, -1, -1);
			init_pair(14, 230, 160);
			init_pair(15, 254, 235);
			init_pair(16, 245, 235);
			init_pair(17, 245, 235);
			init_pair(18, -1, -1);
			init_pair(19, -1, -1);
			init_pair(20, 254, 136);
			init_pair(21, 244, 235);
			init_pair(22, 235, 137);
			init_pair(23, 235, 137);
			init_pair(24, 235, 138);
			init_pair(25, 235, 138);
			init_pair(28, 230, 160);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;
			break;

		case 18:
			/* Solar Light theme */
			init_color(234, 13, 98, 119);
			init_color(235, 18, 141, 172);
			init_color(240, 110, 146, 200);
			init_color(245, 576, 631, 631);
			init_color(244, 557, 616, 624);
			init_color(136, 710, 537, 0);
			init_color(160, 863, 196, 184);
			init_color(137, 880, 607, 0);
			init_color(138, 780, 607, 0);

			init_pair(1, 234, 245);

			init_pair(2, 235, 244);
			init_pair(3, 234, 245);
			init_pair(4, 17, 245);
			init_pair(5, 235, 136);
			init_pair(6, 235, 136);
			init_pair(7, 17, 244);
			init_pair(8, 17, 244);
			init_pair(9, 17, 245);
			init_pair(10, 235, 136);
			init_pair(11, 235, 136);
			init_pair(12, -1, -1);
			init_pair(13, -1, -1);
			init_pair(14, 255, 160);
			init_pair(15, 255, 244);
			init_pair(16, 240, 244);
			init_pair(17, 240, 244);
			init_pair(18, -1, -1);
			init_pair(19, -1, -1);
			init_pair(20, 255, 136);
			init_pair(21, 235, 244);
			init_pair(22, 235, 137);
			init_pair(23, 235, 137);
			init_pair(24, 235, 138);
			init_pair(25, 235, 138);
			init_pair(28, 255, 160);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;
			break;

		case 19:
			/* Gruvbox light theme */
			init_color_rgb_ff(200, 0xff, 0xff, 0xd7); /* background */
			init_color_rgb_ff(201, 0x26, 0x26, 0x26); /* foreground */
			init_color_rgb_ff(202, 0xaf, 0xaf, 0x87); /* modeline bg */
			init_color_rgb_ff(203, 0x4e, 0x4e, 0x4e); /* modeline fg */
			init_color_rgb_ff(204, 0xd7, 0xd6, 0xaf); /* table decor */
			init_color_rgb_ff(205, 0xeb, 0xdb, 0xb2); /* cursor bg */
			init_color_rgb_ff(206, 0xaf, 0xaf, 0xaf); /* footer */
			init_color_rgb_ff(207, 0xff, 0xff, 0xaf); /* lineno bg */

			init_color_rgb_ff(210, 0x87, 0x00, 0x00); /* keyword - red */
			init_color_rgb_ff(211, 0Xd7, 0x5f, 0x5f); /* bookmark - faded red */
			init_color_rgb_ff(212, 0x00, 0x5f, 0x87); /* mark - faded blue */
			init_color_rgb_ff(213, 0xfb, 0xf1, 0xc7); /* cursor bg */
			init_color_rgb_ff(214, 0xd0, 0xcf, 0xa0); /* mark line cursor */
			init_color_rgb_ff(215, 0xff, 0xff, 0xff); /* mark fg - white */

			init_pair(1, 204, 200);

			init_pair(2, 203, 202);
			init_pair(3, 201, 200);
			init_pair(4, 210, 200);
			init_pair(5, 210, 205);
			init_pair(6, 203, 205);
			init_pair(7, 203, 202);
			init_pair(8, 17, 200);
			init_pair(9, 206, 200);
			init_pair(10, 203, 205);
			init_pair(11, 206, 205);
			init_pair(13, 203, 202);
			init_pair(14, 215, 211);
			init_pair(15, 212, 204);
			init_pair(16, 201, 204);
			init_pair(17, 206, 204);
			init_pair(18, -1, -1);
			init_pair(19, -1, -1);
			init_pair(20, 212, 205);
			init_pair(21, 206, 207);
			init_pair(22, 203, 213);
			init_pair(23, 206, 213);
			init_pair(24, 201, 214);
			init_pair(25, 206, 214);
			init_pair(28, 215, 211);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;

			set_colour(34, 204, 201, false, A_REVERSE);
			set_colour(35, 201, 202, false, cursor_attr);
			break;

		case 20:
			/* Tao theme */
			init_color_rgb_ff(200, 0xf1, 0xf1, 0xf1); /* background */
			init_color_rgb_ff(201, 0x61, 0x61, 0x61); /* foreground */
			init_color_rgb_ff(202, 0xfc, 0xfc, 0xfc); /* modeline bg */
			init_color_rgb_ff(203, 0x17, 0x17, 0x17); /* modeline fg */
			init_color_rgb_ff(204, 0x9e, 0x9e, 0x9e); /* table decor */
			init_color_rgb_ff(205, 0x4e, 0x4e, 0x4e); /* cursor bg */
			init_color_rgb_ff(213, 0xf6, 0xf6, 0xf6); /* cursor fg */
			init_color_rgb_ff(206, 0x9e, 0x9e, 0x9e); /* footer */
			init_color_rgb_ff(207, 0xf6, 0xf6, 0xf6); /* lineno bg */

			init_color_rgb_ff(210, 0x00, 0x00, 0x00); /* keyword - red */
			init_color_rgb_ff(211, 0Xd7, 0x5f, 0x5f); /* bookmark - faded red */
			init_color_rgb_ff(212, 0xff, 0xff, 0xff); /* mark fg - white */
			init_color_rgb_ff(215, 0xc3, 0xc3, 0xc3); /* mark bg */
			init_color_rgb_ff(214, 0xda, 0xda, 0xda); /* marked line bg */
			init_color_rgb_ff(216, 0x25, 0x25, 0x25);

			init_color_rgb_ff(217, 0x40, 0x40, 0x40); /* vertical marked cursor bg */

			init_color(240, 40, 50, 200);

			init_pair(1, 204, 200);

			init_pair(2, 203, 202);
			init_pair(3, 201, 200);
			init_pair(4, 210, 200);
			init_pair(5, 213, 205);
			init_pair(6, 213, 205);
			init_pair(7, 203, 202);
			init_pair(8, 17, 200);
			init_pair(9, 206, 200);
			init_pair(10, 213, 205);
			init_pair(11, 204, 205);
			init_pair(13, 203, 202);
			init_pair(14, 212, 211);
			init_pair(15, 216, 214);
			init_pair(16, 201, 214);
			init_pair(17, 206, 214);
			init_pair(18, -1, -1);
			init_pair(19, -1, -1);
			init_pair(20, 212, 205);
			init_pair(21, 204, 207);
			init_pair(28, 212, 211);

			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(23, 204, COLOR_BLACK, false, 0);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */

			init_pair(24, 213, 217);
			init_pair(25, 204, 217);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;

			set_colour(34, 203, 206, false, 0);
			set_colour(35, 213, 203, false, cursor_attr);
			break;

		case 21:
			/* Flatwhite theme */
			init_color_rgb_ff(200, 0xf7, 0xf3, 0xee); /* background */
			init_color_rgb_ff(201, 0x60, 0x5A, 0x52); /* foreground */
			init_color_rgb_ff(202, 0xb9, 0xA9, 0x92); /* modeline bg */
			init_color_rgb_ff(203, 0x17, 0x17, 0x17); /* modeline fg */
			init_color_rgb_ff(204, 0xb9, 0xa9, 0x92); /* table decor */
			init_color_rgb_ff(205, 0x6a, 0x4d, 0xff); /* cursor bg */
			init_color_rgb_ff(213, 0xf6, 0xf6, 0xf6); /* cursor fg */
			init_color_rgb_ff(206, 0x9e, 0x9e, 0x9e); /* footer */
			init_color_rgb_ff(207, 0xf7, 0xf3, 0xee); /* lineno bg */

			init_color_rgb_ff(210, 0x09, 0x09, 0x08); /* labels */
			init_color_rgb_ff(211, 0Xf7, 0xe0, 0xc3); /* bookmark - bg */
			init_color_rgb_ff(212, 0x60, 0x5a, 0x52); /* bookmark - fg */
			init_color_rgb_ff(215, 0xe2, 0xe9, 0xc1); /* mark bg */
			init_color_rgb_ff(214, 0xe2, 0xe9, 0xc1); /* marked line bg */
			init_color_rgb_ff(216, 0x25, 0x25, 0x25);

			init_color_rgb_ff(217, 0x40, 0x40, 0x40); /* vertical marked cursor bg */
			init_color_rgb_ff(218, 0x00, 0x00, 0x00);
			init_color_rgb_ff(219, 0x5f, 0x45, 0xe5);
			init_color_rgb_ff(199, 0x87, 0x70, 0xff);

			init_color(240, 40, 50, 200);

			init_pair(1, 204, 200);
			init_pair(2, 203, 202);
			init_pair(3, 201, 200);
			init_pair(4, 210, 200);
			init_pair(5, 213, 205);
			init_pair(6, 213, 205);
			init_pair(7, 203, 202);
			init_pair(8, 17, 200);
			init_pair(9, 206, 200);
			init_pair(10, 213, 205);
			init_pair(11, 204, 205);
			init_pair(13, 203, 202);
			init_pair(14, 212, 211);
			init_pair(15, 216, 214);
			init_pair(16, 201, 214);
			init_pair(17, 204, 214);
			init_pair(18, -1, -1);
			init_pair(19, 201, 205);
			init_pair(20, 218, 205);
			init_pair(21, 204, 207);
			init_pair(28, 204, 211);

			set_colour(22, COLOR_WHITE, 199, true, cursor_attr);
			set_colour(23, 204, 199, false, 0);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */

			init_pair(24, 213, 219);
			init_pair(25, 204, 219);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;

			set_colour(34, 210, 202, false, 0);
			set_colour(35, 213, 203, false, cursor_attr);
			break;

		case 22:
			/* Relation pipes theme */
			deftheme(PspgTheme_background, PspgGreen, PspgBlack, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			deftheme(PspgTheme_data, PspgCyan, PspgBlack, 0);
			deftheme(PspgTheme_border, PspgGreen, PspgBlack, 0);
			deftheme(PspgTheme_label, PspgWhite, PspgBlack, 0);
			deftheme(PspgTheme_rownum, PspgWhite, PspgCyan, 0);
			deftheme(PspgTheme_recnum, PspgRed, PspgBlack, A_BOLD);
			deftheme(PspgTheme_footer, PspgYellow, PspgBlack, 0);

			deftheme(PspgTheme_cursor_data, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_border, PspgBrightGreen, PspgCyan, 0);
			deftheme(PspgTheme_cursor_label, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgCyan, 0);
			deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0);

			deftheme(PspgTheme_scrollbar_arrows, PspgLightGray, PspgBlack, A_REVERSE);
			deftheme(PspgTheme_scrollbar_background, PspgGray, PspgBlack, A_REVERSE);
			deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0);
			deftheme(PspgTheme_scrollbar_active_slider, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_title, PspgBrightRed, PspgBlack, 0);
			deftheme(PspgTheme_status_bar, PspgBrightRed, PspgBlack, 0);
			deftheme(PspgTheme_prompt_bar, PspgBrightRed, PspgBlack, 0);
			deftheme(PspgTheme_info_bar, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0);

			deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, 0);
			deftheme(PspgTheme_bookmark_border, PspgBrightGreen, PspgRed, A_BOLD);
			deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD);

			deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0);

			deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0);
			deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0);

			deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD);
			deftheme(PspgTheme_pattern_nohl, PspgGreen, PspgBlue, 0);
			deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0);
			deftheme(PspgTheme_pattern_line_border, PspgBrightGreen, PspgGreen, 0);
			deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0);

			deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0);
			deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0);
			break;

		case 23:
			/* PaperColour theme */
			init_color_rgb_ff(200, 0xee, 0xee, 0xee); /* background */
			init_color_rgb_ff(201, 0x09, 0x09, 0x09); /* foreground */
			init_color_rgb_ff(202, 0xd0, 0xd0, 0xd0); /* modeline bg */
			init_color_rgb_ff(203, 0x09, 0x09, 0x09); /* modeline fg */
			init_color_rgb_ff(204, 0x00, 0x87, 0xaf); /* table decor */
			init_color_rgb_ff(205, 0x00, 0x5f, 0x87); /* cursor bg */
			init_color_rgb_ff(213, 0xee, 0xee, 0xee); /* cursor fg */
			init_color_rgb_ff(206, 0x44, 0x44, 0x44); /* footer */
			init_color_rgb_ff(207, 0xee, 0xee, 0xee); /* lineno bg */

			init_color_rgb_ff(210, 0xd7, 0x00, 0x87); /* labels */
			init_color_rgb_ff(211, 0Xdf, 0x00, 0x00); /* bookmark - bg */
			init_color_rgb_ff(212, 0xff, 0xff, 0xf0); /* bookmark - fg */
			init_color_rgb_ff(215, 0xff, 0xff, 0x87); /* mark bg */
			init_color_rgb_ff(214, 0xff, 0xff, 0x87); /* marked line bg */
			init_color_rgb_ff(216, 0x25, 0x25, 0x25);

			init_color_rgb_ff(217, 0x00, 0x5f, 0x40); /* vertical marked cursor bg */
			init_color_rgb_ff(218, 0xff, 0xff, 0xff);
			init_color_rgb_ff(219, 0x5f, 0x45, 0xe5); 
			init_color_rgb_ff(199, 0x00, 0x5f, 0xd0); /* cross cursor */

			init_color(240, 40, 50, 200);

			init_pair(1, 204, 200);
			init_pair(2, 203, 202);
			init_pair(3, 201, 200);
			init_pair(4, 210, 200);
			init_pair(5, 213, 205);
			init_pair(6, 213, 205);
			init_pair(7, 203, 202);
			init_pair(8, 17, 200);
			init_pair(9, 206, 200);
			init_pair(10, 213, 205);
			init_pair(11, 204, 205);
			init_pair(13, 203, 202);
			init_pair(14, 212, 211);
			init_pair(15, 216, 214);
			init_pair(16, 201, 214);
			init_pair(17, 204, 214);
			init_pair(18, -1, -1);
			init_pair(19, 201, 205);
			set_colour(20, 218, 205, false, A_BOLD);
			init_pair(21, 204, 207);
			init_pair(28, 204, 211);

			set_colour(22, COLOR_WHITE, 199, true, cursor_attr);
			set_colour(23, 204, 199, false, 0);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */

			init_pair(24, 213, 217);
			init_pair(25, 204, 217);

			theme_attrs[4] = labels_attr;
			theme_attrs[5] = cursor_attr;
			theme_attrs[6] = cursor_attr;
			theme_attrs[10] = cursor_attr;

			set_colour(31, 200, 206, false, 0);						/* scrollbar background */
			set_colour(32, 200, 205, false, 0);							/* scrollbar slider */

			set_colour(34, 201, 202, false, 0);
			set_colour(35, 213, 203, false, cursor_attr);
			break;
	}
}

void
initialize_theme(int theme, int window_identifier, bool is_tabular_fmt, bool no_highlight_lines, Theme *t)
{
	memset(t, 0, sizeof(Theme));

	/* selected content and cursor in selected area */
	t->selection_attr = ncurses_theme_attr(PspgTheme_selection);
	t->selection_cursor_attr = ncurses_theme_attr(PspgTheme_cursor_selection) ;

	/* cross cursor - initial setting */
	t->cross_cursor_attr = ncurses_theme_attr(PspgTheme_cross_cursor);
	t->cross_cursor_line_attr = ncurses_theme_attr(PspgTheme_cross_cursor_border);

	t->pattern_vertical_cursor_attr = ncurses_theme_attr(PspgTheme_pattern_line_vertical_cursor);
	t->pattern_vertical_cursor_line_attr = ncurses_theme_attr(PspgTheme_pattern_line_vertical_cursor_border);

	t->line_attr = ncurses_theme_attr(PspgTheme_border);

	t->bookmark_data_attr = ncurses_theme_attr(PspgTheme_bookmark);
	t->bookmark_line_attr = ncurses_theme_attr(PspgTheme_bookmark_border);

	t->cursor_bookmark_attr = ncurses_theme_attr(PspgTheme_cursor_bookmark);
	t->cursor_line_attr = ncurses_theme_attr(PspgTheme_cursor_border);

	t->title_attr = ncurses_theme_attr(PspgTheme_title);
	t->status_bar_attr = ncurses_theme_attr(PspgTheme_status_bar);
	t->prompt_attr = ncurses_theme_attr(PspgTheme_prompt_bar);
	t->info_attr = ncurses_theme_attr(PspgTheme_info_bar);
	t->error_attr = ncurses_theme_attr(PspgTheme_error_bar);
	t->input_attr = ncurses_theme_attr(PspgTheme_input_bar);

	t->found_str_attr = no_highlight_lines ? ncurses_theme_attr(PspgTheme_pattern_nohl) : ncurses_theme_attr(PspgTheme_pattern);
	t->pattern_data_attr = ncurses_theme_attr(PspgTheme_pattern_line);
	t->pattern_line_attr = ncurses_theme_attr(PspgTheme_pattern_line_border);
	t->cursor_pattern_attr = ncurses_theme_attr(PspgTheme_pattern_cursor);

	t->expi_attr = ncurses_theme_attr(PspgTheme_recnum);
	t->cursor_expi_attr = ncurses_theme_attr(PspgTheme_cursor_recnum);

	switch (window_identifier)
	{
		case WINDOW_LUC:
		case WINDOW_FIX_ROWS:
			t->data_attr = ncurses_theme_attr(PspgTheme_label);
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_label);
			break;

		case WINDOW_FIX_COLS:
			t->data_attr = ncurses_theme_attr(PspgTheme_label);
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_label);
			break;

		case WINDOW_ROWS:
			t->data_attr = ncurses_theme_attr(PspgTheme_data);
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_data);
			break;

		case WINDOW_FOOTER:
			t->data_attr = is_tabular_fmt ? ncurses_theme_attr(PspgTheme_footer) : ncurses_theme_attr(PspgTheme_data);
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_footer);
			break;

		case WINDOW_ROWNUM:
		case WINDOW_ROWNUM_LUC:
			t->data_attr = ncurses_theme_attr(PspgTheme_rownum);
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_rownum);
			break;

		case WINDOW_VSCROLLBAR:
			t->scrollbar_arrow_attr = ncurses_theme_attr(PspgTheme_scrollbar_arrows);
			t->scrollbar_attr = ncurses_theme_attr(PspgTheme_scrollbar_background);
			t->scrollbar_slider_attr = ncurses_theme_attr(PspgTheme_scrollbar_slider);
			t->scrollbar_active_slider_attr = ncurses_theme_attr(PspgTheme_scrollbar_active_slider);

			switch (theme)
			{
				case 2:
					t->scrollbar_slider_symbol = ACS_DIAMOND;
					break;

				case 12:
					t->scrollbar_slider_symbol = ACS_BLOCK;
					break;

				case 0:
					t->scrollbar_slider_symbol = ACS_BLOCK;
					t->scrollbar_use_arrows = true;
					break;

				case 1: case 3: case 7: case 8:
				case 13: case 14: case 16: case 15:
					t->scrollbar_use_arrows = true;
					break;

				default:
					t->scrollbar_slider_symbol = 0;
					t->scrollbar_use_arrows = false;
			}
			break;
	}

	if (no_highlight_lines)
	{
		t->pattern_data_attr = t->data_attr;
		t->pattern_line_attr = t->line_attr;
	}
}
