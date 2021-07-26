/*-------------------------------------------------------------------------
 *
 * events.c
 *	  handles all events - tty and content
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/inputs.c
 *
 *-------------------------------------------------------------------------
 */
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define PSPG_ESC_DELAY					2000

#ifdef HAVE_INOTIFY

#include <sys/inotify.h>

#endif

#include "inputs.h"
#include "pspg.h"

static char	pathname[MAXPATHLEN] = "";

FILE   *f_tty = NULL;						/* ncurses input stream */
FILE   *f_data = NULL;						/* content input */

int		f_data_opts = 0;						/* describe properties of content input */

int		f_data_fileno = -1;					/* content input used by poll */

static struct pollfd fds[2];

static long last_data_pos = -1;

#ifdef HAVE_INOTIFY

static int inotify_fd = -1;
static int inotify_wd = -1;

#endif

static NCursesEventData saved_event;
static bool saved_event_is_valid = false;

static bool close_f_tty = false;
static bool close_f_data = false;

/*************************************
 * Events processing
 *
 *************************************
 */

/*
 * Read one ncurses event
 */
static int
get_ncurses_event(NCursesEventData *nced, bool *sigint)
{
	bool	first_event = true;
	int		ok = true;

	*sigint = false;

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	wint_t	ch;
	int		ret;

#endif

	/*
	 * When ALT key is used, then ncurses generates two keycodes. And then
	 * we have to read input 2x times.
	 */
repeat:

	errno = 0;

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	ret = get_wch(&ch);
	UNUSED(ret);

	nced->keycode = ch;

#else

	nced->keycode = getch();

#endif

	if (errno == 0)
	{
		if (nced->keycode == KEY_MOUSE)
		{
			ok = getmouse(&nced->mevent) == OK;
		}
		else if (nced->keycode == PSPG_ESC_CODE) /* Escape (before ALT chars) */
		{
			if (first_event)
			{
				first_event = false;
				goto repeat;
			}
		}
	}

	if ((nced->keycode == ERR && errno == EINTR) ||
		 handle_sigint || handle_sigwinch)
	{
		if (handle_sigwinch)
		{
			nced->keycode = KEY_RESIZE;
			return true;
		}
		else if (handle_sigint)
		{
			*sigint = true;
			handle_sigint = false;
			return false;
		}

		return false;
	}

	nced->alt = !first_event;

	return ok;
}

/*
 * When only_tty_events is true, then we don't want to return events related to processed
 * content - new data, inotify event, .. These events are saved, but for this moment ignored.
 * Timeout -1 (timeout is infinity), 0 (no any wait), else timeout in ms.
 */
