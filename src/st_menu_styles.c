#include "st_menu.h"

#include <string.h>

static bool direct_color = false;
static int	current_cpn = 0;
static unsigned int	rgb_color_cache[10];
static int	custom_color_start;

/* 0..255 rgb based colors */
static void
init_color_rgb_ff(short color, short r, short g, short b)
{
	if (direct_color || !can_change_color())
	{
		rgb_color_cache[color] = (r << 16) + (g << 8) + b;
	}
	else
		init_color(custom_color_start + color,
				   (r / 255.0) * 1000.0,
				   (g / 255.0) * 1000.0,
				   (b / 255.0) * 1000.0);
}

/*
 * Set ligh colour
 */
static attr_t
slc(short id, short foreground, short background)
{
	if (COLORS == 8 || foreground == -1)
	{
		init_pair(id, foreground, background);
		return A_BOLD;
	}
	else if (foreground < 8)
	{
		init_pair(id, foreground + 8, background);
		return 0;
	}
	else
	{
		init_pair(id, foreground, background);
		return 0;
	}
}

static unsigned int
get_rgb(short c, bool light)
{
	if (light)
	{
		switch (c)
		{
			case COLOR_BLACK:
				return 0x555555;
			case COLOR_RED:
				return 0xff5555;
			case COLOR_GREEN:
				return 0x55ff55;
			case COLOR_YELLOW:
				return 0xffff55;
			case COLOR_BLUE:
				return 0x5555ff;
			case COLOR_MAGENTA:
				return 0xff55ff;
			case COLOR_CYAN:
				return 0x55ffff;
			case COLOR_WHITE:
				return 0xffffff;
			default:
				return -1;
		}
	}
	else
	{
		switch (c)
		{
			case COLOR_BLACK:
				return 0x000000;
			case COLOR_RED:
				return 0xaa0000;
			case COLOR_GREEN:
				return 0x00aa00;
			case COLOR_YELLOW:
				return 0xaa5500;
			case COLOR_BLUE:
				return 0x0000aa;
			case COLOR_MAGENTA:
				return 0xaa00aa;
			case COLOR_CYAN:
				return 0x00aaaa;
			case COLOR_WHITE:
				return 0xaaaaaa;
			default:
				return -1;
		}
	}

	/* fallback */
	return -1;
}

/*
 * This is taken from tmux
 */
static int
colour_dist_sq(int R, int G, int B, int r, int g, int b)
{
	return ((R - r) * (R - r) + (G - g) * (G - g) + (B - b) * (B - b));
}

static int
colour_to_6cube(int v)
{
	if (v < 48)
		return (0);

	if (v < 114)
		return (1);

	return ((v - 35) / 40);
}

/*
 * Convert an RGB triplet to the xterm(1) 256 colour palette.
 *
 * xterm provides a 6x6x6 colour cube (16 - 231) and 24 greys (232 - 255). We
 * map our RGB colour to the closest in the cube, also work out the closest
 * grey, and use the nearest of the two.
 *
 * Note that the xterm has much lower resolution for darker colours (they are
 * not evenly spread out), so our 6 levels are not evenly spread: 0x0, 0x5f
 * (95), 0x87 (135), 0xaf (175), 0xd7 (215) and 0xff (255). Greys are more
 * evenly spread (8, 18, 28 ... 238).
 */
