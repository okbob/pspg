#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <locale.h>
#include <unistd.h>

#include <signal.h>
#include <sys/ioctl.h>

#include <sys/param.h>

#define FILENAME		"pg_class.txt"
#define STYLE			1

typedef struct
{
	int		border_top_row;			/* nrow of bootom outer border or -1 */
	int		border_head_row;		/* nrow of head outer (required) */
	int		border_bottom_row;		/* nrow of bottom outer border or -1 */
	char	title[65];				/* detected title (trimmed) or NULL */
	int		title_rows;				/* number of rows used as table title (skipped later) */
	char	filename[65];			/* filename (printed on top bar) */
	WINDOW *rows;					/* pointer to holding ncurses pad */
	int		maxy;					/* maxy of used pad area with data */
	int		maxx;					/* maxx of used pad area with data */
	int		maxbytes;				/* max length of line in bytes */
	char   *headline;				/* header separator line */
	int		headline_size;			/* size of headerline in bytes */
	int		headline_char_size;		/* size of headerline in chars */
	int		last_data_row;			/* last line of data row */
	int		last_row;				/* last not empty row */
} DataDesc;

typedef struct
{
	int		fix_rows_rows;			/* number of fixed rows in pad rows */
	int		fix_cols_cols;			/* number of fixed colums in pad rows */
	WINDOW *luc;					/* pad for left upper corner */
	WINDOW *fix_rows;				/* pad for fixed rows */
	WINDOW *fix_columns;			/* pad for fixed columns */
	WINDOW *rows;					/* pad for data */
	WINDOW *top_bar;				/* top bar window */
	WINDOW *bottom_bar;				/* bottom bar window */
	int		cursor_y;				/* y pos of virtual cursor */
	int		cursor_x;				/* x pos of virtual cursor */
	int		theme;					/* color theme number */
} ScrDesc;

/*
 * Returns true, if char is related to data (no border) position.
 * It should be tested on border_head_row line.
 */
static bool
isDataPosChar(char *str)
{
	const char *u1 = "\342\224\200";
	const char *u2 = "\342\225\220";

	if (str[0] == '-')
		return true;

	if (str[0] == ' ' || str[0] == '+')
		return false;

	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;

	return false;
}

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
	const char *u4 = "\342\225\241";

	if (str[0] == '+' || str[0] == '-')
		return true;
	if (strncmp(str, u1, 3) == 0)
		return true;
	if (strncmp(str, u2, 3) == 0)
		return true;
	if (strncmp(str, u3, 3) == 0)
		return true;
	if (strncmp(str, u4, 3) == 0)
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
 * Returns length of utf8 string in chars.
 */
static size_t
utf8len(char *s)
{
	size_t len = 0;

	for (; *s; ++s)
		if ((*s & 0xC0) != 0x80)
			++len;
	return len;
}

/*
 * Returns length of utf8 char in bytes
 */
static int
utf8charlen(char ch)
{
	if ((ch & 0xF0) == 0xF0)
		return 4;

	if ((ch & 0xE0) == 0xE0)
		return 3;

	if ((ch & 0xC0) == 0xC0)
		return 2;

	return 1;
}

/*
 * Copy trimmed string
 */
static void
strncpytrim(char *dest, const char *src,
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
		int	clen;

		if (*src == '\0')
			break;

		clen = utf8charlen(*src);
		if (clen <= ndest && clen <= nsrc)
		{
			int		i;

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
 * Set color pairs based on style
 */
static void
initialize_color_pairs(int theme)
{
	if (theme == 0)
	{
		use_default_colors();

		init_pair(1, -1, -1);							/* default */
		init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, -1, -1);							/* fix rows, columns */
		init_pair(5, COLOR_BLACK, COLOR_WHITE);			/* active cursor over fixed cols */
		init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
	}
	else if (theme == 1)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(1, -1, -1);
		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_YELLOW, COLOR_BLUE);
		init_pair(5, COLOR_YELLOW, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
	}
	else if (theme == 2)
	{
		assume_default_colors(COLOR_WHITE, COLOR_CYAN);

		init_pair(1, -1, -1);
		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_BLACK, COLOR_WHITE);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(7, COLOR_YELLOW, COLOR_WHITE);
	}
}

