/*-------------------------------------------------------------------------
 *
 * menu.c
 *	 holds menu related code
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/menu.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdbool.h>

#include "pspg.h"
#include "st_menu.h"
#include "commands.h"

#define MENU_ITEM_THEME				1
#define MENU_ITEM_OPTIONS			2

ST_CMDBAR_ITEM _bottombar[] = {
	{"Save", false, 2, cmd_SaveData, 0},
	{"Search", false, 7, cmd_ForwardSearch, 0},
	{"Menu", false, 9, cmd_ShowMenu, 0},
	{"Quit", false, 10, cmd_Quit, 0},
	{NULL, false, 0, 0, 0}
};

ST_CMDBAR_ITEM _bottombar_alt1[] = {
	{"Save", false, 2, cmd_SaveData, 0},
	{"Quit", false, 3, cmd_Quit, 0},
	{"Search", false, 7, cmd_ForwardSearch, 0},
	{"Menu", false, 9, cmd_ShowMenu, 0},
	{"Quit", false, 10, cmd_Quit, 0},
	{NULL, false, 0, 0, 0}
};

ST_MENU_ITEM _file[] = {
	{"~S~ave", cmd_SaveData, "s", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~R~aw output quit", cmd_RawOutputQuit, "M-q", 0, 0, 0, NULL},
	{"E~x~it", cmd_Quit, "q, F10", 0, 0, 0, NULL},
	{NULL, 0, NULL, 0, 0, 0, NULL}
};

/*
 * This content must be permanent for all menu life cycle. State variable
 * menu references on this content.
 */
ST_MENU_ITEM _search[] = {
	{"~S~earch", cmd_ForwardSearch, "/", 0, 0, 0, NULL},
	{"Search ~b~ackward", cmd_BackwardSearch, "?", 0, 0, 0, NULL},
	{"Search ~a~gain", cmd_SearchNext, "n", 0, 0, 0, NULL},
	{"Search p~r~evious", cmd_SearchPrev, "N", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Search ~c~olumn", cmd_SearchColumn, "c", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~T~oggle bookmark", cmd_ToggleBookmark, "M-k", 0, 0, 0, NULL},
	{"~P~rev bookmark", cmd_PrevBookmark, "M-i", 0, 0, 0, NULL},
	{"~N~ext bookmark", cmd_NextBookmark, "M-j", 0, 0, 0, NULL},
	{"~F~lush bookmarks", cmd_FlushBookmarks, "M-o", 0, 0, 0, NULL},
	{NULL, 0, NULL, 0, 0, 0, NULL}
};

