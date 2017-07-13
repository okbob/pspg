#include <stdio.h>
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#define FILENAME		"pg_class.txt"
#define FIX_ROWS		3
#define FIX_COLS		44
#define BORDER_TOP_ROW		1
#define BORDER_HEAD_ROW		FIX_ROWS
#define BORDER_BOOTOM_ROW	345
#define STYLE			1

/*
 * Columns separators, used for searching. Usually we don't like to
 * bold these symbols.
 */
const char	   *col1 = "\342\224\202";
const char	   *col2 = "\342\225\221";
const char	   *col3 = "|";

typedef struct {
  int		border_top_row;			/* nrow of bootom outer border or -1 */
  int		border_head_row;		/* nrow of head outer (required) */
  int		border_bottom_row;		/* nrow of bottom outer border or -1 */
  int		border_style;			/* detected PostgreSQL border style */
  char		title[64];				/* detected title (trimmed) or NULL */
  char	   *filename[64];			/* parsed filename */
  WINDOW   *rows;					/* pointer to holding ncurses pad */
  int		maxy;					/* maxy of used pad area with data */
  int		maxx;					/* maxx of used pad area with data */
  int		maxbytes;				/* max length of line in bytes */
  char	   *headline;				/* header separator line */
  int		headline_size;			/* size of headerline in bytes */
  int		last_data_row;			/* last line of data row */
} DataDesc;

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
 * Set color pairs based on style
 */
static void
initialize_color_pairs(int style)
{
	if (style == 0)
	{
		use_default_colors();

		init_pair(1, -1, -1);							/* default */
		init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, -1, -1);							/* fix rows, columns */
		init_pair(5, COLOR_BLACK, COLOR_WHITE);			/* active cursor over fixed cols */
		init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
	}
	else if (style == 1)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(1, -1, -1);
		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_YELLOW, COLOR_BLUE);
		init_pair(5, COLOR_YELLOW, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
	}
	else if (style == 2)
	{
		assume_default_colors(COLOR_WHITE, COLOR_CYAN);

		init_pair(1, -1, -1);
		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_BLACK, COLOR_WHITE);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
	}
}

/*
 * Read data from file and fill ncurses pad. Increase
 * pad when it it necessary
 */
static int
readfile(DataDesc *desc , int *rows, int *cols)
{
	FILE	   *fp;
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	int			maxx, maxy;
	WINDOW	   *pp;

	pp = desc->rows;

	fp = fopen(FILENAME, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "cannot to read file");
		exit(1);
	}

	desc->title[0] = '\0';
	desc->filename[0] = '\0';
	desc->border_top_row = BORDER_TOP_ROW - 1;
	desc->border_head_row = BORDER_HEAD_ROW - 1;
	desc->border_bottom_row = BORDER_BOOTOM_ROW - 1;
	desc->last_data_row = BORDER_BOOTOM_ROW - 2;

	desc->maxbytes = -1;

	*cols = -1;

	getmaxyx(pp, maxy, maxx);

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		nmaxx, nmaxy;
		int		clen = utf8len(line);

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > *cols)
		{
			*cols = clen;
			desc->maxx = clen;
		}

		nmaxx = clen > maxx ? clen + 100 : maxx;
		nmaxy = nrows > maxy - 1 ? nrows  * 2 : maxy;

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

		mvwprintw(pp, nrows++, 0, "%s", line);
	}

	fclose(fp);

	desc->rows = pp;
	desc->maxy = nrows - 1;

	desc->headline = malloc(desc->maxbytes + 1);
	desc->headline_size = mvwinnstr(desc->rows, desc->border_head_row, 0, desc->headline, desc->maxbytes);

	*rows = nrows;

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

/*
 * Draw cursor
 */