/*
 * Read data from file and fill ncurses pad. Increase
 * pad when it it necessary
 */
static int
readfile(FILE *fp, DataDesc *desc , int *rows, int *cols)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	int			maxx, maxy;
	WINDOW	   *pp;
	bool		use_stdin = false;

	pp = desc->rows;

	/* safe reset */
	desc->filename[0] = '\0';

	if (fp == NULL)
	{
		use_stdin = true;
		fp = stdin;
	}
	else
	{
		int		fno;
		char	proclnk[MAXPATHLEN + 1];
		char	path[MAXPATHLEN + 1];
		ssize_t r;

		fno = fileno(fp);

		sprintf(proclnk, "/proc/self/fd/%d", fno);

		r = readlink(proclnk, path, MAXPATHLEN);
		if (r > 0)
		{
			char	   *name;

			path[r] = '\0';
			name = basename(path);
			strncpy(desc->filename, name, 64);
			desc->filename[64] = '\0';
		}
	}

	desc->title[0] = '\0';
	desc->title_rows = 0;
	desc->border_top_row = -1;
	desc->border_head_row = -1;
	desc->border_bottom_row = -1;
	desc->last_data_row = -1;

	desc->maxbytes = -1;
	desc->maxx = -1;

	*cols = -1;

	getmaxyx(pp, maxy, maxx);

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		nmaxx, nmaxy;
		int		clen = utf8len(line);

		/* save possible table name */
		if (nrows == 0 && !isTopLeftChar(line))
		{
			strncpytrim(desc->title, line, 63, len);
			desc->title_rows = 1;
		}

		if (desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;
			/* title surelly doesn't it there */
			if (nrows == 1)
			{
				desc->title[0] = '\0';
				desc->title_rows = 0;
			}
		}
		else if (desc->border_bottom_row == -1 && isBottomLeftChar(line))
		{
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > *cols)
			*cols = clen;

		if ((int) clen > desc->maxx)
			desc->maxx = clen;

		if ((int) clen > 0)
			desc->last_row = nrows;

		nmaxx = clen > maxx ? clen + 100 : maxx;
		nmaxy = nrows > maxy - 1 ? nrows  * 2 : maxy;

		/*
		 * Is necessary to resize main pad? 
		 * Attention: doesn't work well. Maybe ncurses bug?
		 */
		if (nmaxx > maxx || nmaxy > maxy)
		{
			WINDOW *np = newpad(nmaxy, nmaxx);

			copywin(pp, np, 0, 0, 0, 0, nrows - 1, clen, true);
			delwin(pp);
			pp = np;

			/*
			 * wresize doesn't work - it does unwantd artefacts
			 */
			getmaxyx(pp, maxy, maxx);
		}

		wmove(pp, nrows++, 0);
		waddstr(pp, line);
		/*
		 * attention - mvprintw trims lines with lot of multibytes chars
		 * Don't use it: mvwprintw(pp, nrows++, 0, "%s", line);
		 */
	}

	if (!use_stdin)
		fclose(fp);


	desc->rows = pp;
	desc->maxy = nrows - 1;

	if (desc->border_head_row != -1)
	{
		int		i = 0;
		int		last_not_spc = -1;
		char   *c;

		desc->headline = malloc(desc->maxbytes + 1);
		desc->headline_size = mvwinnstr(desc->rows, desc->border_head_row, 0, desc->headline, desc->maxbytes);

		c = desc->headline;
		while (*c != '\0')
		{
			if (*c != ' ')
				last_not_spc = i;
			c += utf8charlen(*c);
			i++;
		}

		desc->headline_char_size = last_not_spc;

		if (desc->last_data_row == -1)
			desc->last_data_row = desc->last_row - 1;
	}
	else
	{
		desc->headline = NULL;
		desc->headline_size = 0;
		desc->headline_char_size;

		/* there are not a data set */
		desc->last_row = nrows;
		desc->last_data_row = nrows;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	*rows = nrows;

	freopen("/dev/tty", "rw", stdin);

	return 0;
}

