/*-------------------------------------------------------------------------
 *
 * infra.c
 *	  a routines for build a infrastructure
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/infra.c
 *
 *-------------------------------------------------------------------------
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "pspg.h"
#include "unicode.h"

FILE	   *logfile = NULL;

/*
 * Print entry to log file
 */
static void
print_log_prefix(void)
{
	time_t		rawtime;
	struct tm  *timeinfo;
	char		outstr[200];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(outstr, sizeof(outstr),  "%a, %d %b %Y %T %z", timeinfo);

	fprintf(logfile, "%s [%ld] ", outstr, (long) getpid());
}

void
log_row(const char *fmt, ...)
{
	va_list		args;

	if (logfile)
	{
		print_log_prefix();

		va_start(args, fmt);
		vfprintf(logfile, fmt, args);
		va_end(args);

		fputc('\n', logfile);
	}

#ifdef DEBUG_PIPE

	if (debug_pipe)
	{
		va_start(args, fmt);
		vfprintf(debug_pipe, fmt, args);
		va_end(args);

		fputc('\n', debug_pipe);
	}

#endif

}

void
leave(const char *fmt, ...)
{
	va_list		args;

	/* close ncurses and input streams */
	exit_handler();

	if (!fmt)
	{
		if (logfile)
		{
			fclose(logfile);
			logfile = NULL;
		}

		exit(EXIT_FAILURE);
	}

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fputc('\n', stderr);

	if (logfile)
	{
		print_log_prefix();

		va_start(args, fmt);
		vfprintf(logfile, fmt, args);
		va_end(args);

		fputc('\n', logfile);

		fclose(logfile);
		logfile = NULL;
	}

#ifdef DEBUG_PIPE

	va_start(args, fmt);
	vfprintf(debug_pipe, fmt, args);
	va_end(args);

	fputc('\n', debug_pipe);

#endif

	exit(EXIT_FAILURE);
}

void
format_error(const char *fmt, ...)
{
	va_list		args;
	char	   *ptr;

	if (!current_state)
		leave("current_state is not initialized");

	va_start(args, fmt);
	vsnprintf(pspg_errstr_buffer, PSPG_ERRSTR_BUFFER_SIZE, fmt, args);
	va_end(args);

	current_state->errstr = pspg_errstr_buffer;

	/* throw multilines strings */
	for (ptr = pspg_errstr_buffer; *ptr; ptr++)
	{
		if (*ptr == '\n')
		{
			*ptr = '\0';
			break;
		}
	}
}

/*
 * Safe memory operation.
 */
void *
smalloc(int size)
{
	void	   *result;

	result = malloc(size);

	if (!result)
		leave("out of memory");

	memset(result, 0, size);

	return result;
}

void *
srealloc(void *ptr, int size)
{
	void	   *result;

	result = realloc(ptr, size);

	if (!result)
		leave("out of memory");

	return result;
}

void *
smalloc2(int size, char *debugstr)
{
	void *result;

	result = malloc(size);
	if (!result)
		leave("out of memory while %s", debugstr);

	memset(result, 0, size);

	return result;
}

char *
sstrdup(const char *str)
{
	char *result = strdup(str);

	if (!result)
		leave("out of memory");

	return result;
}

char *
sstrdup2(const char *str, char *debugstr)
{
	char *result = strdup(str);

	if (!result)
		leave("out of memory while %s", debugstr);

	return result;
}

char *
sstrndup(const char *str, int bytes)
{
	char   *result, *ptr;

	result = ptr = smalloc(bytes + 1);

	while (*str && bytes-- > 0)
		*ptr++ = *str++;

	*ptr = '\0';

	return result;
}

/*
 * Returns byte size of first char of string
 */
inline int
charlen(const char *str)
{
	return use_utf8 ? utf8charlen(*str) : 1;
}

inline int
dsplen(const char *str)
{
	return *str == ' ' ? 1 : (use_utf8 ? utf_dsplen(str) : 1);
}

/*
 * truncate spaces from both ends
 */
char *
trim_str(const char *str, int *size)
{
	char   *result = NULL;
	int		bytes = *size;

	while (*str == ' ' && bytes > 0)
	{
		str += 1;
		bytes -= 1;
	}

	if (bytes > 0)
	{
		const char *after_nspc_chr = NULL;

		result = (char *) str;

		while (bytes > 0)
		{
			int		chrlen = charlen(str);

			if (*str != ' ')
				after_nspc_chr = str + chrlen;

			str = str + chrlen;
			bytes -= chrlen;
		}

		*size = after_nspc_chr - result;
	}
	else
		*size = 0;

	return result;
}

/*
 * truncate spaces from both ends, support quotes and double quotes
 */
char *
trim_quoted_str(const char *str, int *size)
{
	char	   *result;

	result = trim_str(str, size);

	/* check first and last char */
	if (*size > 0)
	{
		if (*result == '"' || *result == '\'')
		{
			if (*result == *(result + *size - 1))
			{
				result += 1;
				*size -= 2;
			}
		}
	}

	return result;
}

/*
 * Few simple functions for string concatetion
 */
void
InitExtStr(ExtStr *estr)
{
	estr->len = 0;
	estr->maxlen = 1024;
	estr->data = smalloc(estr->maxlen);
	*estr->data = '\0';
}

void
ResetExtStr(ExtStr *estr)
{
	estr->len = 0;

	/*
	 * Because the content self is still used, we should not to push
	 * ending zero there.
	 * DONT DO THIS *estr->data = '\0';
	 */
}

