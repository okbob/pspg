/*-------------------------------------------------------------------------
 *
 * file.c
 *	  a routines related to file processing
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/file.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pspg.h"

/*
 * Replace tilde by HOME dir
 */
char *
tilde(char *dest, char *path)
{
	static char buffer[MAXPATHLEN];

	int			chars = 0;
	char	   *w;

	if (!dest)
		dest = buffer;

	w = dest;

	while (*path && chars < MAXPATHLEN - 1)
	{
		if (*path == '~')
		{
			char *home = getenv("HOME");

			if (home == NULL)
				leave("HOME directory is not defined");

			while (*home && chars < MAXPATHLEN - 1)
			{
				*w++ = *home++;
				chars += 1;
			}
			path++;
		}
		else
		{
			*w++ = *path++;
			chars += 1;
		}
	}

	*w = '\0';

	return dest;
}


/*
 * Try to open input stream.
 */
bool
open_data_file(Options *opts, StateData *state)
{
	state->_errno = 0;
	state->errstr = NULL;

	if (opts->pathname)
	{
		char	   *pathname = tilde(state->pathname, opts->pathname);

		errno = 0;

		/*
		 * fopen can be blocking operation on FIFO. It is known limit. Theoretically
		 * it can be fixed by using open fce instead fopen and setting RW and NONBLOCK
		 * in open time. But it doesn't look like robust solution.
		 */
		state->fp = fopen(pathname, "r");
		if (!state->fp)
		{
			/* save errno, and prepare error message */
			state->_errno = errno;
			format_error("cannot to open file \"%s\" (%s)", pathname, strerror(errno));
			return false;
		}
	}
	else
	{
		/* there is not a path name */
		state->pathname[0] = '\0';

		/* use stdin as input if query cannot be used as source */
		if (!opts->query)
		{
			state->fp = stdin;
			state->is_pipe = true;
		}
	}

	if (state->fp)
	{
		struct stat statbuf;

		if (fstat(fileno(state->fp), &statbuf) != 0)
		{
			state->_errno = errno;
			format_error("cannot to get status of file \"%s\" (%s)", state->pathname, strerror(errno));
			return false;
		}

		state->is_fifo = S_ISFIFO(statbuf.st_mode);		/* is FIFO file or pipe */
		state->is_file = S_ISREG(statbuf.st_mode);		/* is regular file */

		/*
		 * FIFO doesn't work well in non stream mode, it's more pipe, than file.
		 * So when we know, so input is FIFO, we force stream mode.
		 */
		if (state->is_fifo && !state->is_pipe)
		{
			log_row("force stream mode because input is FIFO");
			state->stream_mode = true;
		}

		/*
		 * when source is FIFO and not pipe, then we can protect source
		 * against POLLHUP sugnal. One possibility how to do it is reopening
		 * stream with write acess. Then POLLHUP signal is never raised.
		 */
		if (state->is_fifo && !state->is_pipe && state->hold_stream == 2)
		{
			state->fp = freopen(NULL, "a+", state->fp);
			if (!state->fp)
				leave("cannot to reopen file \"%s\" to hold stream (%s)", state->pathname, strerror(errno));
		}

		if (state->stream_mode)
		{
			/* ensure non blocking read from pipe or fifo */
			if (state->is_file)
			{
				if (!state->has_notify_support)
					leave("streaming on file is not available without file notification service");

				state->detect_truncation = true;
				fseek(state->fp, 0L, SEEK_END);
				state->last_position = ftell(state->fp);
			}
			else
			{
				/* in stream mode we use non block reading for FIFO or pipes */
				fcntl(fileno(state->fp), F_SETFL, O_NONBLOCK);
			}
		}

		state->is_blocking = !(fcntl(fileno(state->fp), F_GETFL) & O_NONBLOCK);
	}

	if (state->stream_mode && state->is_fifo)
		state->fds[1].fd = fileno(state->fp);

	return true;
}