/*
 * Set bold attributes for char in line. Skip border chars
 */
static void
set_bold_row(WINDOW *win, int nrow,
			 DataDesc *desc,
			 int start,
			 int maxx1, int cp1,
			 int maxx2, int cp2,
			 int cp3)
{

	if (desc->headline != NULL)
	{
		char   *cur;
		int		xpos = 0;
		int		unprocessed = desc->headline_size;

		cur = desc->headline;

		while (unprocessed > 0 && xpos <= maxx2)
		{
			int		chlen;

			if (xpos >= start)
			{
				/* apply colors for data or for borders */
				if (isDataPosChar(cur))
				{
					if (xpos <= maxx1)
						mvwchgat(win, nrow, xpos, 1, A_BOLD, cp1, 0);
					else
						mvwchgat(win, nrow, xpos, 1, 0, cp2, 0);
				}
				else
					mvwchgat(win, nrow, xpos, 1, 0, cp3, 0);
			}

			chlen = utf8charlen(*cur);
			cur += chlen;
			unprocessed -= chlen;
			xpos += 1;
		}
	}
	else
	{
		mvwchgat(win, nrow, 0, maxx1, A_BOLD, cp1, 0);
		mvwchgat(win, nrow, maxx1, maxx2 - maxx1, 0, cp2, 0);
	}
}

/*
 * Draw cursor
 */
static void
refresh_cursor(int nrow, int prevrow,
			   bool force,
			   DataDesc *desc, ScrDesc *scrdesc)
{
	if (prevrow != nrow || force)
	{
		int		rows_maxy, rows_maxx;
		int		fixc_maxy, fixc_maxx;

		getmaxyx(scrdesc->rows, rows_maxy, rows_maxx);
		getmaxyx(scrdesc->fix_columns, fixc_maxy, fixc_maxx);

		/* clean prev cursor */
		if (prevrow != -1)
		{
			if (prevrow + scrdesc->fix_rows_rows > desc->last_data_row)
			{
				mvwchgat(scrdesc->rows, prevrow + scrdesc->fix_rows_rows, 0, -1, 0, 1, 0);
				mvwchgat(scrdesc->fix_columns, prevrow + scrdesc->fix_rows_rows, 0, -1, 0, 1, 0);
			}
			else
			{
				if (scrdesc->theme == 2)
				{
					set_bold_row(scrdesc->rows, prevrow + scrdesc->fix_rows_rows, desc, 0, rows_maxx, 1, rows_maxx, 0, 1);
					mvwchgat(scrdesc->rows, prevrow + scrdesc->fix_rows_rows, desc->maxx, -1, 0, 1, 0);
				}
				else
					mvwchgat(scrdesc->rows, prevrow + scrdesc->fix_rows_rows, 0, -1, 0, 1, 0);

				set_bold_row(scrdesc->fix_columns, prevrow + scrdesc->fix_rows_rows, desc, 0, fixc_maxx, 4, fixc_maxx, 0, 1);
			}
		}

		if (nrow + scrdesc->fix_rows_rows > desc->last_data_row)
		{
			mvwchgat(scrdesc->rows, nrow + scrdesc->fix_rows_rows, 0, rows_maxx, 0, 6, 0);
			mvwchgat(scrdesc->fix_columns, nrow + scrdesc->fix_rows_rows, 0,  fixc_maxx, 0, 6, 0);
		}
		else
		{
			set_bold_row(scrdesc->rows, nrow + scrdesc->fix_rows_rows, desc, 0, rows_maxx, 6, rows_maxx, 0, scrdesc->theme == 2 ? 1 : 6);
			set_bold_row(scrdesc->fix_columns, nrow + scrdesc->fix_rows_rows, desc, 0, fixc_maxx, 5, fixc_maxx, 0, scrdesc->theme == 2 ? 1 : 6);
		}

		if (desc->maxx < rows_maxx)
			mvwchgat(scrdesc->rows, nrow + scrdesc->fix_rows_rows, desc->headline_char_size + 1, -1, 0, 6, 0);
	}
}