static int
colour_find_rgb(unsigned int rgb)
{
	static const int	q2c[6] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };
	unsigned char		r, g, b;
	int			qr, qg, qb, cr, cg, cb, d, idx;
	int			grey_avg, grey_idx, grey;

	/* try to find most near color in 256 color palete */
	r = ((rgb >> 16) & 0xff);
	g = ((rgb >> 8) & 0xff);
	b = ((rgb) & 0xff);

	/* Map RGB to 6x6x6 cube. */
	qr = colour_to_6cube(r); cr = q2c[qr];
	qg = colour_to_6cube(g); cg = q2c[qg];
	qb = colour_to_6cube(b); cb = q2c[qb];

	/* If we have hit the colour exactly, return early. */
	if (cr == r && cg == g && cb == b)
		return 16 + (36 * qr) + (6 * qg) + qb;

	/* Work out the closest grey (average of RGB). */
	grey_avg = (r + g + b) / 3;
	if (grey_avg > 238)
		grey_idx = 23;
	else
		grey_idx = (grey_avg - 3) / 10;
	grey = 8 + (10 * grey_idx);

	/* Is grey or 6x6x6 colour closest? */
	d = colour_dist_sq(cr, cg, cb, r, g, b);
	if (colour_dist_sq(grey, grey, grey, r, g, b) < d)
		idx = 232 + grey_idx;
	else
		idx = 16 + (36 * qr) + (6 * qg) + qb;

	return idx;
}

static void
set_rgb_color_pair(int *cp, attr_t *attr, int fg, int bg, const char *csrc, attr_t _attr)
{
	unsigned int		fgcolor, bgcolor;

	if (direct_color)
	{
#ifdef  NCURSES_EXT_COLORS

		fgcolor = csrc[0] == 'b' ? get_rgb(fg, false) : rgb_color_cache[fg];
		bgcolor = csrc[1] == 'b' ? get_rgb(bg, false) : rgb_color_cache[bg];

		init_extended_pair(current_cpn, fgcolor, bgcolor);

#else

		/* fallback */
		init_pair(current_cpn, -1, -1);

#endif

	}
	else
	{
		if (can_change_color())
		{
			/* "b" like basic, "c" custom */
			fgcolor = csrc[0] == 'b' ? fg : custom_color_start + fg;
			bgcolor = csrc[1] == 'b' ? bg : custom_color_start + bg;
		}
		else
		{
			fgcolor = csrc[0] == 'b' ? fg : colour_find_rgb(rgb_color_cache[fg]);
			bgcolor = csrc[1] == 'b' ? bg : colour_find_rgb(rgb_color_cache[bg]);
		}

		init_pair(current_cpn, fgcolor, bgcolor);
	}

	*cp = current_cpn++;
	*attr = _attr;
}

static void
set_color_pair(int *cp, attr_t *attr, short fg, short bg, bool light)
{
	if (direct_color)
	{
#ifdef  NCURSES_EXT_COLORS

		int fgcolor, bgcolor;

		fgcolor = get_rgb(fg, light);
		bgcolor = get_rgb(bg, false);

		init_extended_pair(current_cpn, fgcolor, bgcolor);

#else

		/* fallback */
		init_pair(current_cpn, -1, -1);

#endif

		*attr = 0;
	}
	else
	{
		if (light)
			*attr = slc(current_cpn, fg, bg);
		else
		{
			init_pair(current_cpn, fg, bg);
			*attr = 0;
		}
	}

	*cp = current_cpn++;
}


#define islc(f,b)		(slc(start_from_cpn++, f, b))

void
st_menu_set_direct_color(bool _direct_color)
{
	direct_color = _direct_color;
}

/*
 * Prepared styles - config settings. start_from_cpn is first color pair 
 * number that can be used by st_menu library. For ST_MENU_STYLE_ONECOLOR
 * style it is number of already existing color pair.
 */
int
st_menu_load_style(ST_MENU_CONFIG *config, int style, int start_from_cpn,
				   bool force8bit, bool force_ascii_art)
{
	return st_menu_load_style_rgb(config,
								  style,
								  start_from_cpn,
								  NULL,
								  force8bit,
								  force_ascii_art);
}

