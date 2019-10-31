/*-------------------------------------------------------------------------
 *
 * pgcliend.c
 *	  execute query and format result
 *
 * Portions Copyright (c) 2017-2019 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/pgclient.c
 *
 *-------------------------------------------------------------------------
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pspg.h"
#include "unicode.h"

#ifdef HAVE_POSTGRESQL

#include <libpq-fe.h>

char errmsg[1024];

static RowBucketType *
push_row(RowBucketType *rb, RowType *row, bool is_multiline)
{
	if (rb->nrows >= 1000)
	{
		RowBucketType *new = malloc(sizeof(RowBucketType));

		if (!new)
			return NULL;

		new->nrows = 0;
		new->allocated = true;
		new->next_bucket = NULL;

		rb->next_bucket = new;
		rb = new;
	}

	rb->rows[rb->nrows] = row;
	rb->multilines[rb->nrows++] = is_multiline;

	return rb;
}

/*
 * Correct solution is importing header file catalog/pg_type_d.h,
 * but this file is not in basic libpq headers, so instead enhancing
 * dependency just copy these values that should be immutable.
 */
#define INT8OID 20
#define INT2OID 21
#define INT4OID 23
#define FLOAT4OID 700
#define FLOAT8OID 701
#define XIDOID 28
#define CIDOID 29
#define CASHOID 790
#define NUMERICOID 1700
#define OIDOID 26
 
static char
column_type_class(Oid ftype)
{
	char		align;

	switch (ftype)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
		case OIDOID:
		case XIDOID:
		case CIDOID:
		case CASHOID:
			align = 'd';
			break;
		default:
			align = 'a';
			break;
	}
	return align;
}


#endif

#define EXIT_OUT_OF_MEMORY()		do { PQclear(result); PQfinish(conn); leave_ncurses("out of memory"); } while (0)
#define RELEASE_AND_LEAVE(s)		do { PQclear(result); PQfinish(conn); *err = s; return false; } while (0)
#define RELEASE_AND_EXIT(s)			do { PQclear(result); PQfinish(conn); leave_ncurses(s); } while (0)

static int
field_info(Options *opts, char *str, bool *multiline)
{
	long int	digits;
	long int	others;

	if (opts->force8bit)
	{
		int		cw = 0;
		int		width = 0;

		while (*str)
		{
			if (*str++ == '\n')
			{
				*multiline = true;
				width = cw > width ? cw : width;
				cw = 0;
			}
			else
				cw++;
		}

		return cw > width ? cw : width;
	}
	else
		return utf_string_dsplen_multiline(str, strlen(str), multiline, false, &digits, &others);
}

static int
max_int(int a, int b)
{
	return a > b ? a : b;
}

/*
 * exit on fatal error, or return error
 */