static void
refresh_main_pads(ScrDesc *scrdesc, DataDesc *desc,
				 int fixCol, int fixRows,
				 int cursor_col, int first_row,
				 int theme, int maxx)
{
	char	*str = desc->headline;
	int		nchars = desc->headline_size;
	int		clen;
	bool	found = false;
	int		nchar = 0;
	int		i;
	bool	use_default_fixCol = false;

	scrdesc->rows = desc->rows;
	scrdesc->theme = theme;

	/* when there are outer border, starts on 1 position */
	if (desc->border_top_row != -1)
	{
		clen = utf8charlen(*str);
		nchars -= clen;
		str += clen;
		nchar = 1;
	}

	if (fixCol == -1)
	{
		use_default_fixCol = true;
		fixCol = 1;
	}

	while (nchars > 0)
	{
		if (!isDataPosChar(str) && fixCol-- == 1)
		{
			found = true;
			break;
		}

		clen = utf8charlen(*str);
		nchars -= clen;
		str += clen;
		nchar += 1;
	}

	scrdesc->fix_cols_cols = found ? nchar + 1 : 0;

	/* disable default fixCols when is not possible draw in screen */
	if (use_default_fixCol && scrdesc->fix_cols_cols > maxx)
		scrdesc->fix_cols_cols = 0;

	if (fixRows != -1)
		scrdesc->fix_rows_rows = fixRows;
	else
		scrdesc->fix_rows_rows = desc->border_head_row != -1 ? desc->border_head_row + 1 : 0;

	for (i = 0; i <= desc->last_data_row; i++)
	{
		if (i == desc->border_top_row ||
			i == desc->border_head_row ||
			i == desc->border_bottom_row)
			continue;

			if (i < scrdesc->fix_rows_rows)
			{
				set_bold_row(scrdesc->rows, i, desc, 0, desc->maxx, 4, desc->maxx, 4, 1);
			}
			else
			{
				set_bold_row(scrdesc->rows, i, desc, 0, scrdesc->theme != 2 ? scrdesc->fix_cols_cols : desc->maxx, 4, desc->maxx, 1, 1);
			}
	}

	/*
	 * Prepare left upper corner pad
	 */
	if (scrdesc->luc != NULL)
	{
		delwin(scrdesc->luc);
		scrdesc->luc = NULL;
	}

	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		scrdesc->luc = newpad(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols);
		copywin(desc->rows, scrdesc->luc, 0, 0, 0, 0, scrdesc->fix_rows_rows - 1, scrdesc->fix_cols_cols - 1, false);
	}

	/*
	 * Prepare other fixed rows
	 */
	if (scrdesc->fix_rows != NULL)
	{
		delwin(scrdesc->fix_rows);
		scrdesc->fix_rows = NULL;
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		scrdesc->fix_rows = newpad(scrdesc->fix_rows_rows, desc->maxx + 1);
		copywin(desc->rows, scrdesc->fix_rows, 0, 0, 0, 0, scrdesc->fix_rows_rows - 1, desc->maxx, false);
	}

	if (scrdesc->fix_columns != NULL)
	{
		delwin(scrdesc->fix_columns);
		scrdesc->fix_columns = NULL;
	}

	if (scrdesc->fix_cols_cols > 0)
	{
		scrdesc->fix_columns = newpad(desc->maxy + 1, scrdesc->fix_cols_cols);
		copywin(desc->rows, scrdesc->fix_columns, 0, 0, 0, 0, desc->maxy, scrdesc->fix_cols_cols - 1, false);
	}

}

