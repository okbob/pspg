#include <stdbool.h>

#include "st_menu.h"

#include <string.h>

/* 0..255 rgb based colors */
static void
init_color_rgb_ff(short color, short r, short g, short b)
{
	init_color(color,
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

#define islc(f,b)		(slc(start_from_cpn++, f, b))

/*
 * Prepared styles - config settings. start_from_cpn is first color pair 
 * number that can be used by st_menu library. For ST_MENU_STYLE_ONECOLOR
 * style it is number of already existing color pair.
 */
int
st_menu_load_style(ST_MENU_CONFIG *config, int style, int start_from_cpn)
{
	return st_menu_load_style_rgb(config, style, start_from_cpn, NULL);
}

int
st_menu_load_style_rgb(ST_MENU_CONFIG *config, int style, int start_from_cpn, int *start_from_rgb)
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
	config->force_ascii_art = false;

	config->submenu_offset_y = 0;
	config->submenu_offset_x = 0;

#if defined  HAVE_NCURSESW

	if (!config->force8bit)
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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, -1, -1);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLACK);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = islc(COLOR_WHITE, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_YELLOW, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_GREEN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_GREEN);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_WHITE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_WHITE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_CYAN, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_CYAN);

			/* This is different, from original, but cyan text is not readable */
			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM | islc(COLOR_CYAN, COLOR_WHITE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_RED);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_RED);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_WHITE);

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

			config->menu_shadow_cpn = start_from_cpn;
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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = A_REVERSE;

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_UNDERLINE;

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_REVERSE;

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_UNDERLINE | A_REVERSE;

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_TURBO:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_WHITE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_WHITE, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = islc(COLOR_YELLOW, COLOR_BLUE);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_CYAN, COLOR_BLUE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_YELLOW, COLOR_MAGENTA);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_YELLOW, COLOR_MAGENTA);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM | islc(COLOR_WHITE, COLOR_BLUE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_CYAN);

			/* This is different, from original, but cyan text is not readable */
			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_WHITE);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			/* This is different, from original, but cyan text is not readable */
			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_YELLOW, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_UNDERLINE | islc(COLOR_WHITE, COLOR_BLACK);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_YELLOW, COLOR_CYAN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_UNDERLINE | islc(COLOR_YELLOW, COLOR_CYAN);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM | islc(COLOR_WHITE, COLOR_BLACK);

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
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = islc(COLOR_WHITE, COLOR_RED);

			config->menu_unfocused_cpn = start_from_cpn;
			config->menu_unfocused_attr = islc(COLOR_WHITE, COLOR_BLACK);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLUE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_UNDERLINE | islc(COLOR_YELLOW, COLOR_RED);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_YELLOW, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_UNDERLINE | islc(COLOR_YELLOW, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM | islc(COLOR_WHITE, COLOR_RED);

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
				int		_start_from_rgb = *start_from_rgb;

				*start_from_rgb += 5;

				init_color_rgb_ff(_start_from_rgb + 0, 0xfa, 0xfa, 0xfa); /* menu bg */
				init_color_rgb_ff(_start_from_rgb + 1, 0x17, 0x17, 0x17); /* menu fg */
				init_color_rgb_ff(_start_from_rgb + 2, 0x4e, 0x4e, 0x4e); /* cursor bg */
				init_color_rgb_ff(_start_from_rgb + 3, 0xaa, 0xaa, 0xaa); /* shadow */
				init_color_rgb_ff(_start_from_rgb + 4, 0x66, 0x66, 0x66); /* shadow */

				config->menu_background_cpn = start_from_cpn;
				init_pair(start_from_cpn++, _start_from_rgb + 1, _start_from_rgb + 0);
				config->menu_background_attr = 0;

				config->menu_unfocused_cpn = start_from_cpn;
				init_pair(start_from_cpn++, _start_from_rgb + 1, _start_from_rgb + 0);
				config->menu_unfocused_attr = 0;

				config->menu_shadow_cpn = start_from_cpn;
				config->menu_shadow_attr = 0;
				init_pair(start_from_cpn++, COLOR_WHITE, _start_from_rgb + 3);

				config->accelerator_cpn = start_from_cpn;
				init_pair(start_from_cpn++, _start_from_rgb + 1, _start_from_rgb + 0);
				config->accelerator_attr = A_UNDERLINE | A_BOLD;

				config->cursor_cpn = start_from_cpn;
				init_pair(start_from_cpn++, _start_from_rgb + 0, _start_from_rgb + 2);
				config->cursor_attr = 0;

				config->cursor_accel_cpn = start_from_cpn;
				init_pair(start_from_cpn++, _start_from_rgb + 0, _start_from_rgb + 2);
				config->cursor_accel_attr = A_UNDERLINE ;

				config->disabled_cpn = start_from_cpn;
				init_pair(start_from_cpn, _start_from_rgb + 4, _start_from_rgb + 0);
				config->disabled_attr = 0;
			}
			else
			{
				/* fallback */
				config->menu_background_cpn = start_from_cpn;
				config->menu_background_attr = islc(COLOR_BLACK, COLOR_WHITE);

				config->menu_unfocused_cpn = start_from_cpn;
				config->menu_unfocused_attr = islc(COLOR_BLACK, COLOR_WHITE);

				config->menu_shadow_cpn = start_from_cpn;
				config->menu_shadow_attr = 0;
				init_pair(start_from_cpn++, COLOR_BLUE, COLOR_BLACK);

				config->accelerator_cpn = start_from_cpn;
				config->accelerator_attr = A_UNDERLINE | islc(COLOR_BLACK, COLOR_WHITE);

				config->cursor_cpn = start_from_cpn;
				config->cursor_attr = islc(COLOR_WHITE, COLOR_BLACK);

				config->cursor_accel_cpn = start_from_cpn;
				config->cursor_accel_attr = A_UNDERLINE | islc(COLOR_WHITE, COLOR_BLACK);

				config->disabled_cpn = start_from_cpn;
				config->disabled_attr = A_DIM | islc(COLOR_BLACK, COLOR_WHITE);
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
			config->menu_background_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_BLUE, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_BLUE, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLUE);
			config->menu_shadow_attr = A_DIM | A_REVERSE;

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_unfocused_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLUE);
			config->menu_shadow_attr = A_DIM | A_REVERSE;

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = islc(COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = islc(COLOR_WHITE, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = islc(COLOR_WHITE, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = islc(COLOR_BLACK, COLOR_CYAN);

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

	}

	return start_from_cpn;
}