void
ExtStrAppendNewLine(ExtStr *estr, char *str)
{
	int		size = strlen(str);

	if (estr->len + size + 2 > estr->maxlen)
	{
		while (estr->len + size + 2 > estr->maxlen)
			estr->maxlen += 1024;

		estr->data = srealloc(estr->data, estr->maxlen);
	}

	if (estr->len > 0)
		estr->data[estr->len++] = '\n';

	strncpy(&estr->data[estr->len], str, size + 1);
	estr->len += size;
}

/*
 * continuation_mark is related to new line symbol in text.
 * continuation_mark is related to line break created by
 * wrapped mode. continuation_mark2 is equal to previous
 * continuous_mark
 */
void
ExtStrAppendLine(ExtStr *estr,
				 char *str,
				 int size,
				 char linestyle,
				 bool continuation_mark,
				 bool continuation_mark2)
{
	bool	insert_nl = false;

	str = trim_str(str, &size);

	if (size == 0)
		return;

	if (continuation_mark)
	{
		int		continuation_mark_size = 0;
		bool	wrapped_mode = false;

		/* try to detect continuation marks at end of line */
		if (linestyle == 'a')
		{
			if (str[size - 1] == '+')
			{
				continuation_mark_size = 1;
				insert_nl = true;
			}
			else if (str[size - 1] == '.')
			{
				continuation_mark_size = 1;
				wrapped_mode = true;
			}
		}
		else
		{

			if (size > 3)
			{
				const char *u1 = "\342\206\265";	/* ↵ */
				const char *u2 = "\342\200\246";	/* … */
				char	   *ptr = str + size - 3;

				if (strncmp(ptr, u1, 3) == 0)
				{
					continuation_mark_size = 3;
					insert_nl = true;
				}
				else if (strncmp(ptr, u2, 3) == 0)
				{
					continuation_mark_size = 3;
					wrapped_mode = true;
				}
			}
		}

		if (continuation_mark_size > 0)
		{
			size -= continuation_mark_size;

			/*
			 * Trimming right end of string can eats spaces. In normal mode, it
			 * should not  be problem, because there is new line symbol, but in
			 * wrapped mode we can trim space that is used as word separator.
			 * So don't trim in wrapped mode.
			 */
			if (!wrapped_mode)
				str = trim_str(str, &size);
		}
	}

	/*
	 * continuation mark can be on left side to (wrapped mode).
	 * We should to skip it.
	 */
	if (continuation_mark2)
	{
		int		cms = 0;		/* continuation mark size */

		if (linestyle == 'a')
		{
			if (*str == '.')
				cms = 1;
		}
		else
		{
			if (size > 3)
			{
				const char *u1 = "\342\200\246";	/* … */

				if (strncmp(str, u1, 3) == 0)
					cms = 3;
			}
		}

		if (cms > 0)
		{
			str += cms;
			size -= cms;
		}
	}

	if (estr->len + size + 2 > estr->maxlen)
	{
		while (estr->len + size + 2 > estr->maxlen)
			estr->maxlen += 1024;

		estr->data = srealloc(estr->data, estr->maxlen);
	}

	strncpy(&estr->data[estr->len], str, size);
	estr->len += size;

	if (insert_nl)
		estr->data[estr->len++] = '\n';

	estr->data[estr->len] = '\0';
}

int
ExtStrTrimEnd(ExtStr *estr, bool replace_nl)
{
	char	   *ptr;
	char	   *last_nonwhite = NULL;

	ptr = estr->data;

	while (*ptr)
	{
		if (*ptr != ' ' && *ptr != '\n')
			last_nonwhite = ptr;

		if (*ptr == '\n' && replace_nl)
			*ptr = ' ';

		ptr += charlen(ptr);
	}

	if (last_nonwhite)
	{
		estr->len = last_nonwhite - estr->data + 1;
		estr->data[estr->len] = '\0';
	}
	else
		ResetExtStr(estr);

	return estr->len;
}

/*
 * read write stderr poopen function
 */
int
rwe_popen(char *command, int *fin, int *fout, int *ferr)
{
	int		in[2];
	int		out[2];
	int		err[2];
	int		rc;
	int		saved_errno;

	errno = 0;
	saved_errno = 0;

	rc = pipe(in);
	if (rc == 0)
	{
		rc = pipe(out);
		if (rc == 0)
		{
			rc = pipe(err);
			if (rc == 0)
			{
				int		pid = fork();

				if (pid > 0)
				{
					/* parent */
					close(in[0]);
					close(out[1]);
					close(err[1]);

					*fin = in[1];
					*fout = out[0];
					*ferr = err[0];

					return pid;
				}
				else if (pid == 0)
				{
					/* child */
					close(in[1]);
					close(out[0]);
					close(err[0]);

					dup2(in[0], 0);
					dup2(out[1], 1);
					dup2(err[1], 2);

					close(in[0]);
					close(out[1]);
					close(err[1]);

					execl("/bin/sh", "sh", "-c", command, NULL);
					exit(127);
				}
				else
					saved_errno = errno;

				close(err[0]);
				close(err[1]);
			}
			else
				saved_errno = errno;

			close(out[0]);
			close(out[1]);
		}
		else
			saved_errno = errno;

		close(in[0]);
		close(out[1]);
	}
	else
		saved_errno = errno;

	errno = saved_errno;

	return -1;
}

/*
 * Replace tilde by HOME dir
 */
char *
tilde(char *dest, const char *path)
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
