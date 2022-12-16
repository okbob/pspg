/*-------------------------------------------------------------------------
 *
 * inputs.h
 *	  generic processing of input
 *
 * Portions Copyright (c) 2017-2022 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/inputs.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PSPG_INPUT_H
#define PSPG_INPUT_H

#if defined HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#include <ncurses.h>
#elif defined HAVE_CURSES_H
#include <curses.h>
#else
/* fallback */
#include <ncurses/ncurses.h>
#endif

#include "pspg.h"

/*
 * NCurses event - hold all data of ncurses event
 */
typedef struct
{
	int		keycode;
	bool	alt;
	bool	ignore_it;
	MEVENT	mevent;
} NCursesEventData;

typedef enum
{
	PSPG_NCURSES_EVENT,						/* classic ncurses event - keyboard, mouse, resize terminal */
	PSPG_READ_DATA_EVENT,					/* signal of new data on input */
	PSPG_TIMEOUT_EVENT,						/* got a timeout */
	PSPG_SIGINT_EVENT,						/* got a sigint */
	PSPG_FATAL_EVENT,						/* got a fatal error */
	PSPG_ERROR_EVENT,						/* got a error with error message */
	PSPG_NOTHING_VALID_EVENT,				/* got an error, but this error can be ignored */
} PspgEventType;

enum
{
	STREAM_IS_UNKNOWN				= 0,
	STREAM_IS_FIFO					= 1,		/* can be FIFO or PIPE */
	STREAM_IS_PIPE					= 1 << 2,
	STREAM_IS_FILE					= 1 << 3,
	STREAM_CAN_BE_REOPENED			= 1 << 4,
	STREAM_IS_IN_NONBLOCKING_MODE	= 1 << 5,
	STREAM_HAS_NOTIFY_SUPPORT		= 1 << 6,
	STREAM_IS_CLOSED				= 1 << 7,
};

extern FILE	   *f_data;
extern FILE	   *f_tty;
extern int		f_data_opts;

extern int		pspg_esc_delay;

extern int get_pspg_event(NCursesEventData *nced, bool only_tty_events, int timeout);
extern void unget_pspg_event(NCursesEventData *nced);

extern void detect_file_truncation(void);
extern void save_file_position(void);
extern bool open_data_stream(Options *opts);
extern void close_data_stream(void);

extern bool open_tty_stream(void);
extern void close_tty_stream(void);

extern int wait_on_press_any_key(void);
extern const char *get_input_file_basename(void);

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

extern void clean_notify_poll(void);

#endif

#endif

