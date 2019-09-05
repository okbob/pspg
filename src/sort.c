/*-------------------------------------------------------------------------
 *
 * sort.c
 *	  sort of data inside columns
 *
 * Portions Copyright (c) 2017-2019 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/sort.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>

#include "pspg.h"

static int
compar_asc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_DOUBLE)
	{
		if (sda->info == INFO_DOUBLE)
			return sda->d - sdb->d;
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
compar_desc(const void *a, const void *b)
{
	SortData   *sda = (SortData *) a;
	SortData   *sdb = (SortData *) b;

	if (sdb->info == INFO_DOUBLE)
	{
		if (sda->info == INFO_DOUBLE)
			return sdb->d - sda->d;
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
sort_column(SortData *sortbuf, int rows, int rmin, int rmax, bool desc)
{

	qsort(sortbuf + rmin, rmax - rmin + 1, sizeof(SortData), desc ? compar_desc : compar_asc);
}
