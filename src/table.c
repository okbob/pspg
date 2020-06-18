/*-------------------------------------------------------------------------
 *
 * table.c
 *	  a routines for parsing file with data in tabular form
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/table.c
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pspg.h"
#include "unicode.h"

#ifdef DEBUG_PIPE

#include <time.h>
#include <math.h>


static void
current_time(time_t *sec, long *ms)
{
	struct timespec spec;

	clock_gettime(CLOCK_MONOTONIC, &spec);
	*ms = roundl(spec.tv_nsec / 1.0e6);
	*sec = spec.tv_sec;
}

#define time_diff(s1, ms1, s2, ms2)		((s1 - s2) * 1000 + ms1 - ms2)

static void
print_duration(time_t start_sec, long start_ms, const char *label)
{
	time_t		end_sec;
	long		end_ms;

	current_time(&end_sec, &end_ms);

	fprintf(debug_pipe, "duration of \"%s\" is %ld ms\n",
			label,
			time_diff(end_sec, end_ms, start_sec, start_ms));
}

#endif

/*
 * Returns true when char is left upper corner
 */
static bool
isTopLeftChar(char *str)
{
	const char *u1 = "\342\224\214";
	const char *u2 = "\342\225\224";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * Returns true when char is top left header char
 */
static bool
isHeadLeftChar(char *str)
{
	const char *u1 = "\342\224\200";
	const char *u2 = "\342\225\220";
	const char *u3 = "\342\225\236";
	const char *u4 = "\342\224\234";
	const char *u5 = "\342\225\240";
	const char *u6 = "\342\225\237";

	/* ascii */
	if ((str[0] == '+' || str[0] == '-') && str[1] == '-')
		return true;

	/* pgcli fmt */
	if (str[0] == '|' && str[1] == '-')
		return true;

	/* expanded border 1 */
	if (str[0] == '-' && str[1] == '[')
		return true;

	/* csv double header */
	if ((str[0] == ':' || str[0] == '=') && str[1] == '=')
		return true;

	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;
	if (strncmp(str, u3, 3) == 0)
		return true;
	if (strncmp(str, u4, 3) == 0)
		return true;
	if (strncmp(str, u5, 3) == 0)
		return true;
	if (strncmp(str, u6, 3) == 0)
		return true;

	return false;
}

/*
 * Returns true when char is bottom left corner
 */
static bool
isBottomLeftChar(char *str)
{
	const char *u1 = "\342\224\224";
	const char *u2 = "\342\225\232";

	if (str[0] == '+')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

/*
 * detect different faces of headline in extended mode
 */
bool
is_expanded_header(Options *opts, char *str, int *ei_minx, int *ei_maxx)
{
	int		pos = 0;

	if (*str == '+')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\214", 3) == 0 || /* ┌ */
			 strncmp(str, "\342\225\224", 3) == 0 || /* ╔ */
			 strncmp(str, "\342\224\234", 3) == 0 || /* ├╟ */
			 strncmp(str, "\342\225\237", 3) == 0 ||
			 strncmp(str, "\342\225\236", 3) == 0 || /* ╞╠ */
			 strncmp(str, "\342\225\240", 3) == 0)
	{
		str += 3;
		pos += 1;
	}

	if (*str == '-')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\200", 3) == 0 || /* ─ */
			 strncmp(str, "\342\225\220", 3) == 0) /* ═ */
	{
		str += 3;
		pos += 1;
	}

	if (strncmp(str, "[ ", 2) != 0)
		return false;

	if (ei_minx != NULL && ei_maxx != NULL)
	{
		pos += 2;
		str += 2;
		*ei_minx = pos - 1;

		while (*str != ']' && *str != '\0')
		{
			pos += 1;
			str += opts->force8bit ? 1 : utf8charlen(*str);
		}

		*ei_maxx = pos - 1;
	}

	return true;
}

/*
 * Returns true when char (multibyte char) correspond with symbols for
 * line continuation.
 */
static bool
is_line_continuation_char(char *str, DataDesc *desc)
{
	const char *u1 = "\342\206\265";	/* ↵ */
	const char *u2 = "\342\200\246";	/* … */

	if (desc->linestyle == 'a')
	{
		return str[0] == '+' || str[0] == '.';
	}
	else
	{
		/* desc->linestyle == 'u'; */
		return strncmp(str, u1, 3) == 0 || strncmp(str, u2, 3) == 0;
	}
}

