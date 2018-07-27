/*-------------------------------------------------------------------------
 *
 * command.h
 *	  a list of commands and translations between keys and commands
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/command.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef COMMANDS_H
#define COMMANDS_H

/*
 * List of supported commands. Some these commands can be mapped to keys
 */
typedef enum PspgCommand
{
	cmd_Invalid = 0,
	cmd_ReleaseCols,
	cmd_FreezeOneCol,
	cmd_FreezeTwoCols,
	cmd_FreezeThreeCols,
	cmd_FreezeFourCols,
	cmd_SoundSwitchOver,
	cmd_MouseSwitchOver,
	cmd_UtfArtSwitchOver,
	cmd_CSSearchEnable,
	cmd_CISearchEnable,
	cmd_USSearchEnable,
	cmd_HighlightLinesEnable,
	cmd_HighlightValuesEnable,
	cmd_NoHighlightEnable,
	cmd_SetTheme_MidnightBlack,
	cmd_SetTheme_Midnight,
	cmd_SetTheme_Foxpro,
	cmd_SetTheme_Pdmenu,
	cmd_SetTheme_White,
	cmd_SetTheme_Mutt,
	cmd_SetTheme_Pcfand,
	cmd_SetTheme_Green,
	cmd_SetTheme_Blue,
	cmd_SetTheme_WP,
	cmd_SetTheme_Lowcontrast,
	cmd_SetTheme_Darkcyan,
	cmd_SetTheme_Paradox,
	cmd_SetTheme_DBase,
	cmd_SetTheme_DBaseretro,
	cmd_SetTheme_red,
	cmd_SetTheme_simple,
	cmd_SaveSetup,

	cmd_Quit,
	cmd_ShowMenu,
	cmd_FlushBookmark,
	cmd_SwitchOverBookmark,
	cmd_PrevBookmark,
	cmd_NextBookmark,
	cmd_CursorUp,
	cmd_CursorDown,
	cmd_ScrollUp,
	cmd_ScrollDown,
	cmd_ScrollUpHalfPage,
	cmd_ScrollDownHalfPage
	cmd_ScrollLeft,
	cmd_ScrollRight,
	cmd_CursorFirstRow,
	cmd_CursorLastRow,
	cmd_CursorFirstRowPage,
	cmd_CursorLastRowWPage,
	cmd_CursorHalfPage,
	cmd_PageUp,
	cmd_PageDown,
	cmd_ShowFirstCol,
	cmd_ShowLastCol,
	cmd_SaveData,
	cmd_ForwaredSearch,
	cmd_BackwardSearch,
	cmd_SearchNext,
	cmd_SearchPrev
} PspgCommand;

#endif