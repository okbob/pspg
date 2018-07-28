/*-------------------------------------------------------------------------
 *
 * menu.c
 *	 holds menu related code
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/menu.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"
#include "st_menu.h"
#include "commands.h"

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
			return ST_MENU_STYLE_VISION;
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
		default:
			return ST_MENU_STYLE_VISION;
	}
}

/*
 * Prepare configuration and initialize menu
 */
struct ST_MENU *
init_menu(Options *opts, struct ST_MENU *current_menu)
{
	ST_MENU_CONFIG		menu_config;
	ST_MENU_CONFIG		menu_config2;
	struct ST_MENU		*menu = NULL;

	int		menu_theme;

	ST_MENU_ITEM _file[] = {
		{"~S~ave", cmd_SaveData, "s"},
		{"--"},
		{"E~x~it", cmd_Quit, "q, F10"},
		{NULL}
	};

	ST_MENU_ITEM _search[] = {
		{"~S~earch", cmd_ForwardSearch, "/"},
		{"Search ~b~ackward", cmd_BackwardSearch, "?"},
		{"Search ~a~gain", cmd_SearchNext, "n"},
		{"Search p~r~evious", cmd_SearchPrev, "N"},
		{"--"},
		{"~T~oggle bbooookmark", cmdSwitchOverBookmark, "M-k"},
		{"~P~rev bookmark", cmd_PrevBookmark, "M-i"},
		{"~N~ext bookmark", cmd_NextBookmar, "M-j"},
		{"~F~lush bookmarks", cmd_FlushBookmark, "M-o"},
		{NULL}
	};

	ST_MENU_ITEM _command[] = {
		{"_0_Release fixed columns", cmd_ReleaseCols, "0"},
		{"_1_Freeze one column", cmd_FreezeOneCol, "1"},
		{"_2_Freeze two columns", cmd_FreezeTwoCols, "2"},
		{"_3_Freeze three columns", cmd_FreezeThreeCols, "3"},
		{"_4_Freeze four columns", cmd_FreezeFourCols, "4"},
		{"--"},
		{"~P~rev row", cmd_CursorUp, "k, Key up"},
		{"~N~ext row", cmd_CursorDown, "j, Key down"},
		{"Move to l~e~ft", cmd_MoveLeft, "h, Key left"},
		{"Move to ~r~ight", cmd_MoveRight, "l, Key right"},
		{"--"},
		{"Go to ~f~irst row", cmd_CursorFirstRow, "g, C-Home"},
		{"Go to ~l~ast row", cmd_CursorLastRow, "G, C-End"},
		{"~S~how first column", cmd_ShowFirstCol, "^, Home"},
		{"Sho~w~ last column", cmd_ShowLastCol, "$, End"},
		{"--"},
		{"Page up", cmd_PageUp, "C-b, Prev page"},
		{"Page down", cmd_PageDown, "C-f, space, Next page"},
		{NULL}
	};

	ST_MENU_ITEM _theme[] = {
		{"_0_Midnight black", cmd_SetTheme_MidnightBlack},
		{"_1_Midnight theme", cmd_SetTheme_Midnight},
		{"_2_FoxPro like", cmd_SetTheme_Foxpro},
		{"_3_Pdmenu like", cmd_SetTheme_Pdmenu},
		{"_4_White theme", cmd_SetTheme_White},
		{"_5_Mutt theme", cmd_SetTheme_Mutt},
		{"_6_PC Fand like", cmd_SetTheme_Pcfand},
		{"_7_Green theme", cmd_SetTheme_Green},
		{"_8_Blue theme", cmd_SetTheme_Blue},
		{"_9_Word perfect theme", cmd_SetTheme_WP},
		{"_l_Low contrast blue theme", cmd_SetTheme_Lowcontrast},
		{"_c_Dark cyan theme", cmd_SetTheme_Darkcyan},
		{"_p_Paradox like", cmd_SetTheme_Paradox},
		{"_d_DbaseIV retro", cmd_SetTheme_Dbase},
		{"_e_DbaseIV retro (Magenta)", cmd_SetTheme_Dbasemagenta},
		{"_r_Red white theme", cmd_SetTheme_Red},
		{"_s_Simple theme", cmd_SetTheme_Simple},
		{NULL},
	};

	ST_MENU_ITEM _options[] = {
		{"~C~ase sensitive search", cmd_CSSearchEnable, NULL},
		{"Case ~i~nsensitive search", cmd_CISearchEnable, NULL},
		{"~U~pper case sensitive search", cmd_USSearchEnable, NULL},
		{"--"},
		{"Highlight searched ~l~ines", cmd_HighlightLinesEnable, NULL},
		{"Highlight searched ~v~alues", cmd_HighlightValuesEnable, NULL},
		{"~W~ithout highlighting", cmd_NoHighlight, NULL},
		{"--"},
		{"~M~ouse support", cmd_MouseSwitchOver, "M-m"},
		{"~Q~uiet mode", cmd_SoundSwitchOver, NULL},
		{"--"},
		{"Force unicode ~b~orders", cmd_UtfArtSwitchOver, NULL},
		{"~T~heme", MENU_ITEM_THEME, NULL, 0, 0,  0, _theme},
		{"--"},
		{"~S~ave setup", cmd_SaveSetup, NULL},
		{NULL},
	};

	ST_MENU_ITEM menubar[] = {
	  {"~F~ile", 0, NULL, 0, 0, 0, _file},
	  {"~S~earch", 0, NULL, 0, 0, 0, _search},
	  {"~C~ommand", 0, NULL, 0, 0, 0, _command},
	  {"~O~ptions", 0, NULL, 0, 0, 0, _options},
	  {NULL}
	};

	menu_config.force8bit = opts->force8bit;
	menu_config.language = NULL;
	menu_config.encoding = NULL;

	menu_config2.force8bit = opts->force8bit;
	menu_config2.language = NULL;
	menu_config2.encoding = NULL;

	menu_theme = get_menu_style(opts->theme);

	if (menu_theme == ST_MENU_STYLE_FREE_DOS)
	{
		int		fcp;

		fcp = st_menu_load_style(&menu_config, menu_theme, 100);
		st_menu_load_style(&menu_config2, ST_MENU_STYLE_FREE_DOS_P, fcp);
	}
	else if (menu_theme == ST_MENU_STYLE_ONECOLOR:)
	{
		st_menu_load_style(&menu_config, ST_MENU_STYLE_ONE_COLOR, 3);
	}
	else
		st_menu_load_style(&menu_config, ST_MENU_STYLE_ONE_COLOR, 100);

	if (opts.theme == 1)
		menu_config.shadow_width = 2;
	else if (opts.theme == 4)
		menu_config.text_space = 4;

	if (menu_theme == ST_MENU_STYLE_FREE_DOS)
		menu = st_menu_new_menubar2(&menu_config, &menu_config2, menubar);
	else
		menu = st_menu_new_menubar(&menu_config, menubar);

	if (current_menu)
	{
		int		positions[1024];

		st_menu_save(current_menu, positions, 1023);
		st_menu_load(menu, positions);
		st_menu_free(current_menu);
	}

	return menu;
}