ST_MENU_ITEM _command[] = {
	{"_0_Release fixed columns", cmd_ReleaseCols, "0", 0, 0, 0, NULL},
	{"_1_Freeze one column", cmd_FreezeOneCol, "1", 0, 0, 0, NULL},
	{"_2_Freeze two columns", cmd_FreezeTwoCols, "2", 0, 0, 0, NULL},
	{"_3_Freeze three columns", cmd_FreezeThreeCols, "3", 0, 0, 0, NULL},
	{"_4_Freeze four columns", cmd_FreezeFourCols, "4", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~P~rev row", cmd_CursorUp, "k, Key up", 0, 0, 0, NULL},
	{"~N~ext row", cmd_CursorDown, "j, Key down", 0, 0, 0, NULL},
	{"Move to l~e~ft", cmd_MoveLeft, "h, Key left", 0, 0, 0, NULL},
	{"Move to ~r~ight", cmd_MoveRight, "l, Key right", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Go to ~f~irst row", cmd_CursorFirstRow, "g, C-Home", 0, 0, 0, NULL},
	{"Go to l~a~st row", cmd_CursorLastRow, "G, C-End", 0, 0, 0, NULL},
	{"Go to ~l~ine", cmd_GotoLine, "M-l", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~S~how first column", cmd_ShowFirstCol, "^, Home", 0, 0, 0, NULL},
	{"Sho~w~ last column", cmd_ShowLastCol, "$, End", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Page up", cmd_PageUp, "C-b, Prev page", 0, 0, 0, NULL},
	{"Page down", cmd_PageDown, "C-f, space, Next page", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"As~c~ending order", cmd_SortAsc, "a", 0, 0, 0, NULL},
	{"~D~escending order", cmd_SortDesc, "d", 0, 0, 0, NULL},
	{"~O~riginal order", cmd_OriginalSort, "u", 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Refres~h~ screen", cmd_Refresh, "R, C-l", 0, 0, 0, NULL},
	{NULL, 0, NULL, 0, 0, 0, NULL}
};

ST_MENU_ITEM _theme[] = {
	{"_0_Midnight black", cmd_SetTheme_MidnightBlack, NULL, 0, 0, 0, NULL},
	{"_1_Midnight theme", cmd_SetTheme_Midnight, NULL, 0, 0, 0, NULL},
	{"_2_FoxPro like", cmd_SetTheme_Foxpro, NULL, 0, 0, 0, NULL},
	{"_3_Pdmenu like", cmd_SetTheme_Pdmenu, NULL, 0, 0, 0, NULL},
	{"_4_White theme", cmd_SetTheme_White, NULL, 0, 0, 0, NULL},
	{"_5_Mutt theme", cmd_SetTheme_Mutt, NULL, 0, 0, 0, NULL},
	{"_6_PC Fand like", cmd_SetTheme_Pcfand, NULL, 0, 0, 0, NULL},
	{"_7_Green theme", cmd_SetTheme_Green, NULL, 0, 0, 0, NULL},
	{"_8_Blue theme", cmd_SetTheme_Blue, NULL, 0, 0, 0, NULL},
	{"_9_Word perfect theme", cmd_SetTheme_WP, NULL, 0, 0, 0, NULL},
	{"_l_Low contrast blue theme", cmd_SetTheme_Lowcontrast, NULL, 0, 0, 0, NULL},
	{"_c_Dark cyan theme", cmd_SetTheme_Darkcyan, NULL, 0, 0, 0, NULL},
	{"_p_Paradox like", cmd_SetTheme_Paradox, NULL, 0, 0, 0, NULL},
	{"_d_DbaseIV retro", cmd_SetTheme_DBase, NULL, 0, 0, 0, NULL},
	{"_e_DbaseIV retro (Magenta)", cmd_SetTheme_DBasemagenta, NULL, 0, 0, 0, NULL},
	{"_r_Red white theme", cmd_SetTheme_Red, NULL, 0, 0, 0, NULL},
	{"_s_Simple theme", cmd_SetTheme_Simple, NULL, 0, 0, 0, NULL},
	{"_o_Solar Dark theme", cmd_SetTheme_SolarDark, NULL, 0, 0, 0, NULL},
	{"_g_Solar Light theme", cmd_SetTheme_SolarLight, NULL, 0, 0, 0, NULL},
	{"_u_Gruvbox Light theme", cmd_SetTheme_GruvboxLight, NULL, 0, 0, 0, NULL},
	{"_t_Tao Light theme", cmd_SetTheme_TaoLight, NULL, 0, 0, 0, NULL},
	{NULL},
};

ST_MENU_ITEM _options[] = {
	{"~C~ase sensitive search", cmd_CSSearchSet, NULL, 0, 0, 0, NULL},
	{"Case ~i~nsensitive search", cmd_CISearchSet, NULL, 0, 0, 0, NULL},
	{"~U~pper case sensitive search", cmd_USSearchSet, NULL, 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Highlight searched ~l~ines", cmd_HighlightLines, NULL, 0, 0, 0, NULL},
	{"Highlight searched ~v~alues", cmd_HighlightValues, NULL, 0, 0, 0, NULL},
	{"~W~ithout highlighting", cmd_NoHighlight, NULL, 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Show cursor", cmd_ShowCursor, "M-c", 0, 0, 0, NULL},
	{"Show vertical cursor", cmd_ShowVerticalCursor, "M-v", 0, 0, 0, NULL},
	{"Show line ~n~umbers", cmd_RowNumToggle, "M-n", 0, 0, 0, NULL},
	{"Show top bar", cmd_ShowTopBar, NULL, 0, 0, 0, NULL},
	{"Show bottom bar", cmd_ShowBottomBar, NULL, 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~M~ouse support", cmd_MouseToggle, "M-m", 0, 0, 0, NULL},
	{"~Q~uiet mode", cmd_SoundToggle, NULL, 0, 0, 0, NULL},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"Force unicode ~b~orders", cmd_UtfArtToggle, NULL, 0, 0, 0, NULL},
	{"Force ~a~scii menu", cmd_MenuAsciiArtToggle, NULL, 0, 0, 0, NULL},
	{"Bold labels", cmd_BoldLabelsToggle, "M-b", 0, 0, 0, NULL},
	{"Bold cursor", cmd_BoldCursorToggle, NULL, 0, 0, 0, NULL},
	{"~T~heme", MENU_ITEM_THEME, NULL, 0, 0,  0, _theme},
	{"--", 0, NULL, 0, 0, 0, NULL},
	{"~S~ave setup", cmd_SaveSetup, NULL, 0, 0, 0, NULL},
	{NULL},
};

ST_MENU_ITEM menubar[] = {
  {"~F~ile", 0, NULL, 0, 0, 0, _file},
  {"~S~earch", 0, NULL, 0, 0, 0, _search},
  {"~C~ommand", 0, NULL, 0, 0, 0, _command},
  {"~O~ptions", MENU_ITEM_OPTIONS, NULL, 0, 0, 0, _options},
  {NULL}
};


ST_MENU_CONFIG		menu_config;
ST_MENU_CONFIG		menu_config2;
int menu_theme = -1;


/*
 * Returns menu style joined to main theme
 */
static int
get_menu_style(int main_theme)
{
	switch (main_theme)
	{
		case 0:
			return ST_MENU_STYLE_MCB;
		case 1:
			return ST_MENU_STYLE_MC;
		case 2:
			return ST_MENU_STYLE_FOXPRO;
		case 3:
			return ST_MENU_STYLE_DOS;
		case 4:
			return ST_MENU_STYLE_FAND_1;
		case 5:
			return ST_MENU_STYLE_NOCOLOR;
		case 6:
			return ST_MENU_STYLE_FAND_1;
		case 7:
			return ST_MENU_STYLE_ONECOLOR;
		case 8:
			return ST_MENU_STYLE_DOS;
		case 9:
			return ST_MENU_STYLE_PERFECT;
		case 10:
			return ST_MENU_STYLE_XGOLD_BLACK;
		case 11:
			return ST_MENU_STYLE_OLD_TURBO;
		case 12:
			return ST_MENU_STYLE_VISION;
		case 13:
			return ST_MENU_STYLE_DBASE;
		case 14:
			return ST_MENU_STYLE_OLD_TURBO;
		case 15:
			return ST_MENU_STYLE_PERFECT;
		case 16:
			return ST_MENU_STYLE_ONECOLOR;
		case 20:
			return ST_MENU_STYLE_TAO;
		default:
			return ST_MENU_STYLE_VISION;
	}
}

/*
 * Prepare configuration for st_menu
 */
void
init_menu_config(Options *opts)
{
	int start_from_rgb = 220;

	menu_config.force8bit = opts->force8bit;
	menu_config.force_ascii_art = opts->force_ascii_art;
	menu_config.language = NULL;
	menu_config.encoding = NULL;

	menu_config2.force8bit = opts->force8bit;
	menu_config2.language = NULL;
	menu_config2.encoding = NULL;

	menu_theme = get_menu_style(opts->theme);

	if (menu_theme == ST_MENU_STYLE_FREE_DOS)
	{
		int		fcp;

		fcp = st_menu_load_style(&menu_config, menu_theme, 30);
		st_menu_load_style(&menu_config2, ST_MENU_STYLE_FREE_DOS_P, fcp);
	}
		st_menu_load_style_rgb(&menu_config, menu_theme,
								menu_theme == ST_MENU_STYLE_ONECOLOR ? 3 : 30, &start_from_rgb);

	if (opts->theme == 1)
		menu_config.shadow_width = 2;
	else if (opts->theme == 4)
		menu_config.text_space = 4;

	menu_config.force_ascii_art = opts->force_ascii_art;
	menu_config2.force_ascii_art = opts->force_ascii_art;
}


/*
 * Prepare configuration and initialize menu
 */
struct ST_MENU *
init_menu(struct ST_MENU *current_menu)
{
	struct ST_MENU		*menu = NULL;

	if (menu_theme == ST_MENU_STYLE_FREE_DOS)
		menu = st_menu_new_menubar2(&menu_config, &menu_config2, menubar);
	else
		menu = st_menu_new_menubar(&menu_config, menubar);

	if (current_menu)
	{
		int		positions[1024];
		int		*refvals[1024];

		st_menu_save(current_menu, positions, refvals, 1023);
		st_menu_load(menu, positions, refvals);
		st_menu_free(current_menu);
	}

	return menu;
}

struct ST_CMDBAR *
init_cmdbar(struct ST_CMDBAR *current_cmdbar, Options *opts)
{
	struct ST_CMDBAR	   *cmdbar = NULL;

	if (opts->quit_on_f3)
		cmdbar = st_cmdbar_new(&menu_config, _bottombar_alt1);
	else
		cmdbar = st_cmdbar_new(&menu_config, _bottombar);

	if (current_cmdbar)
	{
		st_cmdbar_unpost(current_cmdbar);
		st_cmdbar_free(current_cmdbar);
	}

	return cmdbar;
}

void
post_menu(Options *opts, struct ST_MENU *menu)
{
	st_menu_set_option(menu, cmd_ReleaseCols, ST_MENU_OPTION_MARKED, opts->freezed_cols == 0);
	st_menu_set_option(menu, cmd_FreezeOneCol, ST_MENU_OPTION_MARKED,
									  (opts->freezed_cols == 1 || opts->freezed_cols == -1));
	st_menu_set_option(menu, cmd_FreezeTwoCols, ST_MENU_OPTION_MARKED, opts->freezed_cols == 2);
	st_menu_set_option(menu, cmd_FreezeThreeCols, ST_MENU_OPTION_MARKED, opts->freezed_cols == 3);
	st_menu_set_option(menu, cmd_FreezeFourCols, ST_MENU_OPTION_MARKED, opts->freezed_cols == 4);

	st_menu_set_option(menu, cmd_SoundToggle, ST_MENU_OPTION_MARKED, opts->no_sound);
	st_menu_set_option(menu, cmd_UtfArtToggle, ST_MENU_OPTION_MARKED, opts->force_uniborder);
	st_menu_set_option(menu, cmd_MenuAsciiArtToggle, ST_MENU_OPTION_MARKED, opts->force_ascii_art);
	st_menu_set_option(menu, cmd_MouseToggle, ST_MENU_OPTION_MARKED, !opts->no_mouse);

	st_menu_set_option(menu, cmd_NoHighlight, ST_MENU_OPTION_MARKED, opts->no_highlight_search);
	st_menu_set_option(menu, cmd_HighlightValues, ST_MENU_OPTION_MARKED, opts->no_highlight_lines);
	st_menu_set_option(menu, cmd_HighlightLines, ST_MENU_OPTION_MARKED,
									  !(opts->no_highlight_search || opts->no_highlight_lines));

	st_menu_set_option(menu, cmd_CSSearchSet, ST_MENU_OPTION_MARKED,
									  !(opts->ignore_case || opts->ignore_lower_case));
	st_menu_set_option(menu, cmd_CISearchSet, ST_MENU_OPTION_MARKED, opts->ignore_case);
	st_menu_set_option(menu, cmd_USSearchSet, ST_MENU_OPTION_MARKED, opts->ignore_lower_case);

	st_menu_set_option(menu, cmd_ShowTopBar, ST_MENU_OPTION_MARKED, !opts->no_topbar);
	st_menu_set_option(menu, cmd_ShowBottomBar, ST_MENU_OPTION_MARKED, !opts->no_commandbar);

	st_menu_set_option(menu, cmd_RowNumToggle, ST_MENU_OPTION_MARKED, opts->show_rownum);
	st_menu_set_option(menu, cmd_ShowCursor, ST_MENU_OPTION_MARKED, !opts->no_cursor);
	st_menu_set_option(menu, cmd_ShowVerticalCursor, ST_MENU_OPTION_MARKED, opts->vertical_cursor);

	st_menu_set_option(menu, cmd_BoldLabelsToggle, ST_MENU_OPTION_MARKED, opts->bold_labels);
	st_menu_set_option(menu, cmd_BoldCursorToggle, ST_MENU_OPTION_MARKED, opts->bold_cursor);

	st_menu_reset_all_submenu_options(menu, MENU_ITEM_THEME, ST_MENU_OPTION_MARKED);
	st_menu_enable_option(menu, theme_get_cmd(opts->theme), ST_MENU_OPTION_MARKED);
}
