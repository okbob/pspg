/*-------------------------------------------------------------------------
 *
 * events.c
 *	  handles all events - tty and content
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/inputs.c
 *
 *-------------------------------------------------------------------------
 */
#define PDC_NCMOUSE

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#if defined(HAVE_INOTIFY)

#include <sys/inotify.h>

#elif defined(HAVE_KQUEUE)

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#endif

#include "inputs.h"
#include "pspg.h"

#define PSPG_NOTASSIGNED_CODE					0

static char	pathname[MAXPATHLEN] = "";

FILE   *f_tty = NULL;						/* ncurses input stream */
FILE   *f_data = NULL;						/* content input */

unsigned int f_data_opts = 0;						/* describe properties of content input */

int		f_data_fileno = -1;					/* content input used by poll */

static struct pollfd fds[2];

static long last_data_pos = -1;
static int open_data_stream_prev_errno = 0;

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

static int notify_fd = -1;

#endif

#if defined(HAVE_INOTIFY)

static int inotify_wd = -1;

#endif

static NCursesEventData saved_event;
static bool saved_event_is_valid = false;

static bool close_f_tty = false;

#ifdef PDCURSES

static bool is_button1_pressed = false;

#endif

int		pspg_esc_delay;


/*************************************
 * Events processing
 *
 *************************************
 */

#ifdef  PDCURSES

static bool
is_alt(int *keycode)
{
	switch (*keycode)
	{
		case ALT_0:
			*keycode = '0';
			return true;
		case ALT_1:
			*keycode = '1';
			return true;
		case ALT_2:
			*keycode = '2';
			return true;
		case ALT_3:
			*keycode = '3';
			return true;
		case ALT_4:
			*keycode = '4';
			return true;
		case ALT_5:
			*keycode = '5';
			return true;
		case ALT_6:
			*keycode = '6';
			return true;
		case ALT_7:
			*keycode = '7';
			return true;
		case ALT_8:
			*keycode = '8';
			return true;
		case ALT_9:
			*keycode = '9';
			return true;
		case ALT_A:
			*keycode = 'a';
			return true;
		case ALT_B:
			*keycode = 'b';
			return true;
		case ALT_C:
			*keycode = 'c';
			return true;
		case ALT_D:
			*keycode = 'd';
			return true;
		case ALT_E:
			*keycode = 'e';
			return true;
		case ALT_F:
			*keycode = 'f';
			return true;
		case ALT_G:
			*keycode = 'g';
			return true;
		case ALT_H:
			*keycode = 'h';
			return true;
		case ALT_I:
			*keycode = 'i';
			return true;
		case ALT_J:
			*keycode = 'j';
			return true;
		case ALT_K:
			*keycode = 'k';
			return true;
		case ALT_L:
			*keycode = 'l';
			return true;
		case ALT_M:
			*keycode = 'm';
			return true;
		case ALT_N:
			*keycode = 'n';
			return true;
		case ALT_O:
			*keycode = 'o';
			return true;
		case ALT_P:
			*keycode = 'p';
			return true;
		case ALT_Q:
			*keycode = 'q';
			return true;
		case ALT_R:
			*keycode = 'r';
			return true;
		case ALT_S:
			*keycode = 's';
			return true;
		case ALT_T:
			*keycode = 't';
			return true;
		case ALT_U:
			*keycode = 'u';
			return true;
		case ALT_V:
			*keycode = 'v';
			return true;
		case ALT_W:
			*keycode = 'w';
			return true;
		case ALT_X:
			*keycode = 'x';
			return true;
		case ALT_Y:
			*keycode = 'y';
			return true;
		case ALT_Z:
			*keycode = 'x';
			return true;

		default:
			return false;
	}
}

#endif

/*
 * Read one ncurses event
 */