static void
refresh_cursor(WINDOW *fixcols, WINDOW *rows,
			   int nrow, int prevrow,
			   int style, bool force,
			   DataDesc *desc)
{
	if (prevrow != nrow || force)
	{
		int		rows_maxy, rows_maxx;
		int		fixc_maxy, fixc_maxx;

		getmaxyx(rows, rows_maxy, rows_maxx);
		getmaxyx(fixcols, fixc_maxy, fixc_maxx);

		/* clean prev cursor */
		if (prevrow != -1)
		{
			if (prevrow + FIX_ROWS > desc->last_data_row)
			{
				mvwchgat(rows, prevrow + FIX_ROWS, 0, -1, 0, 1, 0);
				mvwchgat(fixcols, prevrow + FIX_ROWS, 0, -1, 0, 1, 0);
			}
			else
			{
				if (style == 2)
					set_bold_row(rows, prevrow + FIX_ROWS, desc, 0, rows_maxx, 1, rows_maxx, 0, 1);
				else
					mvwchgat(rows, prevrow + FIX_ROWS, 0, -1, 0, 1, 0);

				set_bold_row(fixcols, prevrow + FIX_ROWS, desc, 0, fixc_maxx, 4, fixc_maxx, 0, 1);
			}
		}

		if (nrow + FIX_ROWS > desc->last_data_row)
		{
			mvwchgat(rows, nrow + FIX_ROWS, 0, -1, 0, 6, 0);
			mvwchgat(fixcols, nrow + FIX_ROWS, 0, -1, 0, 6, 0);
		}
		else
		{
			set_bold_row(rows, nrow + FIX_ROWS, desc, 0, rows_maxx, 6, rows_maxx, 0, style == 2 ? 1 : 6);
			set_bold_row(fixcols, nrow + FIX_ROWS, desc, 0, fixc_maxx, 5, fixc_maxx, 0, style == 2 ? 1 : 6);
		}
	}
}