static bool
is_cmdtag(char *str)
{
	if (!str)
		return false;

	if (*str == '?' && strcmp(str, "???") == 0)
	{
			return true;
	}
	else if (*str == 'A')
	{
		if (strncmp(str, "ALTER ", 6) == 0 ||
				strcmp(str, "ANALYZE") == 0)
			return true;
	}
	else if (*str == 'B' && strcmp(str, "BEGIN") == 0)
	{
		return true;
	}
	else if (*str == 'C')
	{
		if (strcmp(str, "CALL") == 0 ||
				strcmp(str, "CHECKPOINT") == 0 ||
				strncmp(str, "CLOSE", 5) == 0 ||
				strcmp(str, "CLUSTER") == 0 ||
				strcmp(str, "COMMENT") == 0 ||
				strncmp(str, "COMMIT", 6) == 0 ||
				strncmp(str, "COPY ", 5) == 0 ||
				strncmp(str, "CREATE ", 7) == 0)
			return true;
	}
	else if (*str == 'D')
	{
		if (strncmp(str, "DEALLOCATE", 10) == 0 ||
				strncmp(str, "DECLARE ", 8) == 0 ||
				strncmp(str, "DELETE ", 7) == 0 ||
				strncmp(str, "DISCARD", 7) == 0 ||
				strcmp(str, "DO") == 0 ||
				strncmp(str, "DROP ", 5) == 0)
			return true;
	}
	else if (*str == 'E')
	{
		if (strcmp(str, "EXECUTE") == 0 ||
				strcmp(str, "EXPLAIN") == 0)
			return true;
	}
	else if (*str == 'F' && strncmp(str, "FETCH ", 6) == 0)
	{
		return true;
	}
	else if (*str == 'G' && strncmp(str, "GRANT", 5) == 0)
	{
		return true;
	}
	else if (*str == 'I')
	{
		if (strncmp(str, "IMPORT ", 7) == 0 ||
				strncmp(str, "INSERT ", 7) == 0)
			return true;
	}
	else if (*str == 'L')
	{
		if (strcmp(str, "LISTEN") == 0 ||
				strcmp(str, "LOAD") == 0 ||
				strcmp(str, "LOCK TABLE") == 0)
			return true;
	}
	else if (*str == 'M' && strncmp(str, "MOVE ", 5) == 0)
	{
		return true;
	}
	else if (*str == 'N' && strcmp(str, "NOTIFY") == 0)
	{
		return true;
	}
	else if (*str == 'P' && strncmp(str, "PREPARE", 7) == 0)
	{
		return true;
	}
	else if (*str == 'R')
	{
		if (strcmp(str, "REASSIGN OWNED") == 0 ||
				strcmp(str, "REFRESH MATERIALIZED VIEW") == 0 ||
				strcmp(str, "REINDEX") == 0 ||
				strcmp(str, "RELEASE") == 0 ||
				strcmp(str, "RESET") == 0 ||
				strncmp(str, "REVOKE", 6) == 0 ||
				strncmp(str, "ROLLBACK", 8) == 0)
			return true;
	}
	else if (*str == 'S')
	{
		if (strcmp(str, "SAVEPOINT") == 0 ||
				strcmp(str, "SECURITY LABEL") == 0 ||
				strncmp(str, "SELECT ", 7) == 0 ||
				strncmp(str, "SET", 3) == 0 ||
				strcmp(str, "SHOW") == 0 ||
				strcmp(str, "START TRANSACTION") == 0)
			return true;
	}
	else if (*str == 'T' && strcmp(str, "TRUNCATE TABLE") == 0)
	{
		return true;
	}
	else if (*str == 'U')
	{
		if (strcmp(str, "UNLISTEN") == 0 ||
				strncmp(str, "UPDATE ", 7) == 0)
			return true;
	}
	else if (*str == 'V' && strcmp(str, "VACUUM") == 0)
	{
		return true;
	}

	return false;
}

#define STATBUF_SIZE		(10 * 1024)

static size_t
_getline(char **lineptr, size_t *n, FILE *fp, bool is_blocking, bool wait_on_data)
{
	int			_errno;
	ssize_t		result;

	if (is_blocking)
	{
		result = getline(lineptr, n, fp);
		_errno = errno;

		if (result < 0)
		{
			free(*lineptr);
			*lineptr = NULL;

			errno = _errno;
		}

		return result;
	}

	if (!feof(fp) && !ferror(fp))
	{
		char   *dynbuf = NULL;
		char	statbuf[STATBUF_SIZE];
		int		fetched_chars = 0;
		int		rc;

		for (;;)
		{
			char   *str;
			int	len = 0;

			errno = 0;
			str = fgets(statbuf, STATBUF_SIZE, fp);
			_errno = errno;

			if (str)
			{
				bool	endline;

				len = strlen(str);
				endline = str[len - 1] == '\n';

				if (dynbuf)
				{
					dynbuf = realloc(dynbuf, fetched_chars + len + 1);
					if (!dynbuf)
						return -1;
					memcpy(dynbuf + fetched_chars, statbuf, len + 1);
					fetched_chars += len;
				}

				if (endline)
				{
endline_exit:
					if (dynbuf)
					{
						*lineptr = dynbuf;
						*n = fetched_chars + 1;

						return fetched_chars;
					}
					else
					{
						*lineptr = sstrdup(statbuf);
						*n = *lineptr ? len + 1 : 0;

						return *lineptr ? len : -1;
					}
				}

				if (!dynbuf)
				{
					dynbuf = sstrdup(statbuf);
					if (!dynbuf)
						return -1;
					fetched_chars += len;
				}

				errno = _errno;
			}

			if (errno)
			{
				if (feof(fp))
				{
					goto endline_exit;
				}
				else if (errno == EAGAIN)
				{
					struct pollfd fds[1];

					if (fetched_chars == 0 && !wait_on_data)
						return -1;

					fds[0].fd = fileno(fp);
					fds[0].events = POLLIN;

					rc = poll(fds, 1, -1);
					if (rc == -1)
					{
						log_row("poll error (%s)",  strerror(errno));

						usleep(1000);
					}

					clearerr(fp);
					continue;
				}
				else
				{
					free(dynbuf);
					return -1;
				}
			}
		}
	}

	return -1;
}