int
_get_pspg_event(NCursesEventData *nced,
			    bool only_tty_events,
			    int timeout)
{
	bool	sigint;
	bool	first_event = true;
	bool	first_loop = true;
	bool	without_timeout = timeout == -1;
	bool	zero_timeout = timeout == 0;

#ifdef HAVE_INOTIFY

	bool	poll_inotify_fd = false;

#endif

	int		nfds;

	/*
	 * Return saved events.
	 */
	if (saved_event_is_valid)
	{
		memcpy(nced, &saved_event, sizeof(NCursesEventData));
		saved_event_is_valid = false;
		return PSPG_NCURSES_EVENT;
	}
	else if (!only_tty_events && handle_sigint)
	{
		handle_sigint = false;
		return PSPG_SIGINT_EVENT;
	}
	else if (handle_sigwinch)
	{
		handle_sigwinch = false;
		nced->alt = false;
		nced->keycode = KEY_RESIZE;
		return PSPG_NCURSES_EVENT;
	}

	/*
	 * Simply way, when we need only tty events and timeout is zero,
	 * This will be used after any ncurses event to get all buffered
	 * ncurses events before refreshing screen. 
	 */
	if (only_tty_events && zero_timeout)
	{
		if (get_ncurses_event(nced, &sigint))
			return PSPG_NCURSES_EVENT;
		else if (sigint)
		{
			if (only_tty_events)
			{
				handle_sigint = true;
				return PSPG_NOTHING_VALID_EVENT;
			}

			return PSPG_SIGINT_EVENT;
		}
		else
			return PSPG_NOTHING_VALID_EVENT;
	}


	fds[0].fd = fileno(f_tty);
	fds[0].events = POLLIN;
	nfds = 1;

	if (!only_tty_events)
	{
		if (current_state->stream_mode && !(f_data_opts & STREAM_IS_FILE))
		{
			fds[1].fd = fileno(f_data);
			fds[1].events = POLLIN;

#ifdef HAVE_INOTIFY

			poll_inotify_fd = false;

#endif

			nfds = 2;
		}

#ifdef HAVE_INOTIFY

		else if (f_data_opts & STREAM_HAS_NOTIFY_SUPPORT)
		{
			fds[1].fd = inotify_fd;
			fds[1].events = POLLIN;
			poll_inotify_fd = true;
			nfds = 2;
		}

#endif

	}

	while (timeout >= 0 || without_timeout)
	{
		int		poll_num;
		time_t	t1_sec, t2_sec;
		long	t1_ms, t2_ms;

		/*
		 * When timeout is 0, then we allow only one iteration, and
		 * returns PSPG_NOTHING_VALID_EVENT if there are not any valid
		 * event. When timeout is higher or there is not timeout, then
		 * we can wait to valid event or to timeout.
		 */
		if (!first_loop)
		{
			if (zero_timeout)
				return PSPG_NOTHING_VALID_EVENT;
		}
		else
			first_loop = false;

		/*
		 * ESCAPE key is used (by ncurses applications) like switcher to alternative
		 * keyboard. The escape event is forced by 2x press of ESCAPE key. The ESCAPE
		 * key signalize start of seqence. The length of this sequnce is limmited by
		 * timeout PSPG_ESC_DELAY (default 2000). So when ESCAPE is pressed, we have
		 * repeat reading to get second key of key's sequance.
		 *
		 */
repeat_reading:

		/*
		 * take time of waitin inside poll function.
		 */
		if (!without_timeout && !zero_timeout)
			current_time(&t1_sec, &t1_ms);

		poll_num = poll(fds, nfds, without_timeout ? -1 : timeout);

		if (!without_timeout && !zero_timeout)
		{
			current_time(&t2_sec, &t2_ms);
			timeout -= (int) (time_diff(t2_sec, t2_ms, t1_sec, t1_ms));
		}

		if (poll_num == -1)
		{
			/* pool error is expected after sigint */
			if (handle_sigint)
			{
				if (!only_tty_events)
				{
					handle_sigint = false;
					return PSPG_SIGINT_EVENT;
				}
				else
					continue;
			}
			else if (handle_sigwinch)
			{
				handle_sigwinch = false;
				nced->alt = false;
				nced->keycode = KEY_RESIZE;
				return PSPG_NCURSES_EVENT;
			}

			log_row("poll error (%s)", strerror(errno));
		}
		else if (poll_num > 0)
		{
			if (fds[0].revents)
			{
				if (get_ncurses_event(nced, &sigint))
				{
					if (nced->alt && nced->keycode == PSPG_NOTASSIGNED_CODE && first_event && timeout != 0)
					{
						first_event = false;

						/*
						 * own implementation of escape delay. For fast escape - press
						 * 2x escape.
						 */
						timeout = PSPG_ESC_DELAY;
						goto repeat_reading;
					}

					if (!first_event)
					{
						/* double escpae */
						if (nced->alt && nced->keycode == PSPG_NOTASSIGNED_CODE)
							nced->keycode = PSPG_ESC_CODE;
						else if (nced->keycode != KEY_MOUSE)
							nced->alt = true;
					}

					return PSPG_NCURSES_EVENT;
				}

				if (sigint)
				{
					if (!only_tty_events)
						return PSPG_SIGINT_EVENT;
				}
			}
			else if (fds[1].revents)
			{
				short revents = fds[1].revents;

				if (revents & POLLHUP)
				{
					/* The pipe cannot be reopened */
					if (f_data_opts & STREAM_IS_PIPE)
					{
						log_row("detected POLLHUP on pipe");
						return PSPG_FATAL_EVENT;
					}

					log_row("force close stream after POLLHUP");
					close_data_stream();

					/* we don't want to reopen stream too quickly, sleep 100ms */
					usleep(1000 * 100);
					return PSPG_READ_DATA_EVENT;
				}
				else if (revents & POLLIN)
				{

#ifdef HAVE_INOTIFY

					if (poll_inotify_fd)
					{
						ssize_t		len;
						char		buff[640];
						bool		stream_closed = false;

						/* there are a events on monitored file */
						len = read(inotify_fd, buff, sizeof(buff));

						/*
						 * read to end, it is notblocking IO, only one event and
						 * one file is monitored
						 */
						while (len > 0)
						{
							const struct inotify_event *ino_event = (struct inotify_event *) buff;

							while (len > 0)
							{
								if ((ino_event->mask & IN_CLOSE_WRITE))
									stream_closed = true;

								len -= sizeof (struct inotify_event) + ino_event->len;
								ino_event += sizeof (struct inotify_event) + ino_event->len;
							}

							len = read(inotify_fd, buff, sizeof(buff));
						}

						if (stream_closed)
						{
							log_row("detected CLOSE WRITE by inotify");
							close_data_stream();
						}

						/*
						 * wait 200ms - sometimes inotify is too fast, and the content
						 * of is not ready for pspg and we get inotify event too prematurely.
						 * Use longer waiting in streaming mode, because detected event is MODIFY
						 */
						usleep(1000 * (stream_closed ? 100 : 250));
					}

#endif

					return PSPG_READ_DATA_EVENT;
				}
			}
		}
		else
		{
			/* timeout */
			if (!first_event)
			{
				nced->alt = false;
				nced->keycode = PSPG_ESC_CODE;
				return PSPG_NCURSES_EVENT;
			}
		}
	}

	return PSPG_TIMEOUT_EVENT;
}

