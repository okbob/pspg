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
	PspgStyleDependency dep;
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


PspgThemeElement themedef[50];


void
deftheme(PspgThemeElements idx, PspgColor fg, PspgColor bg, int attr, PspgStyleDependency dep)
{
	memcpy(&themedef[idx].fg, &fg, sizeof(PspgColor));
	memcpy(&themedef[idx].bg, &bg, sizeof(PspgColor));
	themedef[idx].attr = attr;
	themedef[idx].dep = dep;
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

	PspgThemeElement *te = &themedef[idx];

	result = te->attr;

	if (te->dep != PSPG_INDEPENDENT)
	{
		if (current_state && current_state->opts)
		{
			bool bold_cursor = current_state->opts->bold_cursor;
			bool bold_labels = current_state->opts->bold_labels;

			if ((te->dep == PSPG_CURSOR_BOLD && bold_cursor) ||
				(te->dep == PSPG_LABEL_BOLD && bold_labels))
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

			if (fgcolorbright)
				result |= A_BOLD;
			else if (bgcolorbright)
				result |= A_BOLD | A_REVERSE;
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

//	init_pair(21, COLOR_WHITE, COLOR_BLACK);		/* Fx keys */

//	set_colour(26, COLOR_WHITE, COLOR_RED, true, 0);		/* error */
//	set_colour(27, COLOR_BLACK, COLOR_WHITE, false, 0);		/* input */

//	set_colour(30, COLOR_WHITE, COLOR_BLACK, false, A_REVERSE);	/* scrollbar arrows */
//	set_colour(31, COLOR_WHITE, COLOR_BLACK, true, 0);			/* scrollbar background */
//	set_colour(32, COLOR_BLACK, COLOR_BLUE, false, A_REVERSE);	/* scrollbar slider */
//	set_colour(33, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);	/* scrollbar active slider */
//
//	set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);			/* top bar colors */
//	set_colour(35, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);			/* top bar colors */

	switch (theme)
	{
		case 0:
			/* mc black theme */
			use_default_colors();
			init_pair(1, -1, -1);

			set_colour(2, COLOR_BLACK, COLOR_WHITE, false, 0);			/* top bar colors */
			set_colour(3, -1, -1, false, 0);							/* data alphanumeric */
			set_colour(4, -1, -1, true, labels_attr);						/* fix rows, columns */
			set_colour(5, COLOR_BLACK, COLOR_WHITE, false, cursor_attr);		/* active cursor over fixed cols */
			set_colour(6, COLOR_BLACK, COLOR_WHITE, false, cursor_attr);		/* active cursor */
			set_colour(7, COLOR_BLACK, COLOR_WHITE, false, 0);			/* title color */
			set_colour(8, COLOR_BLACK, COLOR_WHITE, false, 0);			/* expanded header */
			set_colour(9, -1, -1, false, 0);							/* footer */
			set_colour(10, COLOR_BLACK, COLOR_WHITE, false, cursor_attr);	/* footer cursor */
			set_colour(11, COLOR_BLACK, COLOR_WHITE, false, 0);			/* cursor over decoration */
			set_colour(12, COLOR_BLACK, COLOR_WHITE, false, 0);			/* bottom bar colors */
			set_colour(13, COLOR_BLACK, COLOR_WHITE, false, 0);			/* light bottom bar colors */
			set_colour(14, COLOR_BLACK, COLOR_WHITE, false, 0);			/* color of bookmark lines */
			set_colour(15, COLOR_WHITE, COLOR_BLACK, false, 0);			/* color of marked search pattern */
			set_colour(16, -1, -1, false, 0);							/* color of line with pattern */
			set_colour(17, -1, -1, false, 0);							/* color of line art with pattern */
			set_colour(18, -1, -1, false, 0);		/* color of marked search pattern in no-hl line mode */
			set_colour(19, -1, -1, false, 0);		/* color of marked search pattern in cursor */
			set_colour(20, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(21, -1, -1, false, 0);							/* rownum colors */
			set_colour(22, -1, -1, false, A_REVERSE);					/* cross cursor data */
			set_colour(23, -1, -1, false, A_REVERSE);					/* cross cursor line */
			set_colour(24, COLOR_BLACK, COLOR_WHITE, false, cursor_attr);/* vertical cursor pattern data */
			set_colour(25, COLOR_BLACK, COLOR_WHITE, false, cursor_attr);/* vertical cursor pattern line */
			set_colour(27, -1, -1, false, 0);		/* input */
			set_colour(28, COLOR_BLACK, COLOR_WHITE, false, 0);			/* color of bookmark lines for line art */
			set_colour(30, -1, -1, false, 0);							/* scrollbar arrows */
			set_colour(31, -1, -1, true, A_DIM);						/* scrollbar background */
			set_colour(32, -1, -1, false,  0);							/* scrollbar slider */
			set_colour(33, -1, -1, false, A_REVERSE);					/* scrollbar active slider */
			set_colour(34, COLOR_WHITE, COLOR_BLACK, false, A_DIM | A_BOLD | A_REVERSE);			/* selected data */
			set_colour(35, COLOR_BLACK, COLOR_WHITE, true, A_BOLD | A_REVERSE);	/* cursor in selected area */
			break;

		case 1:
			/* mc theme */
			/* 1 */ deftheme(PspgTheme_background, PspgLightGray, PspgBlue, 0, 0);

			/* set color_pair(1) to background */
			ncurses_theme_attr(PspgTheme_background);

			/* 3 */ deftheme(PspgTheme_data, PspgLightGray, PspgBlue, 0, 0);
			/* -1- */ deftheme(PspgTheme_border, PspgLightGray, PspgBlue, 0, 0);
			/* 4 */ deftheme(PspgTheme_label, PspgYellow, PspgBlue, 0, PSPG_LABEL_BOLD);
			/* 21 */ deftheme(PspgTheme_rownum, PspgWhite, PspgCyan, 0, 0);
			/* 8 */ deftheme(PspgTheme_recnum, PspgRed, PspgBlue, A_BOLD, 0);
			/* 9 */ deftheme(PspgTheme_footer, PspgCyan, PspgBlue, 0, 0);

			/* 6 */ deftheme(PspgTheme_cursor_data, PspgBlack, PspgCyan, 0, PSPG_CURSOR_BOLD);
			/* 11 */ deftheme(PspgTheme_cursor_border, PspgLightGray, PspgCyan, 0, 0);
			/* 5 */ deftheme(PspgTheme_cursor_label, PspgYellow, PspgCyan, 0, PSPG_CURSOR_BOLD);
			/* -10- */ deftheme(PspgTheme_cursor_rownum, PspgBlack, PspgCyan, 0, PSPG_CURSOR_BOLD);
			/* -6- */ deftheme(PspgTheme_cursor_recnum, PspgBlack, PspgCyan, 0, PSPG_CURSOR_BOLD);
			/* 10 */ deftheme(PspgTheme_cursor_footer, PspgBlack, PspgCyan, 0, PSPG_CURSOR_BOLD);

			/* 30 */ deftheme(PspgTheme_scrollbar_arrows, PspgLightGray, PspgBlue, 0, 0);
			/* 31 */ deftheme(PspgTheme_scrollbar_background, PspgCyan, PspgBlue, 0, 0);
			/* 32 */ deftheme(PspgTheme_scrollbar_slider, PspgBlue, PspgLightGray, 0, 0);
			/* 33 */ deftheme(PspgTheme_scrollbar_active_slider, PspgBlue, PspgWhite, 0, 0);

			/* 7 */ deftheme(PspgTheme_title, PspgBlack, PspgCyan, 0, 0);
			/* 2 */ deftheme(PspgTheme_status_bar, PspgBlack, PspgCyan, 0, 0);
			/* -2- */ deftheme(PspgTheme_prompt_bar, PspgBlack, PspgCyan, 0, 0);
			/* 13 */ deftheme(PspgTheme_info_bar, PspgBlack, PspgGreen, 0, 0);
			/* 26 */ deftheme(PspgTheme_error_bar, PspgWhite, PspgRed, 0, 0);
			/* 27 */ deftheme(PspgTheme_input_bar, PspgBlack, PspgLightGray, 0, 0);

			/* 14 */ deftheme(PspgTheme_bookmark, PspgWhite, PspgRed, 0, 0);
			/* 28 */ deftheme(PspgTheme_bookmark_border, PspgLightGray, PspgRed, A_BOLD, 0);
			/* 14 */ deftheme(PspgTheme_cursor_bookmark, PspgRed, PspgWhite, A_BOLD, 0);

			/* 22 */ deftheme(PspgTheme_cross_cursor, PspgBlack, PspgBrightCyan, 0, 0);
			/* 23 */ deftheme(PspgTheme_cross_cursor_border, PspgLightGray, PspgBrightCyan, 0, 0);

			/* 34 */ deftheme(PspgTheme_selection, PspgBlack, PspgBrightCyan, 0, 0);
			/* 35 */ deftheme(PspgTheme_cursor_selection, PspgBlack, PspgWhite, 0, 0);

			/* 15 */ deftheme(PspgTheme_pattern, PspgYellow, PspgGreen, A_BOLD, 0);
			/* 18 */ deftheme(PspgTheme_pattern_nohl, PspgGreen, PspgBlue, 0, 0);
			/* 16 */ deftheme(PspgTheme_pattern_line, PspgBlack, PspgGreen, 0, 0);
			/* 17 */ deftheme(PspgTheme_pattern_line_border, PspgLightGray, PspgGreen, 0, 0);
			/* 20 */ deftheme(PspgTheme_pattern_cursor, PspgWhite, PspgBlack, 0, 0);

			/* 24 */ deftheme(PspgTheme_pattern_line_vertical_cursor, PspgBlack, PspgBrightGreen, 0, PSPG_CURSOR_BOLD);
			/* 25 */ deftheme(PspgTheme_pattern_line_vertical_cursor_border, PspgLightGray, PspgBrightGreen, 0, 0);
			break;

		case 2:
			/* FoxPro theme */
			init_pair(1, COLOR_WHITE, COLOR_CYAN);

			set_colour(2, COLOR_BLACK, COLOR_WHITE, false,0);
			set_colour(3, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(4, COLOR_WHITE, COLOR_CYAN, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(7, COLOR_YELLOW, COLOR_WHITE, true,0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, true,0);
			set_colour(9, COLOR_BLUE, COLOR_CYAN, false,0);
			set_colour(10, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_BLUE, false,0);
			set_colour(12, COLOR_WHITE, COLOR_BLUE, true,0);
			set_colour(13, COLOR_WHITE, COLOR_BLUE, true,0);
			set_colour(14, COLOR_WHITE, COLOR_MAGENTA, true,0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true,0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false,0);
			set_colour(17, COLOR_WHITE, COLOR_GREEN, false,0);
			set_colour(18, COLOR_YELLOW, COLOR_GREEN, true,0);
			set_colour(19, COLOR_YELLOW, COLOR_BLUE, true,0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, false,0);
			set_colour(21, COLOR_WHITE, COLOR_CYAN, false, 0);
			set_colour(22, COLOR_BLUE, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_BLUE, COLOR_WHITE, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(26, COLOR_YELLOW, COLOR_RED, true,0);
			set_colour(28, COLOR_WHITE, COLOR_MAGENTA, true,0);
			set_colour(30, COLOR_YELLOW, COLOR_WHITE, true, 0);
			set_colour(31, COLOR_WHITE, COLOR_WHITE, false, 0);
			set_colour(32, COLOR_YELLOW, COLOR_WHITE, true, 0);
			set_colour(33, COLOR_YELLOW, COLOR_WHITE, true, A_BOLD);
			set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);
			set_colour(35, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE | cursor_attr );
			break;

		case 3:
			/* PD Menu theme */
			init_pair(1, COLOR_BLACK, COLOR_CYAN);

			set_colour(2, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(3, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(4, COLOR_WHITE, COLOR_CYAN, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(6, COLOR_CYAN, COLOR_BLACK, true, cursor_attr);
			set_colour(7, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(9, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(10, COLOR_CYAN, COLOR_BLACK, true, cursor_attr);
			set_colour(11, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(12, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_WHITE, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_WHITE, COLOR_GREEN, true, 0);
			set_colour(19, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(21, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_WHITE, COLOR_CYAN, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE);
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);
			set_colour(31, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(32, COLOR_BLACK, COLOR_BLUE, false, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);
			set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);
			set_colour(35, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE | cursor_attr );
			break;

		case 4:
			/* White theme */
			init_pair(1, COLOR_BLACK, COLOR_WHITE);

			set_colour(2, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(3, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(4, COLOR_BLACK, COLOR_WHITE, false, A_BOLD);
			set_colour(5, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(7, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(19, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(23, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			break;

		case 5:
			/* Mutt theme */
			use_default_colors();
			init_pair(1, -1, -1);

			set_colour(2, COLOR_GREEN, COLOR_BLUE, true, 0);
			set_colour(3, -1, -1, false, 0);
			set_colour(4, COLOR_CYAN, -1, true, labels_attr);
			set_colour(5, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(6, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(7, COLOR_GREEN, COLOR_BLUE, true, 0);
			set_colour(8, COLOR_BLACK, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(10, COLOR_BLACK, COLOR_CYAN, false, cursor_attr);
			set_colour(11, -1, COLOR_CYAN, false, 0);
			set_colour(12, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(13, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, -1, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(19, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, false, cursor_attr);
			set_colour(23, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			break;

		case 6:
			/* PC Fand theme */
			init_pair(1, COLOR_WHITE, COLOR_BLACK);

			set_colour(2, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(4, COLOR_CYAN, COLOR_BLACK, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(7, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_WHITE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(19, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(20, COLOR_CYAN, COLOR_BLUE, true, cursor_attr);
			set_colour(22, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(23, COLOR_WHITE, COLOR_CYAN, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			break;

		case 7:
			/* Green theme */
			init_pair(1, COLOR_GREEN, COLOR_BLACK);

			set_colour(2, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(3, COLOR_GREEN, COLOR_BLACK, false, 0);
			set_colour(4, COLOR_GREEN, COLOR_BLACK, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_GREEN, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_GREEN, true, cursor_attr);
			set_colour(7, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_GREEN, true, cursor_attr);
			set_colour(11, COLOR_GREEN, COLOR_GREEN, true, 0);
			set_colour(12, COLOR_WHITE, COLOR_GREEN, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_GREEN, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(17, COLOR_GREEN, COLOR_CYAN, false, 0);
			set_colour(18, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(19, COLOR_CYAN, COLOR_GREEN, false, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(21, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(22, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE |  cursor_attr);
			set_colour(23, COLOR_GREEN, COLOR_GREEN, true, A_REVERSE);
			set_colour(24, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_CYAN, COLOR_GREEN, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_GREEN, COLOR_BLACK, true, 0);
			set_colour(31, COLOR_GREEN, COLOR_BLACK, false, 0);
			set_colour(32, COLOR_GREEN, COLOR_BLACK, false, A_REVERSE);
			set_colour(33, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE);
			set_colour(34, COLOR_GREEN, COLOR_BLACK, true, A_DIM | A_REVERSE);
			set_colour(35, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr );
			break;

		case 8:
			/* Blue theme */
			init_pair(1, COLOR_CYAN, COLOR_BLUE);

			set_colour(2, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(4, COLOR_WHITE, COLOR_BLUE, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(7, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(11, COLOR_CYAN, COLOR_CYAN, true, 0);
			set_colour(12, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_CYAN, COLOR_GREEN, true, 0);
			set_colour(18, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(19, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(21, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(22, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_CYAN, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_CYAN, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);	/* scrollbar arrows */
			set_colour(31, COLOR_WHITE, COLOR_WHITE, false, 0);			/* scrollbar background */
			set_colour(32, COLOR_WHITE, COLOR_WHITE, true, A_REVERSE);	/* scrollbar slider */
			set_colour(33, COLOR_BLACK, COLOR_BLACK, false, A_REVERSE);	/* scrollbar active slider */
			break;

		case 9:
			/* Word Perfect theme */
			init_pair(1, COLOR_WHITE, COLOR_BLUE);

			set_colour(2, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(4, COLOR_CYAN, COLOR_BLUE, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(7, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_GREEN, COLOR_BLUE, true, 0);
			set_colour(10, COLOR_WHITE, COLOR_CYAN, true, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_CYAN, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_WHITE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_BLUE, true, 0);
			set_colour(19, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(21, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(22, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_WHITE, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(31, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(32, COLOR_WHITE, COLOR_WHITE, false, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_WHITE, true, A_REVERSE);
			break;

		case 10:
			/* low contrast theme */
			init_pair(1, COLOR_BLUE, COLOR_CYAN);

			set_colour(2, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(3, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(4, COLOR_WHITE, COLOR_CYAN, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(7, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(11, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_BLUE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(19, COLOR_YELLOW, COLOR_BLUE, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(21, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_BLUE, true, A_REVERSE);
			set_colour(27, COLOR_BLACK, COLOR_CYAN, false, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(31, COLOR_BLACK, COLOR_CYAN, true, 0);
			set_colour(32, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);
			set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_DIM | A_REVERSE);
			set_colour(35, COLOR_GREEN, COLOR_BLACK, false, A_REVERSE | cursor_attr );
			break;

		case 11:
			/* Dark cyan theme */
			init_pair(1, COLOR_CYAN, COLOR_BLACK);

			set_colour(2, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(3, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(4, COLOR_CYAN, COLOR_BLACK, true, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_MAGENTA, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_MAGENTA, true, cursor_attr);
			set_colour(7, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(9, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_MAGENTA, true, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_MAGENTA, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_MAGENTA, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_MAGENTA, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_CYAN, COLOR_GREEN, true, 0);
			set_colour(18, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(19, COLOR_CYAN, COLOR_MAGENTA, false, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(22, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_CYAN, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(31, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(32, COLOR_WHITE, COLOR_WHITE, false, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_WHITE, true, A_REVERSE);
			set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);
			set_colour(35, COLOR_CYAN, COLOR_BLUE, true, cursor_attr );
			break;

		case 12:
			/* Paradox like theme */
			init_pair(1, COLOR_BLUE, COLOR_CYAN);

			set_colour(2, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(4, COLOR_BLUE, COLOR_CYAN, false, labels_attr);
			set_colour(5, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(7, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_BLUE, true, cursor_attr);
			set_colour(11, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_BLUE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(19, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(20, COLOR_WHITE, COLOR_MAGENTA, true, cursor_attr);
			set_colour(21, COLOR_CYAN, COLOR_CYAN, true, 0);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_BLUE, true, A_REVERSE);
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(31, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(32, COLOR_BLUE, COLOR_CYAN, false, 0);
			set_colour(33, COLOR_CYAN, COLOR_BLUE, true, A_REVERSE);
			set_colour(34, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE);
			set_colour(35, COLOR_CYAN, COLOR_BLACK, true, A_DIM | A_REVERSE | cursor_attr );
			break;

		case 13:
			/* DBase retro theme */
			init_pair(1, COLOR_WHITE, COLOR_BLUE);

			set_colour(2, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(4, COLOR_WHITE, COLOR_BLUE, true, labels_attr);
			set_colour(5, COLOR_YELLOW, COLOR_CYAN, true, cursor_attr);
			set_colour(6, COLOR_YELLOW, COLOR_CYAN, true, cursor_attr);
			set_colour(7, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(10, COLOR_BLACK, COLOR_CYAN, false, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_CYAN, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_WHITE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(19, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(22, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_WHITE, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(27, COLOR_BLACK, COLOR_CYAN, false, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(31, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(32, COLOR_WHITE, COLOR_BLUE, false, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);
			set_colour(34, COLOR_WHITE, COLOR_MAGENTA, true, 0);
			set_colour(35, COLOR_MAGENTA, COLOR_BLACK, true, A_REVERSE | cursor_attr );
			break;

		case 14:
			/* DBase retro magenta */
			init_pair(1, COLOR_WHITE, COLOR_BLUE);

			set_colour(2, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(3, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(4, COLOR_MAGENTA, COLOR_BLUE, true, labels_attr);
			set_colour(5, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(6, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(7, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(10, COLOR_BLACK, COLOR_CYAN, false, cursor_attr);
			set_colour(11, COLOR_WHITE, COLOR_CYAN, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_WHITE, COLOR_GREEN, false, 0);
			set_colour(18, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(19, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(21, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(22, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_WHITE, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLUE, true, 0);		/* input */
			set_colour(28, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLUE, true, 0);
			set_colour(31, COLOR_CYAN, COLOR_BLUE, false, 0);
			set_colour(32, COLOR_WHITE, COLOR_BLUE, false, A_REVERSE);
			set_colour(33, COLOR_MAGENTA, COLOR_BLACK, true, A_REVERSE);
			break;

		case 15:
			/* Red theme */
			init_pair(1, COLOR_BLACK, COLOR_WHITE);

			set_colour(2, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(3, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(4, COLOR_RED, COLOR_WHITE, false, A_DIM);
			set_colour(5, COLOR_WHITE, COLOR_RED, true, cursor_attr);
			set_colour(6, COLOR_WHITE, COLOR_RED, true, cursor_attr);
			set_colour(7, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(8, COLOR_WHITE, COLOR_BLUE, false, 0);
			set_colour(9, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(10, COLOR_WHITE, COLOR_RED, true, cursor_attr);
			set_colour(11, COLOR_BLACK, COLOR_RED, false, 0);
			set_colour(12, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(13, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(14, COLOR_YELLOW, COLOR_RED, true, A_REVERSE | A_BOLD);
			set_colour(15, COLOR_YELLOW, COLOR_BLACK, true, 0);
			set_colour(16, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(17, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(18, COLOR_CYAN, COLOR_BLUE, true, 0);
			set_colour(19, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, false, 0);
			set_colour(22, COLOR_WHITE, COLOR_BLACK, true, cursor_attr);
			set_colour(23, COLOR_RED, COLOR_BLACK, false, 0);
			set_colour(24, COLOR_RED, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_RED, COLOR_BLACK, true, A_REVERSE);
			set_colour(27, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);		/* input */
			set_colour(28, COLOR_YELLOW, COLOR_RED, true, A_REVERSE | A_BOLD);
			set_colour(30, COLOR_BLACK, COLOR_WHITE, false, 0);
			set_colour(31, COLOR_BLACK, COLOR_WHITE, true, 0);
			set_colour(32, COLOR_BLACK, COLOR_BLACK, true, A_REVERSE | A_DIM);
			set_colour(33, COLOR_RED, COLOR_BLACK, false, A_REVERSE);
			set_colour(34, COLOR_YELLOW, COLOR_BLACK, true, A_REVERSE);
			set_colour(35, COLOR_RED, COLOR_BLACK, true, A_REVERSE | cursor_attr );
			break;

		case 16:
			/* Simple theme */
			use_default_colors();
			init_pair(1, -1, -1);

			set_colour(2, -1, -1, false, 0);
			set_colour(3, -1, -1, false, 0);
			set_colour(4, -1, -1, true, A_ITALIC | labels_attr);
			set_colour(5, -1, -1, true, A_REVERSE | cursor_attr);
			set_colour(6, -1, -1, true, A_REVERSE | cursor_attr);
			set_colour(7, -1, -1, false, 0);
			set_colour(8, -1, -1, false, 0);
			set_colour(9, -1, -1, false, 0);
			set_colour(10, -1, -1, true, A_REVERSE | cursor_attr);
			set_colour(11, -1, -1, false, A_REVERSE);
			set_colour(12, -1, -1, false, 0);
			set_colour(13, -1, -1, true, 0);
			set_colour(14, -1, -1, false, A_UNDERLINE);
			set_colour(15, -1, -1, false, A_UNDERLINE);
			set_colour(16, -1, -1, false, 0);
			set_colour(17, -1, -1, false, 0);
			set_colour(18, -1, -1, false, A_UNDERLINE);
			set_colour(19, -1, -1, false, A_UNDERLINE | A_REVERSE | A_BOLD);
			set_colour(20, -1, -1, false, A_UNDERLINE | A_REVERSE | A_BOLD);
			set_colour(21, -1, -1, false, 0);
			set_colour(22, -1, -1, true, cursor_attr);
			set_colour(23, -1, -1, false, 0);
			set_colour(24, -1, -1, true, A_REVERSE | cursor_attr);
			set_colour(25, -1, -1, true, A_REVERSE);
			set_colour(27, -1, -1, false, 0);		/* input */
			set_colour(28, -1, -1, false, A_UNDERLINE);
			set_colour(30, -1, -1, true, 0);
			set_colour(31, -1, -1, false, A_DIM);
			set_colour(32, -1, -1, false, A_REVERSE);
			set_colour(33, -1, -1, true, A_REVERSE);
			set_colour(34, -1, -1, false, A_DIM | A_REVERSE);
			set_colour(35, -1, -1, false, A_REVERSE | cursor_attr | A_UNDERLINE);
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
			init_pair(1, COLOR_GREEN, COLOR_BLACK);

			set_colour(2, COLOR_RED, COLOR_BLACK, true, 0);
			set_colour(3, COLOR_CYAN, COLOR_BLACK, false, 0);
			set_colour(4, COLOR_WHITE, COLOR_BLACK, true, labels_attr);
			set_colour(5, COLOR_BLACK, COLOR_CYAN, false, cursor_attr);
			set_colour(6, COLOR_BLACK, COLOR_CYAN, false, 0);
			set_colour(7, COLOR_RED, COLOR_BLACK, true, 0);
			set_colour(8, COLOR_RED, COLOR_BLACK, false, 0);
			set_colour(9, COLOR_YELLOW, COLOR_BLACK, false, 0);
			set_colour(10, COLOR_BLACK, COLOR_CYAN, false, cursor_attr);
			set_colour(11, COLOR_GREEN, COLOR_CYAN, true, 0);
			set_colour(12, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(13, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(14, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(15, COLOR_YELLOW, COLOR_GREEN, true, 0);
			set_colour(16, COLOR_BLACK, COLOR_GREEN, false, 0);
			set_colour(17, COLOR_GREEN, COLOR_GREEN, true, 0);
			set_colour(18, COLOR_GREEN, COLOR_BLUE, false, 0);
			set_colour(19, COLOR_YELLOW, COLOR_CYAN, true, 0);
			set_colour(20, COLOR_WHITE, COLOR_BLACK, true, 0);
			set_colour(21, COLOR_WHITE, COLOR_CYAN, true, 0);
			set_colour(22, COLOR_CYAN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(23, COLOR_CYAN, COLOR_WHITE, true, A_REVERSE);
			set_colour(24, COLOR_GREEN, COLOR_BLACK, true, A_REVERSE | cursor_attr);
			set_colour(25, COLOR_GREEN, COLOR_WHITE, true, A_REVERSE);
			set_colour(26, COLOR_WHITE, COLOR_RED, true, 0);
			set_colour(28, COLOR_GREEN, COLOR_RED, true, 0);
			set_colour(30, COLOR_WHITE, COLOR_BLACK, false, A_REVERSE);
			set_colour(31, COLOR_BLACK, COLOR_BLACK, true, 0);
			set_colour(32, COLOR_WHITE, COLOR_BLUE, false, A_REVERSE);
			set_colour(33, COLOR_WHITE, COLOR_BLACK, true, A_REVERSE);
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
			t->data_attr = ncurses_theme_attr(PspgTheme_rownum);;
			t->cursor_data_attr = ncurses_theme_attr(PspgTheme_cursor_rownum);;
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