/*
 * Copy trimmed string
 */
static void
strncpytrim(Options *opts, char *dest, const char *src,
			size_t ndest, size_t nsrc)
{
	const char *endptr;

	endptr = src + nsrc - 1;

	/* skip trailing spaces */
	while (*src == ' ')
	{
		if (nsrc-- <= 0)
			break;
		src++;
	}

	/* skip ending spaces */
	while (*endptr == ' ')
	{
		if (nsrc-- <= 0)
			break;
		endptr--;
	}

	while(nsrc > 0)
	{
		size_t	clen;

		if (*src == '\0')
			break;

		clen = (size_t) opts->force8bit ? 1 : utf8charlen(*src);
		if (clen <= ndest && clen <= nsrc)
		{
			size_t		i;

			for (i = 0; i < clen; i++)
			{
				*dest++ = *src++;
				ndest--;
				nsrc--;
			}
		}
		else
			break;
	}

	*dest = '\0';
}


/*
 * Read data from file and fill DataDesc.
 */
bool
readfile(Options *opts, DataDesc *desc, StateData *state)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	LineBuffer *rows;

#ifdef DEBUG_PIPE

	time_t		start_sec;
	long		start_ms;

	fprintf(debug_pipe, "readfile start\n");
	current_time(&start_sec, &start_ms);

#endif

	desc->title[0] = '\0';
	desc->title_rows = 0;
	desc->border_top_row = -1;
	desc->border_head_row = -1;
	desc->border_bottom_row = -1;
	desc->first_data_row = -1;
	desc->last_data_row = -1;
	desc->is_expanded_mode = false;
	desc->headline_transl = NULL;
	desc->cranges = NULL;
	desc->columns = 0;
	desc->footer_row = -1;
	desc->alt_footer_row = -1;
	desc->is_pgcli_fmt = false;
	desc->namesline = NULL;
	desc->order_map = NULL;
	desc->total_rows = 0;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	rows = &desc->rows;
	desc->rows.prev = NULL;
	desc->oid_name_table = false;
	desc->multilines_already_tested = false;

	/* safe reset */
	desc->filename[0] = '\0';
	state->errstr = NULL;
	state->_errno = 0;

	if (opts->pathname != NULL)
	{
		char	   *name;

		name = basename(opts->pathname);
		strncpy(desc->filename, name, 64);
		desc->filename[64] = '\0';
	}

	clearerr(state->fp);

	/* detection truncating */
	if (state->detect_truncation)
	{
		struct stat stats;

		if (fstat(fileno(state->fp), &stats) == 0)
		{
			if (stats.st_size < state->last_position)
			{
				log_row("file \"%s\" was truncated", opts->pathname);
				fseek(state->fp,0L, SEEK_SET);
			}
		}
		else
			log_row("cannot to stat file: %s (%s)", opts->pathname, strerror(errno));
	}

	errno = 0;
	read = _getline(&line, &len, state->fp, state->is_blocking, false);
	if (read == -1)
		return false;

	do
	{
		int		clen;

		if (line && line[read - 1] == '\n')
		{
			line[read - 1] = '\0';
			read -= 1;
		}

		/* In streaming mode exit when you find empty row */
		if (state->stream_mode && read == 0)
		{
			free(line);

			/* ignore this line if we are on second line - probably watch mode */
			if (nrows == 1)
				goto next_row;

			break;
		}

		clen = utf_string_dsplen(line, read);

		if (rows->nrows == 1000)
		{
			LineBuffer *newrows = smalloc(sizeof(LineBuffer));

			rows->next = newrows;
			newrows->prev = rows;
			rows = newrows;
		}

		rows->rows[rows->nrows++] = line;

		/* save possible table name */
		if (nrows == 0 && !isTopLeftChar(line))
		{
			strncpytrim(opts, desc->title, line, 63, read);
			desc->title_rows = 1;
		}

		if (desc->border_head_row == -1 && desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
			desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;

			if (!desc->is_expanded_mode)
				desc->is_expanded_mode = is_expanded_header(opts, line, NULL, NULL);

			/* title surely doesn't it there */
			if ((!desc->is_expanded_mode && nrows == 1) ||
			    (desc->is_expanded_mode && nrows == 0))
			{
				desc->title[0] = '\0';
				desc->title_rows = 0;
			}
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row == -1 && isBottomLeftChar(line))
		{
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row != -1 && desc->footer_row == -1)
		{
			desc->footer_row = nrows;
		}
		else if (desc->is_expanded_mode && isBottomLeftChar(line))
		{
			/* Outer border is repeated in expanded mode, use last detected row */
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}

		if (!desc->is_expanded_mode && desc->border_head_row != -1 && desc->border_head_row < nrows
			 && desc->alt_footer_row == -1)
		{
			if (*line != '\0' && *line != ' ')
				desc->alt_footer_row = nrows;
		}

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > desc->maxx + 1)
			desc->maxx = clen - 1;

		if ((int) clen > 1 || (clen == 1 && *line != '\n'))
			desc->last_row = nrows;

		nrows += 1;

		/* Detection of status rows */
		if (nrows == 1 && is_cmdtag(line))
			break;