#ifdef DEBUG_PIPE

int
get_pspg_event(NCursesEventData *nced,
			   bool only_tty_events,
			   int timeout)
{
	static int eventno = 0;
	const char *event_name;
	int		result;

	fprintf(debug_pipe, "*** waiting on event no: %d (%stimeout: %d) ***\n",
			++eventno, only_tty_events ? "only tty, " : "", timeout);

	fflush(debug_pipe);

	result = _get_pspg_event(nced, only_tty_events, timeout);

	switch (result)
	{
		case PSPG_NCURSES_EVENT:
			event_name = "NCURSES";
			break;
		case PSPG_READ_DATA_EVENT:
			event_name = "READ DATA";
			break;
		case PSPG_TIMEOUT_EVENT:
			event_name = "TIMEOUT";
			break;
		case PSPG_SIGINT_EVENT:
			event_name = "SIGINT";
			break;
		case PSPG_FATAL_EVENT:
			event_name = "FATAL";
			break;
		case PSPG_ERROR_EVENT:
			event_name = "ERROR";
			break;
		case PSPG_NOTHING_VALID_EVENT:
			event_name = "NOTHING VALID EVENT";
			break;
		default:
			event_name = "undefined event";
	}

	fprintf(debug_pipe, "*** event no: %d = %s ***\n", eventno, event_name);
	if (result == PSPG_NCURSES_EVENT)
	{
		char	buffer[20];

		if (nced->keycode == KEY_MOUSE)
			sprintf(buffer, ", bstate: %08lx", (unsigned long) nced->mevent.bstate);
		else
			buffer[0] = '\0';

		fprintf(debug_pipe, "*** ncurses event %s%s%s (%d) ***\n",
			  nced->alt ? "Alt " : "",
			  keyname(nced->keycode),
			  buffer,
			  nced->keycode);
	}

	fflush(debug_pipe);

	return result;
}

#else

int
get_pspg_event(NCursesEventData *nced,
			   bool only_tty_events,
			   int timeout)
{
	return _get_pspg_event(nced, only_tty_events, timeout);
}

#endif

void
unget_pspg_event(NCursesEventData *nced)
{
	if (saved_event_is_valid)
		log_row("attention - saved ncurses event is overwritten");

	memcpy(&saved_event, nced, sizeof(NCursesEventData));
	saved_event_is_valid = true;
}

/*************************************
 * Prepare an access to input streams
 *
 *************************************
 */