/*
 * Rewresh aux windows like top bar or bottom bar.
 */
static void
refresh_aux_windows(ScrDesc *scrdesc)
{
	int		maxy, maxx;

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	if (scrdesc->top_bar != NULL)
		delwin(scrdesc->top_bar);

	scrdesc->top_bar = newwin(1, 0, 0, 0);
	wbkgd(scrdesc->top_bar, COLOR_PAIR(2));
	wrefresh(scrdesc->top_bar);

	if (scrdesc->bottom_bar != NULL)
		delwin(scrdesc->bottom_bar);

	scrdesc->bottom_bar = newwin(1, 0, maxy - 1, 0);

	wattron(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
	mvwaddstr(scrdesc->bottom_bar, 0, 1, "Q");
	wattroff(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
	wattron(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
	mvwprintw(scrdesc->bottom_bar, 0, 2, "%-4s", "uit");
	wattroff(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
	wrefresh(scrdesc->bottom_bar);

	wattron(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
	mvwaddstr(scrdesc->bottom_bar, 0, 7, "0..4");
	wattroff(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
	wattron(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
	mvwprintw(scrdesc->bottom_bar, 0, 11, "%s", " C.Freeze ");
	wattroff(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
	wrefresh(scrdesc->bottom_bar);
}

int
main(int argc, char *argv[])
{
	WINDOW *rows;
	int		nrows;
	int		ncols;
	int		maxx, maxy;
	int		c;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		prev_cursor_row;
	int		first_row = 0;
	int		i;
	char   str[120];
	int		style = STYLE;
	DataDesc		desc;
	ScrDesc			scrdesc;
	int		columns = -1;			/* default will be 1 if screen width will be enough */
	int		fixedRows = -1;			/* detect automaticly */
	FILE   *fp = NULL;

	int		opt;

	while ((opt = getopt(argc, argv, "bs:c:df:")) != -1)
	{
		int		n;

		switch (opt)
		{
			case 'b':
				style = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > 2)
				{
					fprintf(stderr, "Only color schemas 0, 1, 2 are supported.\n");
					exit(EXIT_FAILURE);
				}
				style = n;
				break;
			case 'c':
				n = atoi(optarg);
				if (n < 0 || n > 4)
				{
					fprintf(stderr, "fixed columns should be between 0 and 4.\n");
					exit(EXIT_FAILURE);
				}
				columns = n;
				break;
			case 'd':
				fp = fopen(FILENAME, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s", FILENAME);
					exit(1);
				}
				break;
			case 'f':
				fp = fopen(optarg, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s", FILENAME);
					exit(1);
				}
				break;
			default:
				fprintf(stderr, "Usage: %s [-b] [-s n] [-c n] [file...]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	setlocale(LC_ALL, "");

	initscr();

	start_color();
	initialize_color_pairs(style);

	clear();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	memset(&scrdesc, sizeof(ScrDesc), 0);
	refresh_aux_windows(&scrdesc);
	getmaxyx(stdscr, maxy, maxx);


	rows = newpad(10000, 3000);
	desc.rows = rows;
	wbkgd(desc.rows, COLOR_PAIR(1));

	scrollok(rows, true);

	readfile(fp, &desc, &nrows, &ncols);
	rows = desc.rows;

	refresh_main_pads(&scrdesc, &desc, columns, fixedRows, cursor_col, first_row, style, maxx);

	if (scrdesc.theme == 2)
		wattron(scrdesc.top_bar, A_BOLD | COLOR_PAIR(7));
	if (desc.title[0] != '\0')
		mvwprintw(scrdesc.top_bar, 0, 0, "%s", desc.title);
	else if (desc.filename[0] != '\0')
		mvwprintw(scrdesc.top_bar, 0, 0, "%s", desc.filename);
	if (scrdesc.theme == 2)
		wattroff(scrdesc.top_bar, A_BOLD | COLOR_PAIR(7));

	mvwprintw(scrdesc.top_bar, 0, maxx - 35, "C%d: [%3d/%d] L: [%3d/%d] %4.0f%%  ", columns + 1, cursor_col + scrdesc.fix_cols_cols - 1, ncols, cursor_row, nrows - scrdesc.fix_rows_rows, (cursor_row+1)/((double) (nrows-scrdesc.fix_rows_rows + 1))*100.0);
	wrefresh(scrdesc.top_bar);

	refresh_cursor(cursor_row, -1, false, &desc, &scrdesc);

	while (true)
	{
		bool		refresh_scr = false;
		int			fixed_columns;

		refresh();

		/* width of displayed fixed columns cannot be larger than screen */
		fixed_columns = maxx > scrdesc.fix_cols_cols ? scrdesc.fix_cols_cols : maxx;

		if (scrdesc.luc)
			prefresh(scrdesc.luc, desc.title_rows, 0, 1, 0, scrdesc.fix_rows_rows - desc.title_rows, fixed_columns - 1);

		prefresh(rows, scrdesc.fix_rows_rows + first_row, scrdesc.fix_cols_cols + cursor_col, scrdesc.fix_rows_rows - desc.title_rows + 1, scrdesc.fix_cols_cols, maxy - 2, maxx - 1);

		if (scrdesc.fix_rows)
			prefresh(scrdesc.fix_rows, desc.title_rows, scrdesc.fix_cols_cols + cursor_col, 1, scrdesc.fix_cols_cols, scrdesc.fix_rows_rows - desc.title_rows, maxx - 1);

		if (scrdesc.fix_columns)
			prefresh(scrdesc.fix_columns, first_row + scrdesc.fix_rows_rows, 0, scrdesc.fix_rows_rows - desc.title_rows + 1, 0, maxy - 2, fixed_columns - 1);

		refresh();

		c =getch();
		if (c == 'q')
			break;

		prev_cursor_row = cursor_row;

		switch (c)
		{
			case KEY_UP:
				if (cursor_row > 0)
				{
					cursor_row -= 1;
					if (cursor_row < first_row)
						first_row = cursor_row;
				}
				break;

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
				columns = c - '0';
				cursor_col = 0;
				refresh_scr = true;
				break;

			case KEY_DOWN:
				{
					int		max_cursor_row;
					int		max_first_row;

					max_cursor_row = desc.last_row - scrdesc.fix_rows_rows - 1;
					if (++cursor_row > max_cursor_row)
						cursor_row = max_cursor_row;

					if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows + desc.title_rows - 3)
						first_row += 1;

					max_first_row = desc.last_row - maxy + 2 - desc.title_rows;
					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case KEY_LEFT:
				{
					int		slen;
					int		nchars = 0;

					if (cursor_col == 0)
						break;

					for (i = cursor_col - 1; i >= 0; i--)
					{
						slen = mvwinnstr(desc.rows, desc.border_head_row, scrdesc.fix_cols_cols + i - 1, str, 4);

						/* when we didn't find separator in limit */
						if (++nchars > 30)
							break;

						if (!isDataPosChar(str))
						{
							if (nchars == 1)
								continue;

							break;
						}
					}

					cursor_col -= nchars;
					if (cursor_col < 3)
						cursor_col = 0;
				}
				break;

			case KEY_RIGHT:
				{
					int		slen;
					char   *ptr;
					int		nchar = 0;
					int		max_cursor_col;

					slen = mvwinnstr(desc.rows, desc.border_head_row, scrdesc.fix_cols_cols + cursor_col, str, 120);

					for(ptr = str; ptr < str + slen; )
					{
						if (++nchar > 30)
							break;

						if (!isDataPosChar(ptr))
							break;

						ptr += utf8charlen(*ptr);
					}

					cursor_col += nchar;

					if (desc.headline != NULL)
						max_cursor_col = desc.headline_char_size - maxx + 1;
					else
						max_cursor_col = desc.maxx - maxx - 1;

					max_cursor_col = max_cursor_col > 0 ? max_cursor_col : 0;

					if (cursor_col > max_cursor_col)
						cursor_col = max_cursor_col;
				}
				break;

			case 538:		/* CTRL HOME */
				cursor_row = 0;
				first_row = 0;
				break;

			case 533:		/* CTRL END */
				cursor_row = desc.last_row - scrdesc.fix_rows_rows - 1;
				first_row = desc.last_row - maxy + 2 - desc.title_rows;
				break;

			case KEY_PPAGE:
				if (first_row > 0)
				{
					first_row -= maxy - 4;
					if (first_row < 0)
						first_row = 0;
				}
				if (cursor_row > 0)
				{
					cursor_row -= maxy - 4;
					if (cursor_row < 0)
						cursor_row = 0;
				}
				break;

			case KEY_NPAGE:
				{
					int		max_cursor_row;
					int		max_first_row;

					first_row += maxy - scrdesc.fix_rows_rows - 2;
					cursor_row += maxy - scrdesc.fix_rows_rows - 2;

					max_cursor_row = desc.last_row - scrdesc.fix_rows_rows - 1;
					if (cursor_row > max_cursor_row)
						cursor_row = max_cursor_row;

					if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows + desc.title_rows - 3)
						first_row += 1;

					max_first_row = desc.last_row - maxy + 2 - desc.title_rows;
					if (max_first_row < 0)
						max_first_row = 0;
					if (first_row > max_first_row)
						first_row = max_first_row;
				}
				break;

			case KEY_RESIZE:
				refresh_scr = true;
			break;

			case KEY_HOME:
				cursor_col = 0;
				break;

			case KEY_END:
					if (desc.headline != NULL)
						cursor_col = desc.headline_char_size - maxx + 1;
					else
						cursor_col = desc.maxx - maxx - 1;

					cursor_col = cursor_col > 0 ? cursor_col : 0;
				break;
		}

		if (scrdesc.theme == 2)
			wattron(scrdesc.top_bar, A_BOLD | COLOR_PAIR(7));
		if (desc.title[0] != '\0')
			mvwprintw(scrdesc.top_bar, 0, 0, "%s", desc.title);
		else if (desc.filename[0] != '\0')
			mvwprintw(scrdesc.top_bar, 0, 0, "%s", desc.filename);
		if (scrdesc.theme == 2)
			wattroff(scrdesc.top_bar, A_BOLD | COLOR_PAIR(7));

		mvwprintw(scrdesc.top_bar, 0, maxx - 35, "C%d: [%3d/%d] L: [%3d/%d] %4.0f%%  ", columns + 1, cursor_col + scrdesc.fix_cols_cols - 1, ncols, cursor_row, nrows - scrdesc.fix_rows_rows, (cursor_row+1)/((double) (nrows-scrdesc.fix_rows_rows + 1))*100.0);
		wrefresh(scrdesc.top_bar);

		refresh_cursor(cursor_row, prev_cursor_row, refresh_scr, &desc, &scrdesc);

		if (refresh_scr)
		{
			endwin();
			refresh();
			clear();
			getmaxyx(stdscr, maxy, maxx);
			refresh_aux_windows(&scrdesc);

			mvwprintw(scrdesc.top_bar, 0, maxx - 35, "C%d: [%3d/%d] L: [%3d/%d] %4.0f%%  ", columns + 1, cursor_col + scrdesc.fix_cols_cols - 1, ncols, cursor_row, nrows - scrdesc.fix_rows_rows, (cursor_row+1)/((double) (nrows-scrdesc.fix_rows_rows + 1))*100.0);
			wrefresh(scrdesc.top_bar);

			refresh_main_pads(&scrdesc, &desc, columns, fixedRows, cursor_col, first_row, style, maxx);
			refresh_cursor(cursor_row, prev_cursor_row, true, &desc, &scrdesc);
			refresh_scr = false;
		}
	}

	endwin();
}