next_row:

		line = NULL;

		read = _getline(&line, &len, state->fp, state->is_blocking, true);
	} while (read != -1);

	if (errno && errno != EAGAIN)
	{
		log_row("cannot to read from file (%s)", strerror(errno));

		return false;
	}

	if (state->detect_truncation)
		state->last_position = ftell(state->fp);

	desc->total_rows = nrows;

	log_row("read rows %d", nrows);

	/*
	 * border headline cannot be higher than 1000, to simply find it
	 * in first row block. Higher number is surelly wrong, probably
	 * some comment.
	 */
	if (desc->border_top_row >= 1000)
		desc->border_top_row = -1;
	if (desc->border_head_row >= 1000)
		desc->border_head_row = -1;

	if (desc->last_row != -1)
		desc->maxy = desc->last_row;

	desc->headline_char_size = 0;

	if (desc->border_head_row != -1)
	{
		if (desc->border_head_row == 0 && !desc->is_expanded_mode)
			goto broken_format;

		desc->headline = desc->rows.rows[desc->border_head_row];
		desc->headline_size = strlen(desc->headline);

		if (desc->last_data_row == -1)
			desc->last_data_row = desc->last_row - 1;

		if (desc->border_head_row >= 1)
			desc->namesline = desc->rows.rows[desc->border_head_row - 1];

	}
	else if (desc->is_expanded_mode && desc->border_top_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_top_row];
		desc->headline_size = strlen(desc->headline);
	}
	else
	{
broken_format:

		desc->headline = NULL;
		desc->headline_size = 0;
		desc->headline_char_size = 0;

		/* there are not a data set */
		desc->last_data_row = desc->last_row;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

#ifdef DEBUG_PIPE

	print_duration(start_sec, start_ms, "read file");

#endif

	/* clean event buffer */
	if (state->inotify_fd >= 0)
		lseek(state->inotify_fd, 0, SEEK_END);

	return true;
}

/*
 * Translate from UTF8 to semantic characters.
 */