bool
open_data_stream(Options *opts)
{
	current_state->_errno = 0;
	current_state->errstr = NULL;

	if (opts->pathname)
	{
		char	   *locpathname = tilde(pathname, opts->pathname);

		errno = 0;

		/*
		 * fopen can be blocking operation on FIFO. It is known limit. Theoretically
		 * it can be fixed by using open fce instead fopen and setting RW and NONBLOCK
		 * in open time. But it doesn't look like robust solution.
		 */
		f_data = fopen(locpathname, "r");
		if (!f_data)
		{
			/* save errno, and prepare error message */
			current_state->_errno = errno;
			format_error("cannot to open file \"%s\" (%s)", pathname, strerror(errno));
			return false;
		}

		close_f_data = true;
	}
	else
	{
		/* there is not a path name */
		pathname[0] = '\0';

		/* use stdin as input if query cannot be used as source */
		if (!opts->query)
		{
			f_data = stdin;
			f_data_opts = STREAM_IS_PIPE;
		}
	}

	if (f_data)
	{
		struct stat statbuf;

		if (fstat(fileno(f_data), &statbuf) != 0)
		{
			current_state->_errno = errno;
			format_error("cannot to get status of file \"%s\" (%s)", pathname, strerror(errno));
			return false;
		}

		f_data_opts |= S_ISREG(statbuf.st_mode) ? STREAM_IS_FILE : 0;
		f_data_opts |= S_ISFIFO(statbuf.st_mode) ? STREAM_IS_FIFO : 0;

		/*
		 * FIFO doesn't work well in non stream mode, it's more pipe, than file.
		 * So when we know, so input is FIFO, we force stream mode.
		 */
		if ((f_data_opts & STREAM_IS_FIFO) && !(f_data_opts & STREAM_IS_PIPE))
		{
			log_row("force stream mode because input is FIFO");
			current_state->stream_mode = true;
		}

		if (current_state->stream_mode)
		{
			/* ensure non blocking read from pipe or fifo */
			if (f_data_opts & STREAM_IS_FILE)
			{

#ifndef HAVE_INOTIFY

				leave("streaming on file is not available without file notification service");

#endif

				fseek(f_data, 0L, SEEK_END);
				last_data_pos = ftell(f_data);
			}
			else
			{
				/* in stream mode we use non block reading for FIFO or pipes */
				fcntl(fileno(f_data), F_SETFL, O_NONBLOCK);
			}
		}

		f_data_opts |= (fcntl(fileno(f_data), F_GETFL) & O_NONBLOCK) ? STREAM_IS_IN_NONBLOCKING_MODE : 0;
	}

	if (current_state->stream_mode && f_data && (f_data_opts & STREAM_IS_FIFO))
		f_data_fileno = fileno(f_data);
	else
		f_data_fileno = -1;

	if (!(f_data_opts & STREAM_IS_PIPE))
		f_data_opts |= STREAM_CAN_BE_REOPENED;

	if (f_data && !ferror(f_data) &&
		(f_data_opts & STREAM_IS_FILE) &&
		(opts->watch_file || current_state->stream_mode))
	{

#ifdef HAVE_INOTIFY

		if (inotify_fd == -1)
		{
			inotify_fd = inotify_init1(IN_NONBLOCK);
			if (inotify_fd == -1)
				leave("cannot initialize inotify (%s)", strerror(errno));
		}

		if (inotify_wd == -1)
		{
			inotify_wd = inotify_add_watch(inotify_fd,
												 pathname,
												 IN_CLOSE_WRITE |
												 (current_state->stream_mode ? IN_MODIFY : 0));


			if (inotify_wd == -1)
				leave("cannot watch file \"%s\" (%s)", pathname, strerror(errno));
		}

		f_data_opts |= STREAM_HAS_NOTIFY_SUPPORT;

#else

		leave("missing inotify support");

#endif

	}


	return true;
}

void
close_data_stream(void)
{
	if (close_f_data)
	{
		fclose(f_data);

		f_data = NULL;
		f_data_opts = 0;

		close_f_data = false;
	}
}

bool
open_tty_stream(void)
{

#ifndef __APPLE__

	f_tty = fopen("/dev/tty", "r+");

#endif

	if (!f_tty)
	{
		f_tty = fopen(ttyname(fileno(stdout)), "r");
		if (!f_tty)
		{
			if (isatty(fileno(stderr)))
				f_tty = stderr;
		}
		else
			close_f_tty = true;
	}
	else
		close_f_tty = true;

	return f_tty != NULL;
}

/*
 * ending pspg
 */
void
close_tty_stream(void)
{
	if (close_f_tty)
		fclose(f_tty);

	f_tty = NULL;
	close_f_tty = false;

#ifdef HAVE_INOTIFY

	if (inotify_wd >= 0)
	{
		inotify_rm_watch(inotify_fd, inotify_wd);
		inotify_wd = -1;
	}

	if (inotify_fd >= 0)
	{
		close(inotify_fd);
		inotify_fd = -1;
	}

#endif

}

/*************************************
 * File truncation detection
 *
 *************************************
 */

void
detect_file_truncation(void)
{
	/* detection truncating */
	if (last_data_pos != -1)
	{
		struct stat stats;

		if (fstat(fileno(f_data), &stats) == 0)
		{
			if (stats.st_size < last_data_pos)
			{
				log_row("file \"%s\" was truncated", pathname);

				/* read from start of file */
				fseek(f_data, 0L, SEEK_SET);
			}
		}
		else
			log_row("cannot to stat file: %s (%s)", pathname, strerror(errno));
	}
}

void
save_file_position(void)
{
	if (current_state->stream_mode && f_data_opts & STREAM_IS_FILE)
		last_data_pos = ftell(f_data);
}

const char *
get_input_file_basename(void)
{
	if (pathname[0])
	{
		return  basename(pathname);
	}

	return NULL;
}

/*************************************
 * Utility
 *
 *************************************
 */

/*
 * Disable echo and buffering on terminal, read one char,
 * and returns original settings.
 */
int
wait_on_press_any_key(void)
{
	struct termios current;
	struct termios orig_termios;
	int		result;

	tcgetattr(fileno(f_tty), &orig_termios);
	tcgetattr(fileno(f_tty), &current);

	current.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(fileno(f_tty), TCSAFLUSH, &current);

	result = fgetc(f_tty);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);

	return result;
}

#ifdef HAVE_INOTIFY

void
clean_inotify_poll(void)
{
	char buff[64];

	if (inotify_fd >= 0)
	{
		while (read(inotify_fd, buff, sizeof(buff)) >= 0) {};
	}
}

#endif
