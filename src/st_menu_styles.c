#include "st_menu.h"

#include <ncurses.h>

/*
 * Prepared styles - config settings. start_from_cpn is first color pair 
 * number that can be used by st_menu library. For ST_MENU_STYLE_ONECOLOR
 * style it is number of already existing color pair.
 */
int
st_menu_load_style(ST_MENU_CONFIG *config, int style, int start_from_cpn)
{
	config->submenu_tag = '>';
	config->draw_box = true;
	config->extern_accel_text_space = 2;

	config->submenu_offset_y = 0;
	config->submenu_offset_x = 0;

	switch (style)
	{
		case ST_MENU_STYLE_MCB:
			use_default_colors();
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, -1, -1);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

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

			break;

		case ST_MENU_STYLE_MC:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_VISION:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = A_REVERSE;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

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
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = true;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

#if NCURSES_WIDECHAR > 0

			if (!config->force8bit)
				config->submenu_tag = L'\x25BA';

#endif

			config->submenu_offset_y = 0;
			config->submenu_offset_x = -15;

			break;

		case ST_MENU_STYLE_DOS:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

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

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

#if NCURSES_WIDECHAR > 0

			if (!config->force8bit)
				config->submenu_tag = L'\x00BB';

#endif

			break;

		case ST_MENU_STYLE_FAND_2:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

#if NCURSES_WIDECHAR > 0

			if (!config->force8bit)
				config->submenu_tag = L'\x00BB';

#endif

			break;

		case ST_MENU_STYLE_FOXPRO:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			/* This is different, from original, but cyan text is not readable */
			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_WHITE);

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

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_RED);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_RED);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

#if NCURSES_WIDECHAR > 0

			if (!config->force8bit)
				config->submenu_tag = L'\x2BC8';

#endif

			break;

		case ST_MENU_STYLE_NOCOLOR:
			use_default_colors();
			config->menu_background_cpn = 0;
			config->menu_background_attr = 0;

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
			//use_default_colors();
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;

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

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

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

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

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
			config->menu_background_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLUE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_MAGENTA);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_MAGENTA);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_DIM;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLUE);

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
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

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
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

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

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLACK);

			config->disabled_cpn = start_from_cpn;
			config->disabled_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			break;
	}

	return start_from_cpn;
}