int
main()
{
	WINDOW *rows;
	WINDOW *fixluc;
	WINDOW *fixrows;
	WINDOW *fixcols;
	WINDOW *topbar;
	WINDOW *botbar;
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
	DataDesc	   desc;

	setlocale(LC_ALL, "");

	initscr();

	start_color();
	initialize_color_pairs(style);

	clear();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	refresh();
	getmaxyx(stdscr, maxy, maxx);

	topbar = newwin(1, 0, 0, 0);
	wbkgd(topbar, COLOR_PAIR(2));
	wrefresh(topbar);

	botbar = newwin(1, 0, maxy - 1, 0);

	wattron(botbar, A_BOLD | COLOR_PAIR(3));
	mvwaddstr(botbar, 0, 1, "Q");
	wattroff(botbar, A_BOLD | COLOR_PAIR(3));
	wattron(botbar, COLOR_PAIR(2));
	mvwprintw(botbar, 0, 2, "%-4s", "uit");
	wattroff(botbar, COLOR_PAIR(2));
	wrefresh(botbar);

	rows = newpad(100, maxx);
	desc.rows = rows;

	wbkgd(desc.rows, COLOR_PAIR(1));

	scrollok(rows, true);

	readfile(&desc, &nrows, &ncols);
	rows = desc.rows;

	for (i = 0; i <= nrows; i++)
	{
		if (i == desc.border_top_row ||
			i == desc.border_head_row ||
			i == desc.border_bottom_row)
			continue;

			if (i < FIX_ROWS)
			{
				set_bold_row(rows, i, &desc, 0, ncols, 4, ncols, 4, 1);
			}
			else
			{
				set_bold_row(rows, i, &desc, 0, style != 2 ? FIX_COLS : ncols, 4, ncols, 1, 1);
			}
	}

	mvwprintw(topbar, 0, maxx - 35, "C2: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
	wrefresh(topbar);

	fixluc = newwin(FIX_ROWS, FIX_COLS+1, 1, 0);
	copywin(rows, fixluc, 0, 0, 0, 0, FIX_ROWS-1, FIX_COLS, false);
	wrefresh(fixluc);

	fixcols = newpad(nrows+1, FIX_COLS+1);
	copywin(rows, fixcols, 0, 0, 0, 0, nrows, FIX_COLS, false);

	fixrows = newpad(FIX_ROWS + 1, ncols+1);
	copywin(rows, fixrows, 0, 0, 0, 0, FIX_ROWS, desc.maxx, false);

	refresh_cursor(fixcols, rows, cursor_row, -1, style, false, &desc);

	while (true)
	{
		bool		refresh_scr = false;

		prefresh(fixcols, first_row + FIX_ROWS, 0, FIX_ROWS + 1, 0, maxy - 2, FIX_COLS);
		prefresh(fixrows, 0, FIX_COLS + cursor_col + 1, 1, FIX_COLS+1, FIX_ROWS, maxx - 1);
		prefresh(rows, FIX_ROWS + first_row, FIX_COLS+cursor_col+1, FIX_ROWS + 1, FIX_COLS+1, maxy - 2, maxx - 1);

		c = getch();
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

			case KEY_DOWN:
					if (cursor_row < nrows - FIX_ROWS)
					{
						cursor_row += 1;
						if (cursor_row - first_row > maxy - FIX_ROWS - 3)
								first_row += 1;
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
						slen = mvwinnstr(desc.rows, desc.border_head_row, FIX_COLS + i, str, 4);

						/* when we didn't find separator in limit */
						if (++nchars > 20)
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

					slen = mvwinnstr(desc.rows, desc.border_head_row, FIX_COLS + cursor_col + 1, str, 80);

					for(ptr = str; ptr < str + slen; )
					{
						if (++nchar > 20)
							break;

						if (!isDataPosChar(ptr))
							break;

						ptr += utf8charlen(*ptr);
					}

					cursor_col += nchar;

					if (cursor_col + maxx > ncols - 1)
						cursor_col = ncols - maxx - 1;
				}
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
				first_row += maxy - 4;
				cursor_row += maxy - 4;

				if (cursor_row > nrows - FIX_ROWS)
					cursor_row = nrows - FIX_ROWS;

				if (first_row + maxy - 5 > nrows - FIX_ROWS)
					first_row = nrows - FIX_ROWS - maxy + 6;
				break;

			case KEY_RESIZE:
				refresh();
				getmaxyx(stdscr, maxy, maxx);

				refresh_scr = true;
			break;

			case KEY_HOME:
				cursor_col = 0;
				break;

			case KEY_END:
				cursor_col = ncols - maxx - 1;
				break;
		}

		mvwprintw(topbar, 0, maxx - 35, "C2: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
		wrefresh(topbar);

		refresh_cursor(fixcols, rows, cursor_row, prev_cursor_row, style, refresh_scr, &desc);

		if (refresh_scr)
		{
			delwin(topbar);
			topbar = newwin(1, 0, 0, 0);
			wbkgd(topbar, COLOR_PAIR(2));
			wrefresh(topbar);

			delwin(botbar);
			botbar = newwin(1, 0, maxy - 1, 0);

			wattron(botbar, A_BOLD | COLOR_PAIR(3));
			mvwaddstr(botbar, 0, 1, "Q");
			wattroff(botbar, A_BOLD | COLOR_PAIR(3));
			wattron(botbar, COLOR_PAIR(2));
			mvwprintw(botbar, 0, 2, "%-4s", "uit");
			wattroff(botbar, COLOR_PAIR(2));
			wrefresh(botbar);

			delwin(fixluc);
			fixluc = newwin(FIX_ROWS, FIX_COLS+1, 1, 0);
			copywin(rows, fixluc, 0, 0, 0, 0, FIX_ROWS-1, FIX_COLS, false);
			mvwchgat(fixluc, 1, 1, FIX_COLS - 1, A_BOLD, 0, 0);
			wrefresh(fixluc);
		}
	}

	endwin();
}