bool
pg_exec_query(Options *opts, RowBucketType *rb, PrintDataDesc *pdesc, const char **err)
{

#ifdef HAVE_POSTGRESQL

	PGconn	   *conn = NULL;
	PGresult   *result = NULL;

	int			nfields;
	int			size;
	int			i, j;
	char	   *locbuf;
	RowType	   *row;
	bool		multiline_row;
	bool		multiline_col;
	char	   *password;

	const char *keywords[8];
	const char *values[8];

	rb->nrows = 0;
	rb->next_bucket = NULL;

	if (opts->force_password_prompt && !opts->password)
	{
		password = getpass("Password: ");
		opts->password = strdup(password);
		if (!opts->password)
			EXIT_OUT_OF_MEMORY();
	}

	keywords[0] = "host"; values[0] = opts->host;
	keywords[1] = "port"; values[1] = opts->port;
	keywords[2] = "user"; values[2] = opts->username;
	keywords[3] = "password"; values[3] = opts->password;
	keywords[4] = "dbname"; values[4] = opts->dbname;
	keywords[5] = "fallback_application_name"; values[5] = "pspg";
	keywords[6] = "client_encoding"; values[6] = getenv("PGCLIENTENCODING") ? NULL : "auto";
	keywords[7] = NULL; values[7] = NULL;

	conn = PQconnectdbParams(keywords, values, true);

	if (PQstatus(conn) == CONNECTION_BAD &&
		PQconnectionNeedsPassword(conn) &&
		!opts->password)
	{
		password = getpass("Password: ");
		opts->password = strdup(password);
		if (!opts->password)
				EXIT_OUT_OF_MEMORY();

		keywords[3] = "password"; values[3] = opts->password;

		conn = PQconnectdbParams(keywords, values, true);
	}

	/* Check to see that the backend connection was successfully made */
	if (PQstatus(conn) != CONNECTION_OK)
	{
		sprintf(errmsg, "Connection to database failed: %s", PQerrorMessage(conn));
		RELEASE_AND_LEAVE(errmsg);
	}

	/*
	 * ToDo: Because data are copied to local memory, the result can be fetched.
	 * It can save 1/2 memory.
	 */
	result = PQexec(conn, opts->query);
	if (PQresultStatus(result) != PGRES_TUPLES_OK)
	{
		sprintf(errmsg, "Query doesn't return data: %s", PQerrorMessage(conn));
		RELEASE_AND_LEAVE(errmsg);
	}

	if ((nfields = PQnfields(result)) > 1024)
		RELEASE_AND_EXIT("too much columns");

	pdesc->nfields = nfields;
	pdesc->has_header = true;
	for (i = 0; i < nfields; i++)
		pdesc->types[i] = column_type_class(PQftype(result, i));

	/* calculate necessary size of header data */
	size = 0;
	for (i = 0; i < nfields; i++)
		size += strlen(PQfname(result, i)) + 1;

	locbuf = malloc(size);
	if (!locbuf)
		EXIT_OUT_OF_MEMORY();

	/* store header */
	row = malloc(offsetof(RowType, fields) + (nfields * sizeof(char *)));
	if (!row)
		EXIT_OUT_OF_MEMORY();

	row->nfields = nfields;

	multiline_row = false;
	for (i = 0; i < nfields; i++)
	{
		char   *name = PQfname(result, i);

		strcpy(locbuf, name);
		row->fields[i] = locbuf;
		locbuf += strlen(name) + 1;

		pdesc->widths[i] = field_info(opts, row->fields[i], &multiline_col);
		pdesc->multilines[i] = multiline_col;

		multiline_row |= multiline_col;
	}

	rb = push_row(rb, row, multiline_row);
	if (!rb)
		EXIT_OUT_OF_MEMORY();

	/* calculate size for any row and store it */
	for (i = 0; i < PQntuples(result); i++)
	{
		size = 0;
		for (j = 0; j < nfields; j++)
			size += strlen(PQgetvalue(result, i, j)) + 1;

		locbuf = malloc(size);
		if (!locbuf)
			EXIT_OUT_OF_MEMORY();

		/* store data */
		row = malloc(offsetof(RowType, fields) + (nfields * sizeof(char *)));
		if (!row)
			EXIT_OUT_OF_MEMORY();

		row->nfields = nfields;

		multiline_row = false;
		for (j = 0; j < nfields; j++)
		{
			char	*value = PQgetvalue(result, i, j);

			strcpy(locbuf, value);
			row->fields[j] = locbuf;
			locbuf += strlen(value) + 1;

			pdesc->widths[j] = max_int(pdesc->widths[j],
									  field_info(opts, row->fields[j], &multiline_col));
			pdesc->multilines[j] |= multiline_col;
			multiline_row |= multiline_col;
		}

		rb = push_row(rb, row, multiline_row);
		if (!rb)
			EXIT_OUT_OF_MEMORY();
	}

    PQclear(result);
    PQfinish(conn);

	*err = NULL;

	return true;

#else

	err = "Query cannot be executed. The Postgres library was not available at compile time."

	return false;

#endif

}