bool
translate_headline(Options *opts, DataDesc *desc)
{
	char   *srcptr;
	char   *destptr;
	char   *last_black_char = NULL;
	bool	broken_format = false;
	int		processed_chars = 0;
	bool	is_expanded_info = false;
	bool	force8bit = opts->force8bit;

	srcptr = desc->headline;
	destptr = smalloc(desc->headline_size + 2);

	desc->headline_transl = destptr;

	desc->linestyle = 'a';
	desc->border_type = 0;

	desc->expanded_info_minx = -1;

	while (*srcptr != '\0' && *srcptr != '\n' && *srcptr != '\r')
	{
		/* only spaces can be after known right border */
		if (last_black_char != NULL && *last_black_char == 'R')
		{
			if (*srcptr != ' ')
			{
				broken_format = true;
				break;
			}
		}

		if (*srcptr != ' ')
			last_black_char = destptr;

		if (desc->is_expanded_mode && *srcptr == '[')
		{
			if (desc->expanded_info_minx != -1)
			{
				broken_format = true;
				break;
			}

			/* entry to expanded info mode */
			is_expanded_info = true;
			desc->expanded_info_minx = processed_chars;

			*destptr++ = 'd';
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
		}
		else if (is_expanded_info)
		{
			if (*srcptr == ']')
			{
				is_expanded_info = false;
			}
			*destptr++ = 'd';
			srcptr += force8bit ? 1 : utf8charlen(*srcptr);
		}
		else if (strncmp(srcptr, "\342\224\214", 3) == 0 || /* ┌ */
				  strncmp(srcptr, "\342\225\224", 3) == 0)   /* ╔ */
		{
			/* should be expanded mode */
			if (processed_chars > 0 || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\220", 3) == 0 || /* ┐ */
				 strncmp(srcptr, "\342\225\227", 3) == 0)   /* ╗ */
		{
			if (desc->linestyle != 'u' || desc->border_type != 2 ||
				!desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\254", 3) == 0 || /* ┬╤ */
				 strncmp(srcptr, "\342\225\244", 3) == 0 ||
				 strncmp(srcptr, "\342\225\245", 3) == 0 || /* ╥╦ */
				 strncmp(srcptr, "\342\225\246", 3) == 0)
		{
			if (desc->linestyle != 'u' || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;

			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\234", 3) == 0 || /* ├╟ */
				 strncmp(srcptr, "\342\225\237", 3) == 0 ||
				 strncmp(srcptr, "\342\225\236", 3) == 0 || /* ╞╠ */
				 strncmp(srcptr, "\342\225\240", 3) == 0)
		{
			if (processed_chars > 0)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\244", 3) == 0 || /* ┤╢ */
				 strncmp(srcptr, "\342\225\242", 3) == 0 ||
				 strncmp(srcptr, "\342\225\241", 3) == 0 || /* ╡╣ */
				 strncmp(srcptr, "\342\225\243", 3) == 0)
		{
			if (desc->linestyle != 'u' || desc->border_type != 2)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\274", 3) == 0 || /* ┼╪ */
				 strncmp(srcptr, "\342\225\252", 3) == 0 ||
				 strncmp(srcptr, "\342\225\253", 3) == 0 || /* ╫╬ */
				 strncmp(srcptr, "\342\225\254", 3) == 0)
		{
			if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;
			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\200", 3) == 0 || /* ─ */
				 strncmp(srcptr, "\342\225\220", 3) == 0) /* ═ */
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'u';
			}
			else if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 3;
		}
		else if (*srcptr == '+' || *srcptr == ':')
		{
			if (processed_chars == 0)
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
			}
			else
			{
				if (desc->linestyle != 'a')
				{
					broken_format = true;
					break;
				}
				if (desc->border_type == 0)
					desc->border_type = 1;

				*destptr++ = (srcptr[1] == '-' || srcptr[1] == '=') ? 'I' : 'R';
			}
			srcptr += 1;
		}
		else if (*srcptr == '-' || *srcptr == '=')
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'a';
			}
			else if (desc->linestyle != 'a')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 1;
		}
		else if (*srcptr == '|')
		{
			if (processed_chars == 0 && srcptr[1] == '-')
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
				desc->is_pgcli_fmt = true;
			}
			else if (processed_chars > 0 && desc->is_pgcli_fmt && srcptr[-1] == '-')
			{
				*destptr++ = 'R';
			}
			else
			{
				broken_format = true;
				break;
			}
			srcptr += 1;
		}
		else if (*srcptr == ' ')
		{
			if (desc->border_type != 0)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'I';
			srcptr += 1;
		}
		else
		{
			broken_format = true;
			break;
		}
		processed_chars += 1;
	}

	/* should not be - unclosed header */
	if (is_expanded_info)
		broken_format = true;
	else if (desc->is_expanded_mode && desc->expanded_info_minx == -1)
		broken_format = true;

	if (!broken_format)
	{
		char	   *namesline = desc->namesline;
		char	   *first_char = NULL;				/* first non space char of column name */
		int			offset;
		char	   *ptr;
		int			i;

		/* Move right corner more right */
		if (desc->border_type == 0)
		{
			last_black_char[0] = 'd';
			last_black_char[1] = 'R';
			last_black_char[2] = '\0';
		}

		/* trim ending spaces */
		else if (last_black_char != 0)
		{
			last_black_char[1] = '\0';
		}

		desc->headline_char_size = strlen(desc->headline_transl);

		desc->columns = 1;

		ptr = desc->headline_transl;
		while (*ptr)
		{
			if (*ptr++ == 'I')
				desc->columns += 1;
		}

		desc->cranges = smalloc(desc->columns * sizeof(CRange));

		i = 0; offset = 0;
		ptr = desc->headline_transl;
		desc->cranges[0].xmin = 0;
		desc->cranges[0].name_offset = -1;
		desc->cranges[0].name_size = -1;

		while (*ptr)
		{
			char	   *nextchar = NULL;
			int			display_width;

			if (namesline)
			{
				/* invalidate namesline if there are not good enough chars */
				if (!*namesline)
				{
					namesline = NULL;
					nextchar = NULL;
				}
				else
				{
					nextchar = namesline + (opts->force8bit ? 1 : utf8charlen(*namesline));
					display_width = opts->force8bit ? 1 : utf_dsplen(namesline);
				}
			}
			else
				display_width = 1;

			if (*ptr == 'I')
			{
				desc->cranges[i++].xmax = offset;
				desc->cranges[i].xmin = offset;
				desc->cranges[i].name_offset = -1;
				desc->cranges[i].name_size = -1;
			}
			else if (*ptr == 'd')
			{
				if (namesline && *namesline != ' ')
				{
					if (desc->cranges[i].name_offset == -1)
					{
						desc->cranges[i].name_pos = ptr - desc->headline_transl;
						desc->cranges[i].name_width = display_width;
						desc->cranges[i].name_offset = namesline - desc->namesline;
						desc->cranges[i].name_size = nextchar - namesline;
						first_char = namesline;
					}
					else
					{
						desc->cranges[i].name_size = nextchar - first_char;
						desc->cranges[i].name_width = offset + display_width - desc->cranges[i].name_pos;
					}
				}
			}

			/* possibly some chars can hold more display possitions */
			if (namesline)
				namesline = nextchar;
			else
				display_width = 1;

			offset += display_width;
			ptr += display_width;
		}

		desc->cranges[i].xmax = offset - 1;

		if (!namesline)
			desc->namesline = NULL;

		/*
		 * New PostgreSQL system tables contains visible oid columns. I would to
		 * detect this situation and increase by one default freezed columns. So
		 * second column (with name) will be freezed by default too.
		 */
		if (desc->namesline && desc->columns >= 2)
		{
			if (desc->cranges[0].name_size == 3 &&
					nstrstr_with_sizes(desc->namesline + desc->cranges[0].name_offset,
									   desc->cranges[0].name_size,
									   "oid",
									   3))
			{
				if (desc->cranges[1].name_size > 4 &&
						nstrstr_with_sizes(desc->namesline + desc->cranges[1].name_offset + desc->cranges[1].name_size - 4,
										   4, "name", 4))
					desc->oid_name_table = true;
			}
		}

		return true;
	}

	free(desc->headline_transl);
	desc->headline_transl = NULL;

	return false;
}

