/*-------------------------------------------------------------------------
 *
 * sort.c
 *	  sort of data inside columns
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/sort.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>

#include "pspg.h"

static inline int
signof(double n)
{
	if (n < 0)
		return -1;
	else if (n > 0)
		return 1;
	else
		return 0;
}

static int
compar_num_asc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_DOUBLE)
	{
		if (sda->info == INFO_DOUBLE)
			/*
			 * Without using signof function there is possible problem with
			 * mapping of 8bytes double to 4bytes int (the sign of result value
			 * after casting can be wrong).
			 */
			return signof(sda->d - sdb->d);
		else
			return 1;
	}
	else
	{
		if (sda->info == INFO_DOUBLE)
			return -1;
		else
			return 0;
	}
}

static int
compar_num_desc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_DOUBLE)
	{
		if (sda->info == INFO_DOUBLE)
			return signof(sdb->d - sda->d);
		else
			return 1;
	}
	else
	{
		if (sda->info == INFO_DOUBLE)
			return -1;
		else
			return 0;
	}
}

void
sort_column_num(SortData *sortbuf, int rows, bool desc)
{

	qsort(sortbuf, rows, sizeof(SortData), desc ? compar_num_desc : compar_num_asc);
}

static int
compar_text_asc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_STRXFRM)
	{
		if (sda->info == INFO_STRXFRM)
			return strcmp(sda->strxfrm, sdb->strxfrm);
		else
			return 1;
	}
	else
	{
		if (sda->info == INFO_STRXFRM)
			return -1;
		else
			return 0;
	}
}

static int
compar_text_desc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_STRXFRM)
	{
		if (sda->info == INFO_STRXFRM)
			return strcmp(sdb->strxfrm, sda->strxfrm);
		else
			return 1;
	}
	else
	{
		if (sda->info == INFO_STRXFRM)
			return -1;
		else
			return 0;
	}
}

void
sort_column_text(SortData *sortbuf, int rows, bool desc)
{
	qsort(sortbuf, rows, sizeof(SortData), desc ? compar_text_desc : compar_text_asc);
}