int
st_menu_load_style_rgb(ST_MENU_CONFIG *config, int style, int start_from_cpn, int *start_from_rgb,
					   bool force8bit, bool force_ascii_art)
{
	memset(config, 0, sizeof(ST_MENU_CONFIG));

	config->submenu_tag = '>';
	config->mark_tag = '*';
	config->switch_tag_n1 = '?';
	config->switch_tag_0 = '-';
	config->switch_tag_1 = 'x';
	config->scroll_up_tag = '^';
	config->scroll_down_tag = 'v';
	config->draw_box = true;
	config->extern_accel_text_space = 2;

	config->force8bit = force8bit;
	config->force_ascii_art = force_ascii_art;

	config->submenu_offset_y = 0;
	config->submenu_offset_x = 0;

	current_cpn = start_from_cpn;

#if defined  HAVE_NCURSESW

	if (!config->force8bit && !config->force_ascii_art)
	{
		config->mark_tag = L'\x2714';
/*
		config->switch_tag_n1 = L'\x2680';
		config->switch_tag_0 = L'\x2610';
		config->switch_tag_1 = L'\x2611';
*/

		config->switch_tag_n1 = '.';
		config->switch_tag_0 = ' ';
		config->switch_tag_1 = L'\x2714';

		config->scroll_up_tag = L'\x25b2';
		config->scroll_down_tag = L'\x25bc';

	}

#endif

	config->funckey_bar_style = false;

	switch (style)
	{
		case ST_MENU_STYLE_MCB:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, -1, -1, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);

			config->disabled_cpn = config->menu_background_cpn;
			config->disabled_attr = A_BOLD;

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			config->funckey_bar_style = true;

			break;

		case ST_MENU_STYLE_MC:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_WHITE, COLOR_CYAN, true);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_YELLOW, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_BLACK, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 2;

			config->funckey_bar_style = true;

			break;

		case ST_MENU_STYLE_VISION:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_WHITE, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_BLACK, COLOR_GREEN, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_RED, COLOR_GREEN, false);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = true;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			if (!config->force8bit)
				config->submenu_tag = L'\x25BA';

			config->submenu_offset_y = 0;
			config->submenu_offset_x = -15;

			break;

		case ST_MENU_STYLE_DOS:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_WHITE, COLOR_WHITE, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_FAND_1:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_CYAN, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

			if (!config->force8bit)
				config->submenu_tag = L'\x00BB';

			break;

		case ST_MENU_STYLE_FAND_2:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_CYAN, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

			if (!config->force8bit)
				config->submenu_tag = L'\x00BB';

			break;

		case ST_MENU_STYLE_FOXPRO:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_WHITE, COLOR_WHITE, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_CYAN, true);

			/* This is different, from original, but cyan text is not readable */
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_CYAN, COLOR_WHITE, true);
			config->disabled_attr |= A_DIM;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_PERFECT:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_WHITE, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_RED, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_RED, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			if (!config->force8bit)
				config->submenu_tag = L'\x2BC8';

			break;

		case ST_MENU_STYLE_NOCOLOR:
			config->menu_background_cpn = 0;
			config->menu_background_attr = 0;

			config->menu_unfocused_cpn = 0;
			config->menu_unfocused_attr = 0;

			config->menu_shadow_cpn = current_cpn;
			config->menu_shadow_attr = A_REVERSE;

			config->accelerator_cpn = 0;
			config->accelerator_attr = A_UNDERLINE;

			config->cursor_cpn = 0;
			config->cursor_attr = A_REVERSE;

			config->cursor_accel_cpn = 0;
			config->cursor_accel_attr = A_UNDERLINE | A_REVERSE;

			config->disabled_cpn = 0;
			config->disabled_attr = A_DIM;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_ONECOLOR:
			config->menu_background_cpn = current_cpn;
			config->menu_background_attr = 0;

			config->menu_unfocused_cpn = current_cpn;
			config->menu_unfocused_attr = 0;

			config->menu_shadow_cpn = current_cpn;
			config->menu_shadow_attr = A_REVERSE;

			config->accelerator_cpn = current_cpn;
			config->accelerator_attr = A_UNDERLINE;

			config->cursor_cpn = current_cpn;
			config->cursor_attr = A_REVERSE;

			config->cursor_accel_cpn = current_cpn;
			config->cursor_accel_attr = A_UNDERLINE | A_REVERSE;

			config->disabled_cpn = current_cpn;
			config->disabled_attr = A_DIM;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_TURBO:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_WHITE, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_PDMENU:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_WHITE, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_CYAN, COLOR_BLACK, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_OLD_TURBO:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_YELLOW, COLOR_BLUE, true);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_CYAN, COLOR_BLUE, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_YELLOW, COLOR_MAGENTA, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_MAGENTA, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_WHITE, COLOR_BLUE, true);
			config->disabled_attr |= A_DIM;

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_FREE_DOS:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_WHITE, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_RED, COLOR_CYAN, false);

			/* This is different, from original, but cyan text is not readable */
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 1;

			break;

		case ST_MENU_STYLE_FREE_DOS_P:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_RED, COLOR_CYAN, false);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_BLACK, COLOR_WHITE, false);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_RED, COLOR_WHITE, false);

			/* This is different, from original, but cyan text is not readable */
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 1;

			break;

		case ST_MENU_STYLE_MC46:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_YELLOW, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_BLACK, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			config->funckey_bar_style = true;

			break;

		case ST_MENU_STYLE_DBASE:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_WHITE, COLOR_BLACK, true);
			config->accelerator_attr |= A_UNDERLINE;

			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_YELLOW, COLOR_CYAN, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_CYAN, true);
			config->cursor_accel_attr |= A_UNDERLINE;

			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_WHITE, COLOR_BLACK, true);
			config->disabled_attr |= A_DIM;

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_MENUWORKS:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_WHITE, COLOR_RED, true);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_WHITE, COLOR_BLACK, true);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_BLUE, COLOR_BLACK, false);
			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_YELLOW, COLOR_RED, true);
			config->accelerator_attr |= A_UNDERLINE;

			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_YELLOW, COLOR_BLACK, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_YELLOW, COLOR_BLACK, true);
			config->cursor_accel_attr |= A_UNDERLINE;

			set_color_pair(&config->disabled_cpn,  &config->disabled_attr, COLOR_WHITE, COLOR_RED, true);
			config->disabled_attr |= A_DIM;

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_TAO:
			if (start_from_rgb)
			{
				custom_color_start = *start_from_rgb;
				*start_from_rgb += 5;

				init_color_rgb_ff(0, 0xfa, 0xfa, 0xfa); /* menu bg */
				init_color_rgb_ff(1, 0x17, 0x17, 0x17); /* menu fg */
				init_color_rgb_ff(2, 0x4e, 0x4e, 0x4e); /* cursor bg */
				init_color_rgb_ff(3, 0xaa, 0xaa, 0xaa); /* shadow */
				init_color_rgb_ff(4, 0x66, 0x66, 0x66); /* shadow */

				set_rgb_color_pair(&config->menu_background_cpn, &config->menu_background_attr, 1, 0, "cc", 0);
				set_rgb_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, 1, 0, "cc", 0);
				set_rgb_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, 4, 3, "cc", 0);
				set_rgb_color_pair(&config->accelerator_cpn, &config->accelerator_attr, 1, 0, "cc", A_UNDERLINE | A_BOLD);
				set_rgb_color_pair(&config->cursor_cpn, &config->cursor_attr, 0, 2, "cc", 0);
				set_rgb_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, 0, 2, "cc", A_UNDERLINE);
				set_rgb_color_pair(&config->disabled_cpn, &config->disabled_attr, 4, 0, "cc", 0);
			}
			else
			{
				/* fallback */
				set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, true);
				set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, true);
				set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_BLUE, COLOR_BLACK, false);
				set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_BLACK, COLOR_WHITE, true);
				config->accelerator_attr |= A_UNDERLINE;

				set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, true);
				set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);
				config->cursor_accel_attr |= A_UNDERLINE;

				set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);
				config->disabled_attr |= A_DIM;
			}

			config->left_alligned_shortcuts = true;
			config->wide_vborders = true;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_XGOLD:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLUE, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLUE, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_CYAN, COLOR_BLUE, false);
			config->menu_shadow_attr |= A_DIM | A_REVERSE;

			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_YELLOW, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLUE, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLUE, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 2;

			config->submenu_offset_y = 0;
			config->submenu_offset_x = 0;

			break;

		case ST_MENU_STYLE_XGOLD_BLACK:
			set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_CYAN, false);
			set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_CYAN, COLOR_BLUE, false);
			config->menu_shadow_attr |= A_DIM | A_REVERSE;

			set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_YELLOW, COLOR_CYAN, true);
			set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLUE, true);
			set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLUE, true);
			set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_CYAN, true);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 2;

			config->submenu_offset_y = 0;
			config->submenu_offset_x = 0;

			break;

		case ST_MENU_STYLE_FLATWHITE:
			if (start_from_rgb)
			{
				custom_color_start = *start_from_rgb;
				*start_from_rgb += 7;

				init_color_rgb_ff(0, 0xb9, 0xA9, 0x92); /* menu bg */
				init_color_rgb_ff(1, 0x17, 0x17, 0x17); /* menu fg */
				init_color_rgb_ff(2, 0x6a, 0x4d, 0xff); /* cursor bg */
				init_color_rgb_ff(5, 0xff, 0xff, 0xff); /* cursor fg */
				init_color_rgb_ff(3, 0x84, 0x7e, 0x76); /* shadow */
				init_color_rgb_ff(4, 0x66, 0x66, 0x66); /* disabled */
				init_color_rgb_ff(6, 0x00, 0x00, 0x00); /* accel */

				set_rgb_color_pair(&config->menu_background_cpn, &config->menu_background_attr, 1, 0, "cc", 0);
				set_rgb_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, 1, 0, "cc", 0);
				set_rgb_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_WHITE, 3, "bc", 0);
				set_rgb_color_pair(&config->accelerator_cpn, &config->accelerator_attr, 6, 0, "cc", A_UNDERLINE);
				set_rgb_color_pair(&config->cursor_cpn, &config->cursor_attr, 5, 2, "cc", 0);
				set_rgb_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, 5, 2, "cc", A_UNDERLINE);
				set_rgb_color_pair(&config->disabled_cpn, &config->disabled_attr, 4, 0, "cc", 0);
			}
			else
			{
				/* fallback */
				set_color_pair(&config->menu_background_cpn, &config->menu_background_attr, COLOR_BLACK, COLOR_WHITE, true);
				set_color_pair(&config->menu_unfocused_cpn, &config->menu_unfocused_attr, COLOR_BLACK, COLOR_WHITE, true);
				set_color_pair(&config->menu_shadow_cpn, &config->menu_shadow_attr, COLOR_BLUE, COLOR_BLACK, false);
				set_color_pair(&config->accelerator_cpn, &config->accelerator_attr, COLOR_BLACK, COLOR_WHITE, true);
				config->accelerator_attr |= A_UNDERLINE;

				set_color_pair(&config->cursor_cpn, &config->cursor_attr, COLOR_WHITE, COLOR_BLACK, true);
				set_color_pair(&config->cursor_accel_cpn, &config->cursor_accel_attr, COLOR_WHITE, COLOR_BLACK, true);
				config->cursor_accel_attr |= A_UNDERLINE;

				set_color_pair(&config->disabled_cpn, &config->disabled_attr, COLOR_BLACK, COLOR_WHITE, true);
				config->disabled_attr |= A_DIM;
			}

			config->left_alligned_shortcuts = true;
			config->wide_vborders = true;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;
	}

	return current_cpn;
}
