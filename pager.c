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

/*
 * Columns separators, used for searching. Usually we don't like to
 * bold these symbols.
 */
const char	   *col1 = "\342\224\202";
const char	   *col2 = "\342\225\221";
const char	   *col3 = "|";

/*
 * Returns length of utf8 string in chars.
 */
static size_t
utf8len(char *s)
{
    size_t len = 0;
    for (; *s; ++s) if ((*s & 0xC0) != 0x80) ++len;
    return len;
}

/*
 * Returns length of utf8 char in bytes
 */
static int
utf8charlen(char ch)
{
    if ((ch & 0xF0) == 0xF0) {
        return 4;
    }
    else if ((ch & 0xE0) == 0xE0) {
        return 3;
    }
    else if ((ch & 0xC0) == 0xC0) {
        return 2;
    }
    return 1;
}

/*
 * Read data from file and fill ncurses pad. Increase
 * pad when it it necessary
 */
static int
readfile(WINDOW **pad, int *rows, int *cols)
{
	FILE	   *fp;
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	int			maxx, maxy;
	WINDOW	   *pp;

	pp = *pad;

	fp = fopen(FILENAME, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "cannot to read file");
		exit(1);
	}

	*cols = -1;

	getmaxyx(pp, maxy, maxx);

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		nmaxx, nmaxy;
		int		clen = utf8len(line);

		if ((int) clen > *cols)
			*cols = clen;

		nmaxx = len > maxx ? len + 10 : maxx;
		nmaxy = nrows > maxy - 1 ? nrows  * 2 : maxy;

		if (nmaxx > maxx || nmaxy > maxy)
		{
			WINDOW *np = newpad(nmaxy + 10, nmaxx);

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

	*rows = nrows;
	*pad = pp;

	return 0;
}

/*
 * Set bold attributes for char in line. Skip border chars
 */
static void
set_bold_row(WINDOW *win, int nrow,
			 int maxx1, int cp1,
			 int maxx2, int cp2,
			 int cp3)
{
	int		i;
	char	str[20];

	for (i = 0; i < maxx2; i++)
	{
		int		strlen;

		strlen = mvwinnstr(win, nrow, i, str, 4);

		/* skip column separators */
		if (strncmp(str, col1, 3) == 0 ||
				strncmp(str, col2, 3) == 0 ||
				strncmp(str, col3, 1) == 0)
			mvwchgat(win, nrow, i, 1, 0, cp3, 0);
		else
			mvwchgat(win, nrow, i, 1, A_BOLD, i < maxx1 ? cp1 : cp2, 0);
	}
}

/*
 * Draw cursor
 */
static void
refresh_cursor(WINDOW *fixcols, WINDOW *rows,
			   int nrow, int prevrow,
			   int style, bool force)
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
			if (style == 2)
				set_bold_row(rows, prevrow + FIX_ROWS, 0, 0, rows_maxx, 1, 1);
			else
				mvwchgat(rows, prevrow + FIX_ROWS, 0, -1, 0, 1, 0);

			set_bold_row(fixcols, prevrow + FIX_ROWS, 0, 0, fixc_maxx, 4, 1);
		}

		set_bold_row(rows, nrow + FIX_ROWS, 0, 0, rows_maxx, 6, style == 2 ? 1 : 6);
		set_bold_row(fixcols, nrow + FIX_ROWS, 0, 0, fixc_maxx, 6, style == 2 ? 1 : 6);
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
	int		style = 1;

	setlocale(LC_ALL, "");

	initscr();

	start_color();
	use_default_colors();

	if (style == 0)
	{
		use_default_colors();

		init_pair(1, -1, -1);							/* default */
		init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, -1, -1);							/* fix rows, columns */
		init_pair(5, -1, -1);							
		init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
	}
	else if (style == 1)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(1, -1, -1);
		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_YELLOW, COLOR_BLUE);
		init_pair(5, COLOR_CYAN, COLOR_BLUE);
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

	wbkgd(rows, COLOR_PAIR(1));

	scrollok(rows, true);

	readfile(&rows, &nrows, &ncols);
	for (i = 0; i <= nrows; i++)
	{
		if (i + 1 == BORDER_TOP_ROW ||
			i + 1 == BORDER_HEAD_ROW ||
			i + 1 == BORDER_BOOTOM_ROW)
			continue;

			if (i < BORDER_HEAD_ROW)
			{
				set_bold_row(rows, i, 0, 0, ncols, 4, 1);
			}
			else
			{
				set_bold_row(rows, i, FIX_COLS, 4, style != 2 ? FIX_COLS : ncols, 1, 1);
			}
	}

	mvwprintw(topbar, 0, maxx - 35, "C1: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
	wrefresh(topbar);

	fixluc = newwin(FIX_ROWS, FIX_COLS+1, 1, 0);
	copywin(rows, fixluc, 0, 0, 0, 0, FIX_ROWS-1, FIX_COLS, false);
	wrefresh(fixluc);

	fixcols = newpad(nrows+1, FIX_COLS+1);
	copywin(rows, fixcols, 0, 0, 0, 0, nrows, FIX_COLS, false);

	fixrows = newpad(FIX_ROWS + 1, ncols+1);
	copywin(rows, fixrows, 0, FIX_COLS+1, 0, 0, FIX_ROWS, ncols - FIX_COLS, false);

	refresh_cursor(fixcols, rows, cursor_row, -1, style, false);

	while (true)
	{
		bool		refresh_scr = false;

		prefresh(fixcols, first_row + FIX_ROWS, 0, FIX_ROWS + 1, 0, maxy - 2, FIX_COLS);
		prefresh(fixrows, 0, cursor_col, 1, FIX_COLS+1, FIX_ROWS, maxx - 1);
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
						{
								first_row += 1;
						}
					}
				break;

			case KEY_LEFT:
				{
					int		strlen;
					int		nchars = 0;
					bool	found = false;

					if (cursor_col == 0)
						break;

					for (i = cursor_col - 1; i >= 0; i--)
					{
						strlen = mvwinnstr(fixrows, 1, i, str, 4);

						/* when we didn't find separator in limit */
						if (++nchars > 20)
							break;

						if (strncmp(str, col1, 3) == 0 ||
							strncmp(str, col2, 3) == 0 ||
							strncmp(str, col3, 1) == 0)
						{
							if (nchars == 1)
							{
								/*
								 * ignore this position, it means so this column
								 * was aligned cleanly.
								 */
								continue;
							}
							else
							{
								found = true;
								break;
							}
						}
					}

					cursor_col -= nchars - 1;
					if (cursor_col < 3)
						cursor_col = 0;
				}
				break;

			case KEY_RIGHT:
				{
					int		strlen;
					int		nchars = 0;

					strlen = mvwinnstr(fixrows, 1, cursor_col, str, 30);
					for(i = 0; i < strlen; i += utf8charlen(str[i]))
					{
						if (++nchars > 20)
							break;

						cursor_col++;
						if (strncmp(&str[i], col1, 3) == 0 ||
							strncmp(&str[i], col2, 3) == 0 ||
							strncmp(&str[i], col3, 1) == 0)
						{
							break;
						}
					}

				if (cursor_col + maxx  > ncols - 1)
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

		mvwprintw(topbar, 0, maxx - 35, "C1: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
		wrefresh(topbar);

		refresh_cursor(fixcols, rows, cursor_row, prev_cursor_row, style, refresh_scr);

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
