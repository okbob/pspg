/*-------------------------------------------------------------------------
 *
 * command.h
 *	  a list of commands and translations between keys and commands
 *
 * Portions Copyright (c) 2017-2025 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/commands.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "config.h"

/*
 * List of supported commands. Some these commands can be mapped to keys
 */
typedef enum PspgCommand
{
	cmd_Invalid = 0,
	cmd_RESIZE_EVENT,
	cmd_MOUSE_EVENT,

	cmd_ReleaseCols = 100,
	cmd_FreezeOneCol,
	cmd_FreezeTwoCols,
	cmd_FreezeThreeCols,
	cmd_FreezeFourCols,
	cmd_FreezeFiveCols,
	cmd_FreezeSixCols,
	cmd_FreezeSevenCols,
	cmd_FreezeEightCols,
	cmd_FreezeNineCols,
	cmd_SoundToggle,
	cmd_MouseToggle,
	cmd_UtfArtToggle,
	cmd_MenuAsciiArtToggle,
	cmd_CSSearchSet,
	cmd_CISearchSet,
	cmd_USSearchSet,
	cmd_HighlightLines,
	cmd_HighlightValues,
	cmd_NoHighlight,

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
	cmd_SetTheme_DBasemagenta,
	cmd_SetTheme_Red,
	cmd_SetTheme_Simple,
	cmd_SetTheme_SolarDark,
	cmd_SetTheme_SolarLight,
	cmd_SetTheme_GruvboxLight,
	cmd_SetTheme_TaoLight,
	cmd_SetTheme_Flatwhite,
	cmd_SetTheme_RelationalPipes,
	cmd_SetTheme_PaperColor,
	cmd_SetTheme_Dracula,
	cmd_SetTheme,
	cmd_SetCustomTheme,
	cmd_SaveSetup,

	cmd_Escape,
	cmd_Quit,
	cmd_RawOutputQuit,
	cmd_ShowMenu,
	cmd_FlushBookmarks,
	cmd_ToggleBookmark,
	cmd_PrevBookmark,
	cmd_NextBookmark,
	cmd_CursorUp,
	cmd_CursorDown,
	cmd_ScrollUp,
	cmd_ScrollDown,
	cmd_ScrollUpHalfPage,
	cmd_ScrollDownHalfPage,
	cmd_MoveLeft,
	cmd_MoveRight,
	cmd_MoveCharLeft,
	cmd_MoveCharRight,
	cmd_MoveColumnLeft,
	cmd_MoveColumnRight,
	cmd_CursorFirstRow,
	cmd_CursorLastRow,
	cmd_CursorFirstRowPage,
	cmd_CursorLastRowPage,
	cmd_CursorHalfPage,
	cmd_PageUp,
	cmd_PageDown,
	cmd_ShowFirstCol,
	cmd_ShowLastCol,
	cmd_SaveData,
	cmd_SaveAsCSV,
	cmd_ForwardSearch,
	cmd_BackwardSearch,
	cmd_ForwardSearchInSelection,
	cmd_BackwardSearchInSelection,
	cmd_SearchNext,
	cmd_SearchPrev,
	cmd_SearchColumn,
	cmd_ShowTopBar,
	cmd_ShowBottomBar,
	cmd_RowNumToggle,
	cmd_GotoLine,
	cmd_GotoLineRel,
	cmd_ShowCursor,
	cmd_ShowVerticalCursor,
	cmd_BoldLabelsToggle,
	cmd_BoldCursorToggle,
	cmd_ShowScrollbar,
	cmd_SortAsc,
	cmd_SortDesc,
	cmd_OriginalSort,
	cmd_TogglePause,
	cmd_Refresh,
	cmd_SetCopyFile,
	cmd_SetCopyClipboard,
	cmd_UseClipboard_CSV,
	cmd_UseClipboard_TSVC,
	cmd_UseClipboard_SQL_values,
	cmd_UseClipboard_text,
	cmd_UseClipboard_pipe_separated,
	cmd_UseClipboard_INSERT,
	cmd_UseClipboard_INSERT_with_comments,
	cmd_TogleEmptyStringIsNULL,
	cmd_SetOwnNULLString,

	cmd_Copy,
	cmd_CopyAllLines,
	cmd_CopyTopLines,
	cmd_CopyBottomLines,
	cmd_CopyLine,
	cmd_CopyLineExtended,
	cmd_CopyColumn,
	cmd_CopyMarkedLines,
	cmd_CopySearchedLines,
	cmd_CopySelected,

	cmd_Mark,
	cmd_MarkColumn,
	cmd_MarkAll,
	cmd_Unmark,
	cmd_Mark_NestedCursorCommand,

	cmd_BsCommand,
	cmd_ShowPrimaryScreen,
	cmd_ToggleHighlightOddRec,
	cmd_ToggleHideHeaderLine,
} PspgCommand;

extern void initialize_special_keycodes();
extern const char *cmd_string(int cmd);
extern int translate_event(int c, bool alt, Options *opts, int *nested_command);
extern bool require_complete_load(int cmd);

extern bool is_cmd_RowNumToggle(int c, bool alt);
extern bool key_is_allowed_mark_mode_cursor(int c);


extern int cmd_get_theme(int cmd);
extern int theme_get_cmd(int theme);

#endif