/*
 * Cut text from column and translate it to number.
 */
static bool
cut_text(char *str, int xmin, int xmax, bool border0, bool force8bit, char **result)
{
#define TEXT_STACK_BUFFER_SIZE		1024

	if (str)
	{
		char	   *_str = NULL;
		char	   *after_last_nospc = NULL;
		int			pos = 0;
		int			charlen;
		bool		skip_left_spaces = true;

		while (*str)
		{
			charlen = utf8charlen(*str);

			if (pos > xmin || (border0 && pos >= xmin))
			{
				if (skip_left_spaces)
				{
					if (*str == ' ')
					{
						pos += 1;
						str += 1;
						continue;
					}

					/* first nspc char */
					skip_left_spaces = false;
					_str = str;
				}
			}

			if (*str != ' ')
				after_last_nospc = str + charlen;

			pos += utf_dsplen(str);
			str += charlen;

			if (pos >= xmax)
				break;
		}

		if (_str != NULL)
		{
			char		buffer[TEXT_STACK_BUFFER_SIZE];
			char	   *dynbuf = NULL;
			char	   *cstr = NULL;
			int			size;
			int			dynbuf_size = 0;

			cstr = strndup(_str, after_last_nospc - _str);
			if (!cstr)
				leave("out of memory");

			if (force8bit)
			{
				*result = cstr;
				return true;
			}

			errno = 0;
			size = strxfrm(buffer, (const char *) cstr, 1024);
			if (errno != 0)
			{
				/* cannot to sort this string */
				free(cstr);
				return false;
			}

			if (size > TEXT_STACK_BUFFER_SIZE - 1)
			{
				while (size > dynbuf_size)
				{
					if (dynbuf)
						free(dynbuf);

					dynbuf_size = size + 1;
					dynbuf = smalloc(dynbuf_size);

					errno = 0;
					size = strxfrm(dynbuf, cstr, dynbuf_size);
					if (errno != 0)
					{
						/* cannot to sort this string */
						free(cstr);
						return false;
					}
				}
			}

			free(cstr);

			if (!dynbuf)
			{
				dynbuf = sstrdup(buffer);
				if (!dynbuf)
					leave("out of memory");
			}

			*result = dynbuf;

			return true;
		}
	}

	*result = NULL;

	return false;
}

/*
 * Try to cut numeric (double) value from row defined by specified xmin, xmax positions.
 * Units (bytes, kB, MB, GB, TB) are supported. Returns true, when returned value is valid.
 */
