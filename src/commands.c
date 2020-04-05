/*-------------------------------------------------------------------------
 *
 * commands.c
 *	  a list of commands and translations between keys and commands
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/commands.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"
#include "commands.h"

int		CTRL_HOME;
int		CTRL_END;


#ifdef NCURSES_EXT_FUNCS

static int
get_code(const char *capname, int fallback)
{

#ifdef NCURSES_EXT_FUNCS

	char	*s;
	int		result;

	s = tigetstr((NCURSES_CONST char *) capname);

	if (s == NULL || s == (char *) -1)
		return fallback;

	result = key_defined(s);
	return result > 0 ? result : fallback;

#else

	return fallback;

#endif

}

#endif

/*
 * Set a value of CTRL_HOME and CTRL_END key codes. These codes
 * can be redefined on some plaforms.
 */
void
initialize_special_keycodes()
{

#ifdef NCURSES_EXT_FUNCS

	use_extended_names(TRUE);

#endif

	CTRL_HOME = get_code("kHOM5", 538);
	CTRL_END = get_code("kEND5", 533);

}

/*
 * For debug purposes
 */
const char *
cmd_string(int cmd)
{
	switch (cmd)
	{
		case cmd_Invalid:
			return "Invalid";
		case cmd_RESIZE_EVENT:
			return "RESIZE";
		case cmd_MOUSE_EVENT:
			return "MOUSE";

		case cmd_ReleaseCols:
			return "ReleaseCols";
		case cmd_FreezeOneCol:
			return "FreezeOneCol";
		case cmd_FreezeTwoCols:
			return "FreezeTwoCols";
		case cmd_FreezeThreeCols:
			return "FreezeThreeCols";
		case cmd_FreezeFourCols:
			return "FreezeFourCols";
		case cmd_FreezeFiveCols:
			return "FreezeFiveCols";
		case cmd_FreezeSixCols:
			return "FreezeSixCols";
		case cmd_FreezeSevenCols:
			return "FreezeSevenCols";
		case cmd_FreezeEightCols:
			return "FreezeEightCols";
		case cmd_FreezeNineCols:
			return "FreezeNineCols";
		case cmd_SoundToggle:
			return "SoundTogle";
		case cmd_MouseToggle:
			return "MouseTogle";
		case cmd_UtfArtToggle:
			return "UtfArtToggle";
		case cmd_MenuAsciiArtToggle:
			return "MenuAsciiArtToggle";
		case cmd_CSSearchSet:
			return "CSSearchSet";
		case cmd_CISearchSet:
			return "CISearchSet";
		case cmd_USSearchSet:
			return "USSearchSet";
		case cmd_HighlightLines:
			return "HighlightLines";
		case cmd_HighlightValues:
			return "HighlightValues";
		case cmd_NoHighlight:
			return "NoHighlight";

		case cmd_SetTheme_MidnightBlack:
			return "SetTheme_MidnightBlack";
		case cmd_SetTheme_Midnight:
			return "SetTheme_Midnight";
		case cmd_SetTheme_Foxpro:
			return "SetTheme_Foxpro";
		case cmd_SetTheme_Pdmenu:
			return "SetTheme_Pdmenu";
		case cmd_SetTheme_White:
			return "SetTheme_White";
		case cmd_SetTheme_Mutt:
			return "SetTheme_Mutt";
		case cmd_SetTheme_Pcfand:
			return "SetTheme_Pcfand";
		case cmd_SetTheme_Green:
			return "SetTheme_Green";
		case cmd_SetTheme_Blue:
			return "SetTheme_Blue";
		case cmd_SetTheme_WP:
			return "SetTheme_WP";
		case cmd_SetTheme_Lowcontrast:
			return "SetTheme_Lowcontrast";
		case cmd_SetTheme_Darkcyan:
			return "SetTheme_Darkcyan";
		case cmd_SetTheme_Paradox:
			return "SetTheme_Paradox";
		case cmd_SetTheme_DBase:
			return "SetTheme_DBase";
		case cmd_SetTheme_DBasemagenta:
			return "SetTheme_DBasemagenta";
		case cmd_SetTheme_Red:
			return "SetTheme_Red";
		case cmd_SetTheme_Simple:
			return "SetTheme_Simple";
		case cmd_SetTheme_SolarDark:
			return "SetTheme_SolarDark";
		case cmd_SetTheme_SolarLight:
			return "SetTheme_SolarLight";
		case cmd_SetTheme_GruvboxLight:
			return "SetTheme_GruvboxLight";
		case cmd_SetTheme_TaoLight:
			return "SetTheme_TaoLight";
		case cmd_SaveSetup:
			return "SaveSetup";

		case cmd_Escape:
			return "Escape";
		case cmd_Quit:
			return "Quit";
		case cmd_RawOutputQuit:
			return "RawOutputQuit";
		case cmd_ShowMenu:
			return "ShowMenu";
		case cmd_FlushBookmarks:
			return "FlushBookmarks";
		case cmd_ToggleBookmark:
			return "ToggleBookmark";
		case cmd_PrevBookmark:
			return "PrevBookmark";
		case cmd_NextBookmark:
			return "NextBookmark";
		case cmd_CursorUp:
			return "CursorUp";
		case cmd_CursorDown:
			return "CursorDown";
		case cmd_ScrollUp:
			return "ScrollUp";
		case cmd_ScrollDown:
			return "ScrollDown";
		case cmd_ScrollUpHalfPage:
			return "ScrollUpHalfPage";
		case cmd_ScrollDownHalfPage:
			return "ScrollDownHalfPage";
		case cmd_MoveLeft:
			return "MoveLeft";
		case cmd_MoveRight:
			return "MoveRight";
		case cmd_CursorFirstRow:
			return "CursorFirstRow";
		case cmd_CursorLastRow:
			return "CursorLastRow";
		case cmd_CursorFirstRowPage:
			return "CursorFirstRowPage";
		case cmd_CursorLastRowPage:
			return "CursorLastRowPage";
		case cmd_CursorHalfPage:
			return "CursorHalfPage";
		case cmd_PageUp:
			return "PageUp";
		case cmd_PageDown:
			return "PageDown";
		case cmd_ShowFirstCol:
			return "ShowFirstCol";
		case cmd_ShowLastCol:
			return "ShowLastCol";
		case cmd_SaveData:
			return "SaveData";
		case cmd_ForwardSearch:
			return "ForwardSearch";
		case cmd_BackwardSearch:
			return "BackwardSearch";
		case cmd_SearchNext:
			return "SearchNext";
		case cmd_SearchPrev:
			return "SearchPrev";
		case cmd_SearchColumn:
			return "SearchColumn";
		case cmd_ShowTopBar:
			return "ShowTopBar";
		case cmd_ShowBottomBar:
			return "ShowBottomBar";
		case cmd_RowNumToggle:
			return "RowNumToggle";
		case cmd_GotoLine:
			return "GotoLine";

		case cmd_ShowCursor:
			return "ShowCursor";
		case cmd_ShowVerticalCursor:
			return "ShowVerticalCursor";

		case cmd_BoldLabelsToggle:
			return "BoldLabelsToggle";
		case cmd_BoldCursorToggle:
			return "BoldCursorToggle";

		case cmd_SortAsc:
			return "SortAsc";
		case cmd_SortDesc:
			return "SortDesc";
		case cmd_OriginalSort:
			return "OriginalSort";

		case cmd_TogglePause:
			return "TogglePause";

		case cmd_Refresh:
			return "Refresh";

		default:
			return "unknown command";
	}
}