static bool
get_ncurses_event(NCursesEventData *nced, bool *sigint, bool *sigwinch)
{
	bool	first_event = true;
	int		ok = true;

#if NCURSES_WIDECHAR > 0 && defined HAVE_NCURSESW

	wint_t	ch;
	int		ret;

#endif

	*sigint = false;
	*sigwinch = false;

	nced->keycode = PSPG_NOTASSIGNED_CODE;
	nced->ignore_it = false;
	nced->alt = false;

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

#ifdef DEBUG_PIPE

	fprintf(debug_pipe, "*** keycode: %d, err: %d\n", nced->keycode, errno);

#endif

	if (errno == EINTR)
	{
		if (handle_sigint)
		{
			*sigint = true;
			handle_sigint = false;
		}

		if (handle_sigwinch)
		{
			*sigwinch = true;
			handle_sigwinch = false;
		}

		return false;
	}

	if (errno == 0)
	{
		if (nced->keycode == ERR)
		{
				nced->keycode = PSPG_NOTASSIGNED_CODE;
				nced->alt = false;
				nced->ignore_it = true;
				return true;
		}

		if (nced->keycode == KEY_MOUSE)
		{
			ok = getmouse(&nced->mevent) == OK;

#ifdef PDCURSES

			/*
			 * event filter - we want to report mouse move only when
			 * BUTTON1 is pressed (note: we have not xterm mouse mode 1002
			 * there, because terminalinfo is just fake API there.
			 */
			if (nced->mevent.bstate & BUTTON1_PRESSED)
			{
				is_button1_pressed = true;
			}
			else if (nced->mevent.bstate & BUTTON1_RELEASED)
			{
				is_button1_pressed = false;
			}

			if (nced->mevent.bstate & REPORT_MOUSE_POSITION)
			{
				if (is_button1_pressed)
				{
						return true;
				}

				nced->keycode = PSPG_NOTASSIGNED_CODE;
				nced->ignore_it = true;
				return true;
			}

#endif

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

	/*
	 * Workaround for issue #204. MacOS returns ERR when there are not
	 * any other activity after ESC in ESCDELAY limit.
	 */
	if (nced->keycode == ERR && !first_event && errno == 0)
		nced->keycode = PSPG_NOTASSIGNED_CODE;

#if PDCURSES

	nced->alt = is_alt(&nced->keycode);

#else

	nced->alt = !first_event;

#endif

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
	bool	sigwinch;
	bool	first_event = true;
	bool	first_loop = true;
	bool	without_timeout = timeout == -1;
	bool	zero_timeout = timeout == 0;

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

	bool	poll_notify_fd = false;

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
		return PSPG_SIGWINCH_EVENT;
	}

	/*
	 * Simply way, when we need only tty events and timeout is zero,
	 * This will be used after any ncurses event to get all buffered
	 * ncurses events before refreshing screen. 
	 */
	if (only_tty_events && zero_timeout)
	{
		if (get_ncurses_event(nced, &sigint, &sigwinch))
			return PSPG_NCURSES_EVENT;
		else if (sigint)
		{
			handle_sigint = true;
			return PSPG_NOTHING_VALID_EVENT;
		}
		else if (sigwinch)
		{
			return PSPG_SIGWINCH_EVENT;
		}
		else
			return PSPG_NOTHING_VALID_EVENT;
	}

	fds[0].fd = fileno(f_tty);
	fds[0].events = POLLIN;
	nfds = 1;

	if (!only_tty_events)
	{
		if (current_state->stream_mode &&
			(f_data_opts & STREAM_IS_OPEN) &&
			!(f_data_opts & STREAM_IS_FILE) &&
			!(f_data_opts & STREAM_IS_CLOSED))
		{
			fds[1].fd = fileno(f_data);
			fds[1].events = POLLIN;

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

			poll_notify_fd = false;

#endif

			nfds = 2;
		}

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

		else if ((f_data_opts & STREAM_HAS_NOTIFY_SUPPORT) &&
				 (notify_fd != -1))
		{
			fds[1].fd = notify_fd;
			fds[1].events = POLLIN;
			poll_notify_fd = true;
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
				return PSPG_SIGWINCH_EVENT;
			}

#ifdef PDCURSES

			/*
			 * I didn't find how to resize term in pdcurses without crash.
			 * So instead to call resize_term from custom space, use pdcurses
			 * sigwinch handler, and force pdcurses to handle resizing after
			 * terminal is resized.
			 */
			else if (is_termresized())
			{
				goto ncurses_get;
			}

#endif

			log_row("poll error (%s) %d", strerror(errno));
		}
		else if (poll_num > 0)
		{
			if (fds[0].revents)
			{

#ifdef PDCURSES

ncurses_get:

#endif

				if (get_ncurses_event(nced, &sigint, &sigwinch))
				{
					if (nced->alt && nced->keycode == PSPG_NOTASSIGNED_CODE && first_event && timeout != 0)
					{
						first_event = false;

						if (pspg_esc_delay != 0)
						{
							if (pspg_esc_delay > 0)
							{
								timeout = pspg_esc_delay;
								without_timeout = false;
							}
							else
							{
								timeout = -1;
								without_timeout = true;
							}

							goto repeat_reading;
						}
						else
						{
							/* esc delay is not wanted */
							nced->keycode = PSPG_ESC_CODE;
							return PSPG_NCURSES_EVENT;
						}
					}

					if (!first_event)
					{
						/* double escape */
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
				else if (sigwinch)
				{
					return PSPG_SIGWINCH_EVENT;
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
						f_data_opts |= STREAM_IS_CLOSED;
						log_row("detected POLLHUP on pipe");
						return PSPG_NOTHING_VALID_EVENT;
					}

					log_row("force close stream after POLLHUP");
					close_data_stream();

					/* we don't want to reopen stream too quickly, sleep 100ms */
					usleep(1000 * 100);
					return PSPG_READ_DATA_EVENT;
				}
				else if (revents & POLLIN)
				{

#if defined(HAVE_INOTIFY)

					if (poll_notify_fd)
					{
						ssize_t		len;
						char		buff[640];
						bool		stream_closed = false;

						/* there are a events on monitored file */
						len = read(notify_fd, buff, sizeof(buff));

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

							len = read(notify_fd, buff, sizeof(buff));
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

#elif defined(HAVE_KQUEUE)

					if (poll_notify_fd)
					{
						struct kevent kqev;
						struct timespec tmout = {0, 0};
						bool		stream_closed = false;
						int			rc;

						rc = kevent(notify_fd, NULL, 0, &kqev, 1, &tmout);
						while (rc == 1)
						{
							if (kqev.flags & EV_ERROR)
								log_row("kqueue EV_ERROR (%s)", strerror(kqev.data));

#if defined(NOTE_CLOSE_WRITE)

							else if (kqev.flags & NOTE_CLOSE_WRITE)
								stream_closed = true;
#endif

							rc = kevent(notify_fd, NULL, 0, &kqev, 1, &tmout);
						}

						if (rc == -1)
							log_row("kqueue error (%s)", strerror(errno));

						if (stream_closed)
						{
							log_row("detected CLOSE WRITE by kqueue");
							close_data_stream();
						}

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
		case PSPG_SIGWINCH_EVENT:
			event_name = "SIGWINCH";
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
		if (!nced->ignore_it)
		{
			char		buffer[20];

			if (nced->keycode == KEY_MOUSE)
				sprintf(buffer, ", bstate: %08lx", (unsigned long) nced->mevent.bstate);
			else
				buffer[0] = '\0';

			fprintf(debug_pipe, "*** ncurses event %s%s%s (%d) ***\n",
				  nced->alt ? "Alt " : "",
				  nced->keycode ? keyname(nced->keycode) : "0",
				  buffer,
				  nced->keycode);
		}
		else
			fprintf(debug_pipe, "*** ignored ncurses event ****\n");
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
	struct stat statbuf;
	const char *mode = "r";

	current_state->_errno = 0;
	current_state->errstr = NULL;

	if (opts->pathname)
	{
		char	   *locpathname = tilde(pathname, opts->pathname);

		errno = 0;

		/*
		 * Try to detect if source is file or pipe
		 */
		if (stat(locpathname, &statbuf) == 0)
		{
			if (S_ISFIFO(statbuf.st_mode))
			{
				log_row("data source is FIFO");

				/*
				 * Without mode="rw+", fopen over FIFO is very blocking,
				 * if FIFO is not currently opened by some process for
				 * write. We can protect self by open FIFO directly with
				 * rw+ mode.
				 */
				mode = "rw+";
			}
		}

		/*
		 * fopen can be blocking operation on FIFO. It is known limit. Theoretically
		 * it can be fixed by using open fce instead fopen and setting RW and NONBLOCK
		 * in open time. But it doesn't look like robust solution.
		 */
		f_data = fopen(locpathname, mode);
		if (!f_data)
		{
			/* save errno, and prepare error message */
			current_state->_errno = errno;

			/*
			 * In watch mode, the file can be created in next cycle. In this case
			 * we don't want skip current content, when stream mode is active.
			 */
			open_data_stream_prev_errno = errno;
			format_error("cannot to open file \"%s\" (%s)", pathname, strerror(errno));
			return false;
		}

		f_data_opts = STREAM_IS_OPEN | STREAM_CAN_BE_CLOSED;
	}
	else
	{
		/* there is not a path name */
		pathname[0] = '\0';

		/* use stdin as input if query cannot be used as source */
		if (!opts->query && !opts->querystream)
		{
			f_data = stdin;
			f_data_opts = STREAM_IS_OPEN | STREAM_IS_PIPE;
		}
	}

	if (f_data)
	{
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

#if !defined(HAVE_INOTIFY) && !defined(HAVE_KQUEUE)

				leave("streaming on file is not available without file notification service");

#endif

				/*
				 * In stream mode skip current content if this file
				 * is opened first time.
				 */
				fseek(f_data, 0L,
					  open_data_stream_prev_errno == ENOENT ? SEEK_SET : SEEK_END);

				last_data_pos = ftell(f_data);
				open_data_stream_prev_errno = 0;
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

#if defined(HAVE_INOTIFY)

		if (notify_fd == -1)
		{
			notify_fd = inotify_init1(IN_NONBLOCK);
			if (notify_fd == -1)
				leave("cannot initialize inotify (%s)", strerror(errno));
		}

		if (inotify_wd == -1)
		{
			inotify_wd = inotify_add_watch(notify_fd,
												 pathname,
												 IN_CLOSE_WRITE |
												 (current_state->stream_mode ? IN_MODIFY : 0));


			if (inotify_wd == -1)
				leave("cannot watch file \"%s\" (%s)", pathname, strerror(errno));
		}

		f_data_opts |= STREAM_HAS_NOTIFY_SUPPORT;

#elif defined(HAVE_KQUEUE) && defined(NOTE_CLOSE_WRITE)

		if (notify_fd == -1)
		{
			static struct kevent event;
			int		rc;
			int		fd2;

			notify_fd = kqueue();
			if (notify_fd == -1)
				leave("cannot to initialize kqueue(%s)", strerror(errno));

			/*
			 * The data file can be closed after reading, but we want to watch
			 * file longer time, so use own file descriptor (when we don't use
			 * stream mode)
			 */
			fd2 = fileno(f_data);
			if (!current_state->stream_mode)
				fd2 = dup(fd2);

			EV_SET(&event,
				   fd2,
				   EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
				   NOTE_CLOSE_WRITE |
				   (current_state->stream_mode ? NOTE_WRITE : 0),
				   0, NULL);

			rc = kevent(notify_fd, &event, 1, NULL, 0, NULL);
			if (rc == -1)
				leave("cannot to register kqueue event (%s)", strerror(errno));

			if (event.flags & EV_ERROR)
				leave("cannot to register kqueue event (%s)", strerror(event.data));
		}

		f_data_opts |= STREAM_HAS_NOTIFY_SUPPORT;

#elif defined(HAVE_KQUEUE)

		/*
		 * NOTE_CLOSE_WRITE is available from FreeBSD 11
		 * On older BSD systems this event is not available.
		 */
		if (notify_fd == -1 && current_state->stream_mode)
		{
			static struct kevent event;
			int		rc;

			notify_fd = kqueue();
			if (notify_fd == -1)
				leave("cannot to initialize kqueue(%s)", strerror(errno));

			EV_SET(&event,
				   fileno(f_data),
				   EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
				   NOTE_WRITE,
				   0, NULL);

			rc = kevent(notify_fd, &event, 1, NULL, 0, NULL);
			if (rc == -1)
				leave("cannot to register kqueue event (%s)", strerror(errno));

			if (event.flags & EV_ERROR)
				leave("cannot to register kqueue event (%s)", strerror(event.data));
		}

		f_data_opts |= STREAM_HAS_NOTIFY_SUPPORT;

#else

		leave("missing inotify support");

#endif

	/*
	 * In streaming mode, when input stream is file, then is
	 * high risk of desynchronization. Until we will have some
	 * synchronization mark in protocol, we should not to
	 * allow progressive load.
	 */
	if (current_state->stream_mode &&
		(f_data_opts & STREAM_IS_FILE))
		opts->progressive_load_mode = false;
	}

	return true;
}

void
close_data_stream(void)
{
	if (f_data_opts & STREAM_CAN_BE_CLOSED & STREAM_IS_OPEN)
	{
		fclose(f_data);

		f_data = NULL;

#if defined(HAVE_INOTIFY) && defined(HAVE_KQUEUE)

		f_data_opts = f_data_opts & STREAM_HAS_NOTIFY_SUPPORT;

#else

		f_data_opts = 0;

#endif

		f_data_opts &= ~(STREAM_CAN_BE_CLOSED);
		f_data_opts &= ~(STREAM_IS_OPEN);
	}

	/*
	 * KQUEUE has joined notification file descriptor,
	 * so we should to invalidate notify_fd too.
	 */
#if defined(HAVE_KQUEUE) && defined(NOTE_CLOSE_WRITE)

	if (current_state->stream_mode && notify_fd != -1)
	{
		close(notify_fd);
		notify_fd = -1;
		f_data_opts = 0;
	}

#elif defined(HAVE_KQUEUE)

	if (notify_fd != -1)
	{
		close(notify_fd);
		notify_fd = -1;
		f_data_opts = 0;
	}

#endif

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

#if defined(HAVE_INOTIFY)

	if (inotify_wd >= 0)
	{
		inotify_rm_watch(notify_fd, inotify_wd);
		inotify_wd = -1;
	}

#endif

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

	if (notify_fd >= 0)
	{
		close(notify_fd);
		notify_fd = -1;
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

		/*
		 * This detection based on size is weak. It doesn't detect
		 * case when old content is overwritten by new content with
		 * same or bigger size.
		 */
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

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

void
clean_notify_poll(void)
{

#if defined(HAVE_INOTIFY)

	if (notify_fd >= 0)
	{
		char		buff[64];

		while (read(notify_fd, buff, sizeof(buff)) >= 0) {};
	}

#elif defined(HAVE_KQUEUE)


	if  (notify_fd >= 0)
	{
		struct kevent kqev;
		struct timespec tmout = {0, 0};
		int			rc;

		rc = kevent(notify_fd, NULL, 0, &kqev, 1, &tmout);
		while (rc == 1)
			rc = kevent(notify_fd, NULL, 0, &kqev, 1,  &tmout);
	}

#endif

}

#endif