static bool
cut_numeric_value(char *str, int xmin, int xmax, double *d, bool border0, bool *isnull, char **nullstr)
{

#define BUFFER_MAX_SIZE			101

	char		buffer[BUFFER_MAX_SIZE];
	char	   *buffptr;
	char	   *after_last_nospace = NULL;
	char	   *first_nospace_nodigit = NULL;
	char		decimal_point = '\0';
	bool		only_digits = false;
	bool		only_digits_with_point = false;
	bool		skip_initial_spaces = true;
	int			x = 0;
	long		mp = 1;

	*isnull = false;

	if (str)
	{
		after_last_nospace = buffptr = buffer;
		memset(buffer, 0, BUFFER_MAX_SIZE);

		while (*str)
		{
			int		charlen = utf8charlen(*str);

			if (x > xmin || (border0 && x >= xmin))
			{
				char	c =  *str;

				if (skip_initial_spaces)
				{
					if (c == ' ')
					{
						x += 1;
						str += 1;
						continue;
					}

					/* first char should be a digit */
					if (!isdigit(c))
					{
						char	   *_nullstr = *nullstr;
						size_t		len;
						char	   *saved_str = str;

						after_last_nospace = saved_str;

						/*
						 * We should to check nullstr if exists, or we should to save
						 * this string as nullstr.
						 */
						while (*str)
						{
							if (*str != ' ')
								after_last_nospace = str + charlen;

							x += utf_dsplen(str);
							str += charlen;

							if (x >= xmax)
								break;

							if (*str)
								charlen = utf8charlen(*str);
						}

						len = after_last_nospace - saved_str;

						if (_nullstr)
						{
							if (strlen(_nullstr) == len)
								*isnull = strncmp(_nullstr, saved_str, len) == 0;
							else
								*isnull = false;
						}
						else
						{
							_nullstr = smalloc(len + 1);

							memcpy(_nullstr, saved_str, len);
							_nullstr[len] = '\0';

							*isnull = true;
							*nullstr = _nullstr;
						}

						return false;
					}

					skip_initial_spaces = false;
					only_digits = true;
				}

				memcpy(buffptr, str, charlen);

				/* trim from right */
				if (c != ' ')
				{
					bool	only_digits_prev = only_digits;
					bool	only_digits_with_point_prev = only_digits_with_point;

					after_last_nospace = buffptr + charlen;
					if (after_last_nospace - buffer > (BUFFER_MAX_SIZE - 1))
					{
						/* too long string - should not be translated to number */
						return false;
					}

					if (c == '.' || c == ',')
					{
						if (only_digits)
						{
							only_digits = false;
							only_digits_with_point = true;
							decimal_point = c;
						}
						else
							return false;
					}
					else if (!isdigit(c))
					{
						only_digits = false;
						only_digits_with_point = false;
					}

					/* Save point of chage between digits and other */
					if ((only_digits_prev || only_digits_with_point_prev) &&
					   !(only_digits || only_digits_with_point))
					{
						first_nospace_nodigit = buffptr;
					}
				}
				buffptr += charlen;
			}

			x += utf_dsplen(str);
			str += charlen;

			if (x >= xmax)
				break;
		} /* while (*str) */

		/* trim spaces from right */
		*after_last_nospace = '\0';

		if (first_nospace_nodigit)
		{
			if (nstreq(first_nospace_nodigit, "bytes"))
				mp = 1l;
			else if (nstreq(first_nospace_nodigit, "kB"))
				mp = 1024l;
			else if (nstreq(first_nospace_nodigit, "MB"))
				mp = 1024l * 1024;
			else if (nstreq(first_nospace_nodigit, "GB"))
				mp = 1024l * 1024 * 1024;
			else if (nstreq(first_nospace_nodigit, "TB"))
				mp = 1024l * 1024 * 1024 * 1024;
			else
				/* unknown unit */
				return false;

			*first_nospace_nodigit = '\0';
		}

		if (decimal_point == ',')
		{
			char   *ptr = buffer;

			while (*ptr)
			{
				if (*ptr == ',')
					*ptr = '.';
				ptr += 1;
			}
		}

		errno = 0;
		*d = strtod(buffer, NULL);
		if (errno == 0)
		{
			*d = *d * mp;
			return true;
		}
	}

	return false;
}


/*
 * Prepare order map - it is used for printing data in different than
 * original order. "sbcn" - sort by column number
 */