int
translate_event(int c, bool alt, Options *opts)
{
	if (alt)
	{
		switch (c)
		{
			case 'b':
				return cmd_BoldLabelsToggle;
			case 'c':
				return cmd_ShowCursor;
			case 'l':
				return cmd_GotoLine;
			case 'm':
				return cmd_MouseToggle;
			case 'n':
				return cmd_RowNumToggle;
			case 'o':
				return cmd_FlushBookmarks;
			case 'k':
				return cmd_ToggleBookmark;
			case 'i':
				return cmd_PrevBookmark;
			case 'j':
				return cmd_NextBookmark;
			case 'q':
				return cmd_RawOutputQuit;
			case 'v':
				return cmd_ShowVerticalCursor;
			case '9':
				return cmd_ShowMenu;
			case 27:
				return cmd_Escape;
			case '0':
				return cmd_Quit;
		}
	}
	else
	{
		switch (c)
		{
			case KEY_RESIZE:
				return cmd_RESIZE_EVENT;
			case KEY_MOUSE:
				return cmd_MOUSE_EVENT;
			case KEY_F(9):
				return cmd_ShowMenu;
			case KEY_F(10):
			case 'q':
				return cmd_Quit;
			case KEY_F(3):
				if (opts->quit_on_f3)
					return cmd_Quit;
				break;
			case KEY_UP:
			case 'k':
				return cmd_CursorUp;
			case KEY_DOWN:
			case 'j':
				return cmd_CursorDown;
			case '0':
				return cmd_ReleaseCols;
			case '1':
				return cmd_FreezeOneCol;
			case '2':
				return cmd_FreezeTwoCols;
			case '3':
				return cmd_FreezeThreeCols;
			case '4':
				return cmd_FreezeFourCols;
			case '5':
				return cmd_FreezeFiveCols;
			case '6':
				return cmd_FreezeSixCols;
			case '7':
				return cmd_FreezeSevenCols;
			case '8':
				return cmd_FreezeEightCols;
			case '9':
				return cmd_FreezeNineCols;
			case 4:		/* CTRL D */
				return cmd_ScrollDownHalfPage;
			case 21:	/* CTRL U */
				return cmd_ScrollUpHalfPage;
			case 5:		/* CTRL E */
				return cmd_ScrollDown;
			case 25:	/* CTRL Y */
				return cmd_ScrollUp;
			case KEY_LEFT:
			case 'h':
				return cmd_MoveLeft;
			case KEY_RIGHT:
			case 'l':
				return cmd_MoveRight;
			case 'H':
				return cmd_CursorFirstRowPage;
			case 'L':
				return cmd_CursorLastRowPage;
			case 'M':
				return cmd_CursorHalfPage;
			case KEY_PPAGE:
			case 2:		/* CTRL B */
				return cmd_PageUp;
			case ' ':
				return opts->watch_time > 0 ? cmd_TogglePause : cmd_PageDown;
			case 6:		/* CTRL F */
			case KEY_NPAGE:
				return cmd_PageDown;
			case KEY_HOME:
			case '^':
				return cmd_ShowFirstCol;
			case KEY_END:
			case '$':
				return cmd_ShowLastCol;
			case 's':
				return cmd_SaveData;
			case '/':
				return cmd_ForwardSearch;
			case '?':
				return cmd_BackwardSearch;
			case 'n':
				return cmd_SearchNext;
			case 'N':
				return cmd_SearchPrev;
			case 'g':
				return cmd_CursorFirstRow;
			case 'G':
				return cmd_CursorLastRow;
			case 'c':
				return cmd_SearchColumn;
			case 'a':
				return cmd_SortAsc;
			case 'd':
				return cmd_SortDesc;
			case 'u':
				return cmd_OriginalSort;
			case 'R':
			case 12:	/* CTRL L */
				return cmd_Refresh;
		}
	}

	if (c == CTRL_HOME)
		return cmd_CursorFirstRow;
	else if (c == CTRL_END)
		return cmd_CursorLastRow;

	return cmd_Invalid;
}

