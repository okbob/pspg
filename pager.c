#include <stdio.h>
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#define FILENAME		"pg_class.txt"
#define FIX_ROWS		3
#define FIX_COLS		44

static size_t
utf8len(char *s)
{
    size_t len = 0;
    for (; *s; ++s) if ((*s & 0xC0) != 0x80) ++len;
    return len;
}

static int
readfile(WINDOW *pad, int *rows, int *cols)
{
	FILE	   *fp;
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	int			maxx, maxy;


	fp = fopen(FILENAME, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "cannot to read file");
		exit(1);
	}

	*cols = -1;

	getmaxyx(pad, maxx, maxy);

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		nmaxx, nmaxy;
		int		clen = utf8len(line);

		if ((int) clen > *cols)
			*cols = clen;

		nmaxx = len > maxx ? len : maxx;
		nmaxy = nrows > maxy ? nrows * 2 : maxy;

		if (nmaxx > maxx || nmaxy > maxy)
		{
			wresize(pad, nmaxy, nmaxx);
			getmaxyx(pad, maxx, maxy);
		}

		mvwprintw(pad, nrows++, 0, "%s", line);
	}

	fclose(fp);

	*rows = nrows;
	

	return 0;
}

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

int main()
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

	const char	   *col1 = "\342\224\202";
	const char	   *col2 = "\342\225\221";
	const char	   *col3 = "|";

	setlocale(LC_ALL, "");

	initscr();

	start_color();
	use_default_colors();

	init_pair(1, -1, -1);
	init_pair(2, COLOR_BLACK, COLOR_WHITE);
	init_pair(3, COLOR_WHITE, COLOR_BLACK);

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

	rows = newpad(200, maxx);
	wbkgd(rows, COLOR_PAIR(1));

	scrollok(rows, true);

	readfile(rows, &nrows, &ncols);

	mvwprintw(topbar, 0, maxx - 35, "C1: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
	wrefresh(topbar);

	fixluc = newwin(FIX_ROWS, FIX_COLS+1, 1, 0);
	copywin(rows, fixluc, 0, 0, 0, 0, FIX_ROWS-1, FIX_COLS, false);
	mvwchgat(fixluc, 1, 1, FIX_COLS - 1, A_BOLD, 0, 0);
	wrefresh(fixluc);

	fixcols = newpad(nrows+1, FIX_COLS+1);
	copywin(rows, fixcols, 0, 0, 0, 0, nrows, FIX_COLS, false);
	for (i = 0; i < nrows; i++)
		mvwchgat(fixcols, i, 1, FIX_COLS - 2, A_BOLD, 0, 0);

	fixrows = newpad(FIX_ROWS + 1, ncols+1);
	copywin(rows, fixrows, 0, FIX_COLS+1, 0, 0, FIX_ROWS, ncols - FIX_COLS, false);

	/* set column labels attributes in fixrows */
	for (i = 0; i < ncols; i++)
	{
		int		strlen;

		strlen = mvwinnstr(fixrows, 1, i, str, 4);

		/* skip column separators */
		if (strncmp(str, col1, 3) == 0 ||
			strncmp(str, col2, 3) == 0 ||
			strncmp(str, col3, 1) == 0)
		{
			continue;
		}

		mvwchgat(fixrows, 1, i, 1, A_BOLD, 0, 0);
	}

	mvwchgat(rows, cursor_row + FIX_ROWS, 0, -1, 0, 2, 0);
	mvwchgat(fixcols, cursor_row + FIX_ROWS, 0, -1, 0, 2, 0);

	prefresh(fixcols, FIX_ROWS, 0, FIX_ROWS + 1, 0, maxy - 2, FIX_COLS);
	prefresh(fixrows, 0, 0, 1, FIX_COLS+1, FIX_ROWS, maxx - 1);
	prefresh(rows, FIX_ROWS, FIX_COLS, FIX_ROWS + 1, FIX_COLS, maxy - 2, maxx - 1);

	while ((c = getch()) != 'q')
	{
		bool		refresh_scr = false;

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

		}

		mvwprintw(topbar, 0, maxx - 35, "C1: [%3d/%d] L: [%3d/%d] %4.0f%%  ", cursor_col + FIX_COLS, ncols, cursor_row, nrows - FIX_ROWS, (cursor_row+1)/((double) (nrows-FIX_ROWS + 1))*100.0);
		wrefresh(topbar);

		if (prev_cursor_row != cursor_row || refresh_scr)
		{
			mvwchgat(rows, prev_cursor_row + FIX_ROWS, 0, -1, 0, 1, 0);
			mvwchgat(rows, cursor_row + FIX_ROWS, 0, -1, 0, 2, 0);

			mvwchgat(fixcols, prev_cursor_row + FIX_ROWS, 1, - 1, A_BOLD, 1, 0);
			mvwchgat(fixcols, prev_cursor_row + FIX_ROWS, 0, 1, 0, 1, 0);
			mvwchgat(fixcols, cursor_row + FIX_ROWS, 0, -1, A_BOLD, 2, 0);
		}

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

		prefresh(fixcols, first_row + FIX_ROWS, 0, FIX_ROWS + 1, 0, maxy - 2, FIX_COLS);
		prefresh(fixrows, 0, cursor_col, 1, FIX_COLS+1, FIX_ROWS, maxx - 1);
		prefresh(rows, FIX_ROWS + first_row, FIX_COLS+cursor_col+1, FIX_ROWS + 1, FIX_COLS+1, maxy - 2, maxx - 1);

	}

	endwin();
}
