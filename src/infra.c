/*-------------------------------------------------------------------------
 *
 * infra.c
 *	  a routines for build a infrastructure
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
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

	if (active_ncurses)
		endwin();

	if (!fmt)
		fmt = "no data";

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

	if (!current_state)
		leave("curren_state is not initialized");

	va_start(args, fmt);
	vsnprintf(pspg_errstr_buffer, PSPG_ERRSTR_BUFFER_SIZE, fmt, args);
	va_end(args);

	current_state->errstr = pspg_errstr_buffer;
}