/*
 * Returns cmd for theme
 *
 */
int
theme_get_cmd(int theme)
{
	switch (theme)
	{
		case 0:
			return cmd_SetTheme_MidnightBlack;
		case 1:
			return cmd_SetTheme_Midnight;
		case 2:
			return cmd_SetTheme_Foxpro;
		case 3:
			return cmd_SetTheme_Pdmenu;
		case 4:
			return cmd_SetTheme_White;
		case 5:
			return cmd_SetTheme_Mutt;
		case 6:
			return cmd_SetTheme_Pcfand;
		case 7:
			return cmd_SetTheme_Green;
		case 8:
			return cmd_SetTheme_Blue;
		case 9:
			return cmd_SetTheme_WP;
		case 10:
			return cmd_SetTheme_Lowcontrast;
		case 11:
			return cmd_SetTheme_Darkcyan;
		case 12:
			return cmd_SetTheme_Paradox;
		case 13:
			return cmd_SetTheme_DBase;
		case 14:
			return cmd_SetTheme_DBasemagenta;
		case 15:
			return cmd_SetTheme_Red;
		case 16:
			return cmd_SetTheme_Simple;
		case 17:
			return cmd_SetTheme_SolarDark;
		case 18:
			return cmd_SetTheme_SolarLight;
		case 19:
			return cmd_SetTheme_GruvboxLight;
		case 20:
			return cmd_SetTheme_TaoLight;

	};

	return cmd_Invalid;
}

/*
 * Returns theme for cmd
 *
 */
int
cmd_get_theme(int cmd)
{
	switch (cmd)
	{
		case cmd_SetTheme_MidnightBlack:
			return 0;
		case cmd_SetTheme_Midnight:
			return 1;
		case cmd_SetTheme_Foxpro:
			return 2;
		case cmd_SetTheme_Pdmenu:
			return 3;
		case cmd_SetTheme_White:
			return 4;
		case cmd_SetTheme_Mutt:
			return 5;
		case cmd_SetTheme_Pcfand:
			return 6;
		case cmd_SetTheme_Green:
			return 7;
		case cmd_SetTheme_Blue:
			return 8;
		case cmd_SetTheme_WP:
			return 9;
		case cmd_SetTheme_Lowcontrast:
			return 10;
		case cmd_SetTheme_Darkcyan:
			return 11;
		case cmd_SetTheme_Paradox:
			return 12;
		case cmd_SetTheme_DBase:
			return 13;
		case cmd_SetTheme_DBasemagenta:
			return 14;
		case cmd_SetTheme_Red:
			return 15;
		case cmd_SetTheme_Simple:
			return 16;
		case cmd_SetTheme_SolarDark:
			return 17;
		case cmd_SetTheme_SolarLight:
			return 18;
		case cmd_SetTheme_GruvboxLight:
			return 19;
		case cmd_SetTheme_TaoLight:
			return 20;
	};

	return 1;
}
