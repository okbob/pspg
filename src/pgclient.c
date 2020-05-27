/*-------------------------------------------------------------------------
 *
 * pgcliend.c
 *	  execute query and format result
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
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

#define EXIT_OUT_OF_MEMORY()		do { PQclear(result); PQfinish(conn); leave("out of memory"); } while (0)
#define RELEASE_AND_LEAVE(s)		do { PQclear(result); PQfinish(conn); *err = s; return false; } while (0)
#define RELEASE_AND_EXIT(s)			do { PQclear(result); PQfinish(conn); leave(s); } while (0)

#ifdef HAVE_POSTGRESQL

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
 * Returns true, when some column should be hidden.
 */
static int
mark_hidden_columns(PGresult *result,
					int nfields,
					Options *opts,
					bool *hidden)
{
	char	   *names;
	char	   *endnames;
	char	   *ptr;
	int		i;
	int		visible_columns = 0;

	for (i = 0; i < nfields; i++)
		hidden[i] = false;

	if (!opts->csv_skip_columns_like)
		return nfields;

	/* prepare list of hidden columns */
	names = sstrdup(opts->csv_skip_columns_like);

	endnames = names + strlen(names);
	ptr = names;
	while (*ptr)
	{
		/* space is separator between words */
		if (*ptr == ' ')
			*ptr = '\0';
		ptr += 1;
	}

	for (i = 0; i < nfields; i++)
	{
		char	   *fieldname = PQfname(result, i);

		ptr = names;
		while (ptr < endnames)
		{
			if (*ptr)
			{
				size_t		len = strlen(ptr);

				if (*ptr == '^')
				{
					if (strncmp(fieldname, ptr + 1, len - 1) == 0)
						hidden[i] = true;
				}
				else if (ptr[len - 1] == '$')
				{
					size_t		len2 = strlen(fieldname);

					if (len2 > (len - 1) &&
						strncmp(fieldname + len2 - len + 1, ptr, len - 1) == 0)
						hidden[i] = true;
				}
				else if (strstr(fieldname, ptr))
					hidden[i] = true;
			}

			ptr += strlen(ptr) + 1;
		}

		if (!hidden[i])
			visible_columns += 1;
	}

	free(names);

	return visible_columns;
}


#endif


/*
 * exit on fatal error, or return error
 */
bool
pg_exec_query(Options *opts, RowBucketType *rb, PrintDataDesc *pdesc, const char **err)
{

	log_row("execute query \"%s\"", opts->query);

#ifdef HAVE_POSTGRESQL

	PGconn	   *conn = NULL;
	PGresult   *result = NULL;

	int			nfields;
	int			size;
	int			i, j;
	int			n;
	char	   *locbuf;
	RowType	   *row;
	bool		multiline_row;
	bool		multiline_col;
	char	   *password;

	const char *keywords[8];
	const char *values[8];

	bool	   *hidden;

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

	hidden = smalloc(1024 * sizeof(bool));

	pdesc->nfields = mark_hidden_columns(result, nfields, opts, hidden);

	pdesc->has_header = true;
	n = 0;
	for (i = 0; i < nfields; i++)
		if (!hidden[i])
			pdesc->types[n++] = column_type_class(PQftype(result, i));

	/* calculate necessary size of header data */
	size = 0;
	for (i = 0; i < nfields; i++)
		if (!hidden[i])
			size += strlen(PQfname(result, i)) + 1;

	locbuf = malloc(size);
	if (!locbuf)
		EXIT_OUT_OF_MEMORY();

	/* store header */
	row = malloc(offsetof(RowType, fields) + (pdesc->nfields * sizeof(char *)));
	if (!row)
		EXIT_OUT_OF_MEMORY();

	row->nfields = nfields;

	multiline_row = false;
	n = 0;
	for (i = 0; i < nfields; i++)
	{
		char   *name = PQfname(result, i);

		if (hidden[i])
			continue;

		strcpy(locbuf, name);
		row->fields[n] = locbuf;
		locbuf += strlen(name) + 1;

		pdesc->widths[n] = field_info(opts, row->fields[n], &multiline_col);
		pdesc->multilines[n++] = multiline_col;

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
			if (!hidden[j])
				size += strlen(PQgetvalue(result, i, j)) + 1;

		locbuf = malloc(size);
		if (!locbuf)
			EXIT_OUT_OF_MEMORY();

		/* store data */
		row = malloc(offsetof(RowType, fields) + (pdesc->nfields * sizeof(char *)));
		if (!row)
			EXIT_OUT_OF_MEMORY();

		row->nfields = pdesc->nfields;

		multiline_row = false;
		n = 0;
		for (j = 0; j < nfields; j++)
		{
			char	*value;

			if (hidden[j])
				continue;

			value = PQgetvalue(result, i, j);

			strcpy(locbuf, value);
			row->fields[n] = locbuf;
			locbuf += strlen(value) + 1;

			pdesc->widths[n] = max_int(pdesc->widths[n],
									  field_info(opts, row->fields[n], &multiline_col));
			pdesc->multilines[n] |= multiline_col;
			multiline_row |= multiline_col;

			pdesc->columns_map[n] = n;
			n += 1;
		}

		rb = push_row(rb, row, multiline_row);
		if (!rb)
			EXIT_OUT_OF_MEMORY();
	}

	free(hidden);

	PQclear(result);
	PQfinish(conn);

	*err = NULL;

	return true;

#else

	*err = "Query cannot be executed. The Postgres library was not available at compile time.";

	return false;

#endif

}
