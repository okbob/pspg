/*-------------------------------------------------------------------------
 *
 * commands.c
 *	  a list of commands and translations between keys and commands
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/commands.c
 *
 *-------------------------------------------------------------------------
 */

#include "pspg.h"
#include "commands.h"

int
translate_event(int c, bool alt)
{
	if (alt)
	{
		switch (c)
		{
			case 'm':
				return cmd_MouseToggle;
			case 'o':
				return cmd_FlushBookmarks;
			case 'k':
				return cmd_ToggleBookmark;
			case 'i':
				return cmd_PrevBookmark;
			case 'j':
				return cmd_NextBookmark;
			case '9':
				return cmd_ShowMenu;
			case 27:
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
			case KEY_NPAGE:
			case ' ':
			case 6:		/* CTRL F */
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
		}
	}

	return cmd_Invalid;
}