void
update_order_map(Options *opts, ScrDesc *scrdesc, DataDesc *desc, int sbcn, bool desc_sort)
{
	LineBuffer	   *lnb = &desc->rows;
	char		   *nullstr = NULL;
	int				xmin, xmax;
	int				lineno = 0;
	bool			continual_line = false;
	bool			has_multilines = false;
	bool			isnull;
	bool			detect_string_column = false;
	bool			border0 = (desc->border_type == 0);
	bool			border1 = (desc->border_type == 1);
	bool			border2 = (desc->border_type == 2);
	SortData	   *sortbuf;
	int				sortbuf_pos = 0;
	int			i;

	xmin = desc->cranges[sbcn - 1].xmin;
	xmax = desc->cranges[sbcn - 1].xmax;

	sortbuf = smalloc(desc->total_rows * sizeof(SortData));

	/* first time we should to detect multilines */
	if (!desc->multilines_already_tested)
	{
		desc->multilines_already_tested = true;

		lnb = &desc->rows;
		lineno = 0;

		while (lnb)
		{
			for (i = 0; i < lnb->nrows; i++)
			{
				if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
				{
					char   *str = lnb->rows[i];
					bool	found_continuation_symbol = false;
					int		j = 0;

					while (j < desc->headline_char_size)
					{
						if (border0)
						{
							/* border 0, last continuation symbol is after headline */
							if (j + 1 == desc->headline_char_size)
							{
								char	*sym;

								sym = str + (opts->force8bit ? 1 : utf8charlen(*str));
								if (*sym != '\0')
									found_continuation_symbol = is_line_continuation_char(sym, desc);
							}
							else if (desc->headline_transl[j] == 'I')
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}
						else if (border1)
						{
							if ((j + 1 < desc->headline_char_size && desc->headline_transl[j + 1] == 'I') ||
									  (j + 1 == desc->headline_char_size))
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}
						else if (border2)
						{
							if ((j + 1 < desc->headline_char_size) &&
									(desc->headline_transl[j + 1] == 'I' || desc->headline_transl[j + 1] == 'R'))
								found_continuation_symbol = is_line_continuation_char(str, desc);
						}

						if (found_continuation_symbol)
							break;

						j += opts->force8bit ? 1 : utf_dsplen(str);
						str += opts->force8bit ? 1 : utf8charlen(*str);
					}

					if (found_continuation_symbol)
					{
						if (lnb->lineinfo == NULL)
							lnb->lineinfo = smalloc(1000 * sizeof(LineInfo));

						lnb->lineinfo[i].mask ^= LINEINFO_CONTINUATION;
						has_multilines = true;
					}
				}

				lineno += 1;
			}
			lnb = lnb->next;
		}
	}

	lnb = &desc->rows;
	lineno = 0;
	sortbuf_pos = 0;

	if (!desc->order_map)
		desc->order_map = smalloc(desc->total_rows * sizeof(MappedLine));

	/*
	 * There are two possible sorting methods: numeric or string.
	 * We can try numeric sort first if all values are numbers or
	 * just only one type of string value (like NULL string). This
	 * value can be repeated,
	 *
	 * When there are more different strings, then start again and
	 * use string sort.
	 */
	while (lnb)
	{
		for (i = 0; i < lnb->nrows; i++)
		{
			desc->order_map[lineno].lnb = lnb;
			desc->order_map[lineno].lnb_row = i;

			if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
			{
				if (!continual_line)
				{
					sortbuf[sortbuf_pos].lnb = lnb;
					sortbuf[sortbuf_pos].lnb_row = i;
					sortbuf[sortbuf_pos].strxfrm = NULL;

					if (cut_numeric_value(lnb->rows[i],
										   xmin, xmax,
										   &sortbuf[sortbuf_pos].d,
										   border0,
										   &isnull,
										   &nullstr))
						sortbuf[sortbuf_pos++].info = INFO_DOUBLE;
					else
					{
						sortbuf[sortbuf_pos++].info = INFO_UNKNOWN;
						if (!isnull)
						{
							detect_string_column = true;
							goto sort_by_string;
						}
					}
				}

				if (has_multilines)
				{
					continual_line = (lnb->lineinfo &&
									  (lnb->lineinfo[i].mask & LINEINFO_CONTINUATION));
				}
			}

			lineno += 1;
		}

		lnb = lnb->next;
	}

sort_by_string:

	free(nullstr);

	if (detect_string_column)
	{
		/* read data again and use nls_string */
		lnb = &desc->rows;
		lineno = 0;
		sortbuf_pos = 0;
		while (lnb)
		{
			for (i = 0; i < lnb->nrows; i++)
			{
				desc->order_map[lineno].lnb = lnb;
				desc->order_map[lineno].lnb_row = i;

				if (lineno >= desc->first_data_row && lineno <= desc->last_data_row)
				{
					if (!continual_line)
					{
						sortbuf[sortbuf_pos].lnb = lnb;
						sortbuf[sortbuf_pos].lnb_row = i;
						sortbuf[sortbuf_pos].d = 0.0;

						if (cut_text(lnb->rows[i], xmin, xmax, border0, opts->force8bit, &sortbuf[sortbuf_pos].strxfrm))
							sortbuf[sortbuf_pos++].info = INFO_STRXFRM;
						else
							sortbuf[sortbuf_pos++].info = INFO_UNKNOWN;		/* empty string */
					}

					if (has_multilines)
					{
						continual_line =  (lnb->lineinfo &&
										   (lnb->lineinfo[i].mask & LINEINFO_CONTINUATION));
					}
				}

				lineno += 1;
			}
			lnb = lnb->next;
		}
	}

	if (lineno != desc->total_rows)
		leave("unexpected processed rows after sort prepare");

	if (detect_string_column)
		sort_column_text(sortbuf, sortbuf_pos, desc_sort);
	else
		sort_column_num(sortbuf, sortbuf_pos, desc_sort);

	lineno = desc->first_data_row;

	for (i = 0; i < sortbuf_pos; i++)
	{
		desc->order_map[lineno].lnb = sortbuf[i].lnb;
		desc->order_map[lineno].lnb_row = sortbuf[i].lnb_row;
		lineno += 1;

		/* assign other continual lines */
		if (has_multilines)
		{
			int		lnb_row;
			bool	continual = false;

			lnb = sortbuf[i].lnb;
			lnb_row = sortbuf[i].lnb_row;

			continual = lnb->lineinfo &&
									   (lnb->lineinfo[lnb_row].mask & LINEINFO_CONTINUATION);

			while (lnb && continual)
			{
				lnb_row += 1;
				if (lnb_row >= lnb->nrows)
				{
					lnb_row = 0;
					lnb = lnb->next;
				}

				desc->order_map[lineno].lnb = lnb;
				desc->order_map[lineno].lnb_row = lnb_row;
				lineno += 1;

				continual = lnb && lnb->lineinfo &&
								(lnb->lineinfo[lnb_row].mask & LINEINFO_CONTINUATION);
			}
		}
	}

	/*
	 * We cannot to say nothing about found_row, so most
	 * correct solution is clean it now.
	 */
	scrdesc->found_row = -1;

	for (i = 0; i < sortbuf_pos; i++)
		free(sortbuf[i].strxfrm);

	free(sortbuf);
}
