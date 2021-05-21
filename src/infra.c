/*-------------------------------------------------------------------------
 *
 * infra.c
 *	  a routines for build a infrastructure
 *
 * Portions Copyright (c) 2017-2021 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/infra.c
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "pspg.h"
#include "unicode.h"

/*
 * Print entry to log file
 */
static void
print_log_prefix(FILE *logfile)
{
	time_t		rawtime;
	struct tm  *timeinfo;
	const char *asct;
	int		len;

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	asct = asctime(timeinfo);
	len = strlen(asct);

	fprintf(logfile, "%.*s ", len - 1, asct);
	fprintf(logfile, "[%ld] ", (long) getpid());
}

void
log_row(const char *fmt, ...)
{
	va_list		args;

	if (current_state && current_state->logfile)
	{
		print_log_prefix(current_state->logfile);

		va_start(args, fmt);
		vfprintf(current_state->logfile, fmt, args);
		va_end(args);

		fputc('\n', current_state->logfile);
	}

#ifdef DEBUG_PIPE

	va_start(args, fmt);
	vfprintf(debug_pipe, fmt, args);
	va_end(args);

	fputc('\n', debug_pipe);

#endif

}

void
leave(const char *fmt, ...)
{
	va_list		args;

	exit_ncurses();

	if (!fmt)
	{
		if (current_state && current_state->logfile)
			fclose(current_state->logfile);
		exit(EXIT_FAILURE);
	}

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fputc('\n', stderr);

	if (current_state && current_state->logfile)
	{
		print_log_prefix(current_state->logfile);

		va_start(args, fmt);
		vfprintf(current_state->logfile, fmt, args);
		va_end(args);

		fputc('\n', current_state->logfile);
		fclose(current_state->logfile);
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
 * truncate spaces from both ends
 */
char *
trim_str(const char *str, int *size, bool force8bit)
{
	char   *result = NULL;

	while (*str == ' ' && *size > 0)
	{
		str += 1;
		*size -= 1;
	}

	if (*size > 0)
	{
		const char   *after_nspc_chr = NULL;

		result = (char *) str;

		while (*size > 0)
		{
			int		charlen = force8bit ? 1 : utf8charlen(*str);

			if (*str != ' ')
				after_nspc_chr = str + charlen;

			str = str + charlen;
			*size -= charlen;
		}

		*size = after_nspc_chr - result;
	}

	return result;
}

/*
 * truncate spaces from both ends, support quotes and double quotes
 */
char *
trim_quoted_str(const char *str, int *size, bool force8bit)
{
	char	   *result;

	result = trim_str(str, size, force8bit);

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

void
ExtStrAppendLine(ExtStr *estr,
				 char *str,
				 int size,
				 bool force8bit,
				 char linestyle,
				 bool continuation_mark)
{
	bool	insert_nl = false;

	str = trim_str(str, &size, force8bit);

	if (size == 0)
		return;

	if (continuation_mark)
	{
		int		continuation_mark_size = 0;

		/* try to detect continuation marks at end of line */
		if (linestyle == 'a')
		{
			if (str[size - 1] == '+')
			{
				continuation_mark_size = 1;
				insert_nl = true;
			}
			else if (str[size - 1] == '.')
				continuation_mark_size = 1;
		}
		else
		{
			const char *u1 = "\342\206\265";	/* ↵ */
			const char *u2 = "\342\200\246";	/* … */

			if (size > 3)
			{
				char	   *ptr = str + size - 3;

				if (strncmp(ptr, u1, 3) == 0)
				{
					continuation_mark_size = 3;
					insert_nl = true;
				}
				else if (strncmp(ptr, u2, 3) == 0)
					continuation_mark_size = 3;
			}
		}

		if (continuation_mark_size > 0)
		{
			size -= continuation_mark_size;

			str = trim_str(str, &size, force8bit);
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
ExtStrTrimEnd(ExtStr *estr, bool replace_nl, bool force8bit)
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

		ptr += force8bit ? 1 : utf8charlen(*ptr);
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
