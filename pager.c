#ifdef __FreeBSD__
#define _WITH_GETLINE
#include <ncurses/curses.h>
#else
#include <curses.h>
#endif
#include <errno.h>
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

//#define COLORIZED_NO_ALTERNATE_SCREEN

typedef struct LineBuffer
{
	int		first_row;
	int		nrows;
	char   *rows[1000];
	struct LineBuffer *next;
} LineBuffer;

/*
 * Available formats of headline chars
 *
 *  L, R   .. outer border
 *  I      .. inner border
 *  d      .. data
 */

/*
 * This structure should be immutable
 */
typedef struct
{
	int		border_top_row;			/* nrow of bootom outer border or -1 */
	int		border_head_row;		/* nrow of head outer (required) */
	int		border_bottom_row;		/* nrow of bottom outer border or -1 */
	int		border_type;			/* detected type of border: 0, 1, 2 */
	char	linestyle;				/* detected linestyle: a, u */
	bool	is_expanded_mode;		/* true when data are in expanded mode */
	int		expanded_info_minx;		/* begin of info in \x mode .. RECORD x */
	char	title[65];				/* detected title (trimmed) or NULL */
	int		title_rows;				/* number of rows used as table title (skipped later) */
	char	filename[65];			/* filename (printed on top bar) */
	LineBuffer rows;				/* list of rows buffers */
	int		maxy;					/* maxy of used pad area with data */
	int		maxx;					/* maxx of used pad area with data */
	LineBuffer footer;				/* footer rows */
	int		footer_maxy;			/* maxy of used pad area for footer */
	int		footer_maxx;			/* maxx of used pad area for footer */
	int		maxbytes;				/* max length of line in bytes */
	char   *headline;				/* header separator line */
	int		headline_size;			/* size of headerline in bytes */
	char   *headline_transl;		/* translated headline */
	int		headline_char_size;		/* size of headerline in chars */
	int		last_data_row;			/* last line of data row */
	int		last_row;				/* last not empty row */
} DataDesc;

/*
 * This structure can be muttable - depends on displayed data
 */
typedef struct
{
	int		fix_rows_rows;			/* number of fixed rows in window rows */
	int		fix_cols_cols;			/* number of fixed colums in window rows */
	int		maxy;					/* max y size of screen */
	int		maxx;					/* max x size of screen */
	WINDOW *luc;					/* window for left upper corner */
	WINDOW *fix_rows;				/* window for fixed rows */
	WINDOW *fix_cols;				/* window for fixed columns */
	WINDOW *rows;					/* window for data */
	WINDOW *footer;					/* window for footer */
	WINDOW *top_bar;				/* top bar window */
	WINDOW *bottom_bar;				/* bottom bar window */
	int		cursor_y;				/* y pos of virtual cursor */
	int		cursor_x;				/* x pos of virtual cursor */
	int		theme;					/* color theme number */
	char	searchterm[256];		/* currently active search input */
} ScrDesc;

static int
min_int(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
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

#if NCURSES_MOUSE_VERSION > 1
#define MOUSE_WHEEL_BUTTONS			1
#endif

//#define DEBUG_COLORS				1

/*
 * Translate from UTF8 to semantic characters.
 */
static bool
translate_headline(DataDesc *desc)
{
	char   *transl;
	char   *srcptr;
	char   *destptr;
	char   *last_black_char = NULL;
	bool	broken_format = false;
	int		processed_chars = 0;
	bool	is_expanded_info = false;

	srcptr = desc->headline;
	destptr = malloc(desc->headline_size + 1);
	memset(destptr, 0, desc->headline_size + 1);
	desc->headline_transl = destptr;

	desc->linestyle = 'a';
	desc->border_type = 0;

	desc->expanded_info_minx = -1;

	while (*srcptr != '\0' && *srcptr != '\n' && *srcptr != '\r')
	{
		/* only spaces can be after known right border */
		if (last_black_char != NULL && *last_black_char == 'R')
		{
			if (*srcptr != ' ')
			{
				broken_format = true;
				break;
			}
		}

		if (*srcptr != ' ')
			last_black_char = destptr;

		if (desc->is_expanded_mode && *srcptr == '[')
		{
			if (desc->expanded_info_minx != -1)
			{
				broken_format = true;
				break;
			}

			/* entry to expanded info mode */
			is_expanded_info = true;
			desc->expanded_info_minx = processed_chars;

			*destptr++ = 'd';
			srcptr += utf8charlen(*srcptr);
		}
		else if (is_expanded_info)
		{
			if (*srcptr == ']')
			{
				is_expanded_info = false;
			}
			*destptr++ = 'd';
			srcptr += utf8charlen(*srcptr);
		}
		else if (strncmp(srcptr, "\342\224\214", 3) == 0 || /* ┌ */
		         strncmp(srcptr, "\342\225\224", 3) == 0)   /* ╔ */
		{
			/* should be expanded mode */
			if (processed_chars > 0 || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\220", 3) == 0 || /* ┐ */
		         strncmp(srcptr, "\342\225\227", 3) == 0)   /* ╗ */
		{
			if (desc->linestyle != 'u' || desc->border_type != 2 ||
				!desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\254", 3) == 0 || /* ┬╤ */
			     strncmp(srcptr, "\342\225\244", 3) == 0 ||
			     strncmp(srcptr, "\342\225\245", 3) == 0 || /* ╥╦ */
			     strncmp(srcptr, "\342\225\246", 3) == 0)
		{
			if (desc->linestyle != 'u' || !desc->is_expanded_mode)
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;

			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\234", 3) == 0 || /* ├╟ */
		         strncmp(srcptr, "\342\225\237", 3) == 0 ||
		         strncmp(srcptr, "\342\225\236", 3) == 0 || /* ╞╠ */
		         strncmp(srcptr, "\342\225\240", 3) == 0)
		{
			if (processed_chars > 0)
			{
				broken_format = true;
				break;
			}
			desc->linestyle = 'u';
			desc->border_type = 2;
			*destptr++ = 'L';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\244", 3) == 0 || /* ┤╢ */
		         strncmp(srcptr, "\342\225\242", 3) == 0 ||
		         strncmp(srcptr, "\342\225\241", 3) == 0 || /* ╡╣ */
		         strncmp(srcptr, "\342\225\243", 3) == 0)
		{
			if (desc->linestyle != 'u' || desc->border_type != 2)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'R';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\274", 3) == 0 || /* ┼╪ */
		         strncmp(srcptr, "\342\225\252", 3) == 0 ||
		         strncmp(srcptr, "\342\225\253", 3) == 0 || /* ╫╬ */
		         strncmp(srcptr, "\342\225\254", 3) == 0)
		{
			if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			if (desc->border_type == 0)
				desc->border_type = 1;
			*destptr++ = 'I';
			srcptr += 3;
		}
		else if (strncmp(srcptr, "\342\224\200", 3) == 0 || /* ─ */
		         strncmp(srcptr, "\342\225\220", 3) == 0) /* ═ */
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'u';
			}
			else if (desc->linestyle != 'u')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 3;
		}
		else if (*srcptr == '+')
		{
			if (processed_chars == 0)
			{
				*destptr++ = 'L';
				desc->linestyle = 'a';
				desc->border_type = 2;
			}
			else
			{
				if (desc->linestyle != 'a')
				{
					broken_format = true;
					break;
				}
				if (desc->border_type == 0)
					desc->border_type = 1;

				*destptr++ = (srcptr[1] == '-') ? 'I' : 'R';
			}
			srcptr += 1;
		}
		else if (*srcptr == '-')
		{
			if (processed_chars == 0)
			{
				desc->linestyle = 'a';
			}
			else if (desc->linestyle != 'a')
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'd';
			srcptr += 1;
		}
		else if (*srcptr == ' ')
		{
			if (desc->border_type != 0)
			{
				broken_format = true;
				break;
			}
			*destptr++ = 'I';
			srcptr += 1;
		}
		else
		{
			broken_format = true;
			break;
		}
		processed_chars += 1;
	}

	/* should not be - unclosed header */
	if (is_expanded_info)
		broken_format = true;
	else if (desc->is_expanded_mode && desc->expanded_info_minx == -1)
		broken_format = true;

	/* trim ending spaces */
	if (!broken_format && last_black_char != 0)
	{
		last_black_char[1] = '\0';
		desc->headline_char_size = strlen(desc->headline_transl);

		return true;
	}

	free(desc->headline_transl);
	desc->headline_transl = NULL;

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
 * detect different faces of headline in extended mode
 */
static bool
is_expanded_header(char *str, int *ei_minx, int *ei_maxx)
{
	int		pos = 0;

	if (*str == '+')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\214", 3) == 0 || /* ┌ */
	         strncmp(str, "\342\225\224", 3) == 0 || /* ╔ */
	         strncmp(str, "\342\224\234", 3) == 0 || /* ├╟ */
	         strncmp(str, "\342\225\237", 3) == 0 ||
	         strncmp(str, "\342\225\236", 3) == 0 || /* ╞╠ */
	         strncmp(str, "\342\225\240", 3) == 0)
	{
		str += 3;
		pos += 1;
	}

	if (*str == '-')
	{
		str += 1;
		pos += 1;
	}
	else if (strncmp(str, "\342\224\200", 3) == 0 || /* ─ */
	         strncmp(str, "\342\225\220", 3) == 0) /* ═ */
	{
		str += 3;
		pos += 1;
	}

	if (strncmp(str, "[ ", 2) != 0)
		return false;

	if (ei_minx != NULL && ei_maxx != NULL)
	{
		pos += 2;
		str += 2;
		*ei_minx = pos - 1;

		while (*str != ']' && *str != '\0')
		{
			pos += 1;
			str += utf8charlen(*str);
		}

		*ei_maxx = pos - 1;
	}

	return true;
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

		init_pair(2, COLOR_BLACK, COLOR_WHITE);			/* top bar colors */
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, -1, -1);							/* fix rows, columns */
		init_pair(5, COLOR_BLACK, COLOR_WHITE);			/* active cursor over fixed cols */
		init_pair(6, COLOR_BLACK, COLOR_WHITE);			/* active cursor */
		init_pair(8, COLOR_BLACK, COLOR_WHITE);			/* expanded header */
	}
	else if (theme == 1)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_YELLOW, COLOR_BLUE);
		init_pair(5, COLOR_YELLOW, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_RED, COLOR_BLUE);
	}
	else if (theme == 2)
	{
		assume_default_colors(COLOR_WHITE, COLOR_CYAN);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_BLACK, COLOR_WHITE);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(7, COLOR_YELLOW, COLOR_WHITE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 3)
	{
		assume_default_colors(COLOR_BLACK, COLOR_CYAN);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_WHITE, COLOR_CYAN);
		init_pair(5, COLOR_WHITE, COLOR_BLACK);
		init_pair(6, COLOR_CYAN, COLOR_BLACK);
		init_pair(8, COLOR_WHITE, COLOR_CYAN);
	}
	else if (theme == 4)
	{
		assume_default_colors(COLOR_BLACK, COLOR_WHITE);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_BLACK, COLOR_WHITE);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 5)
	{
		use_default_colors();

		init_pair(2, COLOR_GREEN, COLOR_BLUE);
		init_pair(3, COLOR_YELLOW, COLOR_WHITE);
		init_pair(4, COLOR_CYAN, -1);
		init_pair(5, COLOR_BLACK, COLOR_CYAN);
		init_pair(6, COLOR_BLACK, COLOR_CYAN);
		init_pair(8, COLOR_BLACK, COLOR_BLUE);
		init_pair(9, COLOR_BLACK, COLOR_CYAN);
	}
	else if (theme == 6)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLACK);

		init_pair(2, COLOR_BLACK, COLOR_CYAN);
		init_pair(3, COLOR_CYAN, COLOR_BLACK);
		init_pair(4, COLOR_CYAN, COLOR_BLACK);
		init_pair(5, COLOR_WHITE, COLOR_BLUE);
		init_pair(6, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 7)
	{
		assume_default_colors(COLOR_GREEN, COLOR_BLACK);

		init_pair(2, COLOR_CYAN, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_GREEN, COLOR_BLACK);
		init_pair(5, COLOR_WHITE, COLOR_GREEN);
		init_pair(6, COLOR_WHITE, COLOR_GREEN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 8)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_WHITE, COLOR_BLUE);
		init_pair(4, COLOR_WHITE, COLOR_BLUE);
		init_pair(5, COLOR_WHITE, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
	else if (theme == 9)
	{
		assume_default_colors(COLOR_WHITE, COLOR_BLUE);

		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		init_pair(4, COLOR_CYAN, COLOR_BLUE);
		init_pair(5, COLOR_WHITE, COLOR_CYAN);
		init_pair(6, COLOR_WHITE, COLOR_CYAN);
		init_pair(8, COLOR_WHITE, COLOR_BLUE);
	}
}

/*
 * Read data from file and fill ncurses pad. Increase
 * pad when it it necessary
 */
static int
readfile(FILE *fp, DataDesc *desc)
{
	char	   *line = NULL;
	size_t		len;
	ssize_t		read;
	int			nrows = 0;
	bool		use_stdin = false;
	LineBuffer *rows;
	LineBuffer *footer;

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
	desc->is_expanded_mode = false;
	desc->headline_transl = NULL;

	desc->maxbytes = -1;
	desc->maxx = -1;

	memset(&desc->rows, 0, sizeof(LineBuffer));
	rows = &desc->rows;

	errno = 0;

	while (( read = getline(&line, &len, fp)) != -1)
	{
		int		nmaxx, nmaxy;
		int		clen = utf8len(line);

		if (rows->nrows == 1000)
		{
			LineBuffer *newrows = malloc(sizeof(LineBuffer));
			memset(newrows, 0, sizeof(LineBuffer));
			rows->next = newrows;
			rows = newrows;
		}

		rows->rows[rows->nrows++] = line;

		/* save possible table name */
		if (nrows == 0 && !isTopLeftChar(line))
		{
			strncpytrim(desc->title, line, 63, len);
			desc->title_rows = 1;
		}

		if (desc->border_head_row == -1 && desc->border_top_row == -1 && isTopLeftChar(line))
		{
			desc->border_top_row = nrows;
			desc->is_expanded_mode = is_expanded_header(line, NULL, NULL);
		}
		else if (desc->border_head_row == -1 && isHeadLeftChar(line))
		{
			desc->border_head_row = nrows;

			if (!desc->is_expanded_mode)
				desc->is_expanded_mode = is_expanded_header(line, NULL, NULL);

			/* title surelly doesn't it there */
			if ((!desc->is_expanded_mode && nrows == 1) ||
			    (desc->is_expanded_mode && nrows == 0))
			{
				desc->title[0] = '\0';
				desc->title_rows = 0;
			}
		}
		else if (!desc->is_expanded_mode && desc->border_bottom_row == -1 && isBottomLeftChar(line))
		{
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}
		else if (desc->is_expanded_mode && isBottomLeftChar(line))
		{
			/* Outer border is repeated in expanded mode, use last detected row */
			desc->border_bottom_row = nrows;
			desc->last_data_row = nrows - 1;
		}

		if ((int) len > desc->maxbytes)
			desc->maxbytes = (int) len;

		if ((int) clen > desc->maxx + 1)
			desc->maxx = clen - 1;

		if ((int) clen > 0)
			desc->last_row = nrows;

		nrows += 1;
		line = NULL;
	}

	if (errno != 0)
	{
		fprintf(stderr, "cannot to read file: %s\n", strerror(errno));
		exit(1);
	}

	if (!use_stdin)
		fclose(fp);

	desc->maxy = nrows ;

	desc->headline_char_size = 0;

	if (desc->border_head_row != -1)
	{
		int		i = 0;

		desc->headline = desc->rows.rows[desc->border_head_row];
		desc->headline_size = strlen(desc->headline);

		if (desc->last_data_row == -1)
			desc->last_data_row = desc->last_row - 1;
	}
	else if (desc->is_expanded_mode && desc->border_top_row != -1)
	{
		desc->headline = desc->rows.rows[desc->border_top_row];
		desc->headline_size = strlen(desc->headline);
	}
	else
	{
		desc->headline = NULL;
		desc->headline_size = 0;
		desc->headline_char_size = 0;

		/* there are not a data set */
		desc->last_row = nrows;
		desc->last_data_row = nrows;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}

	freopen("/dev/tty", "rw", stdin);

	return 0;
}

static void
window_fill(WINDOW *win,
			int srcy, int srcx,				/* offset to displayed data */
			int cursor_row,					/* row of row cursor */
			DataDesc *desc,
			attr_t data_attr,				/* colors for data (alphanums) */
			attr_t line_attr,				/* colors for borders */
			attr_t expi_attr,				/* colors for expanded headers */
			attr_t cursor_data_attr,		/* colors for cursor on data positions */
			attr_t cursor_line_attr,		/* colors for cursor on border position */
			attr_t cursor_expi_attr)		/* colors for cursor on expanded headers */
{
	int			maxy, maxx;
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;

	/* fast leaving */
	if (win == NULL)
		return;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	getmaxyx(win, maxy, maxx);

	while (row < maxy )
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;
		bool		is_cursor_row;

		is_cursor_row = row == cursor_row;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
			rowstr = lnb->rows[lnb_row++];
		else
			rowstr = NULL;

		active_attr = is_cursor_row ? cursor_line_attr : line_attr;
		wattron(win, active_attr);

		wmove(win, row++, 0);

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
			}
			else
			{
				fix_line_attr_style = effective_row == desc->border_top_row ||
											effective_row == desc->border_head_row ||
											effective_row >= desc->border_bottom_row;
				is_expand_head = false;
			}

			/* skip first srcx chars */
			for (i = 0; i < srcx; i++)
			{
				if (*rowstr != '\0')
					rowstr += utf8charlen(*rowstr);
				else
					break;
			}

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0')
			{
				for (i = 0; i < maxx; i++)
				{
					if (is_expand_head)
					{
						int		pos = srcx + i;
						int		new_attr;

						if (is_cursor_row)
							new_attr = pos >= ei_min && pos <= ei_max ? cursor_expi_attr : cursor_line_attr;
						else
							new_attr = pos >= ei_min && pos <= ei_max ? expi_attr : line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
							}

							/* disable current style */
							wattroff(win, active_attr);

							/* active new style */
							active_attr = new_attr;
							wattron(win, active_attr);
						}
					}
					else if (!fix_line_attr_style && desc->headline_transl != NULL)
					{
						int htrpos = srcx + i;

						if (htrpos < desc->headline_char_size)
						{
							int		new_attr;

							if (is_cursor_row)
								new_attr = desc->headline_transl[htrpos] == 'd' ? cursor_data_attr : cursor_line_attr;
							else
								new_attr = desc->headline_transl[htrpos] == 'd' ? data_attr : line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									waddnstr(win, rowstr, bytes);
									rowstr += bytes;
									bytes = 0;
								}

								/* disable current style */
								wattroff(win, active_attr);

								/* active new style */
								active_attr = new_attr;
								wattron(win, active_attr);
							}
						}
					}

					if (*ptr != '\0')
					{
						int len  = utf8charlen(*ptr);
						ptr += len;
						bytes += len;
					}
					else
						break;
				}
			}
			else if (is_cursor_row)
				/* in this case i is not valid, but it is necessary for cursor line printing */
				i = 1;

			if (bytes > 0)
				waddnstr(win, rowstr, bytes);

			/* clean other chars on line */
			if (i < maxx)
				wclrtoeol(win);

			/* draw cursor line to screan end of line */
			if (is_cursor_row && i < maxx)
				mvwchgat(win, row - 1, i - 1, -1, 0, PAIR_NUMBER(cursor_data_attr), 0);
		}
		else
		{
			wclrtobot(win);
			break;
		}

		wattroff(win, active_attr);
	}
}

static void
ansi_colors(int pairno, short int *fc, short int *bc)
{
	pair_content(pairno, fc, bc);
	*fc = *fc != -1 ? *fc + 30 : 39;
	*bc = *bc != -1 ? *bc + 40 : 49;
}

static char *
ansi_attr(attr_t attr)
{
	static char result[20];
	int		pairno;
	short int fc, bc;

#ifndef COLORIZED_NO_ALTERNATE_SCREEN

	return "";

#else

	pairno = PAIR_NUMBER(attr);
	ansi_colors(pairno, &fc, &bc);

	if ((attr & A_BOLD) != 0)
	{
		snprintf(result, 20, "\e[1;%d;%dm", fc, bc);
	}
	else
		snprintf(result, 20, "\e[0;%d;%dm", fc, bc);

	return result;

#endif

}

/*
 * Print data to primary screen without ncurses
 */
static void
draw_rectange(int offsety, int offsetx,			/* y, x offset on screen */
			int maxy, int maxx,				/* size of visible rectangle */
			int srcy, int srcx,				/* offset to displayed data */
			DataDesc *desc,
			attr_t data_attr,				/* colors for data (alphanums) */
			attr_t line_attr,				/* colors for borders */
			attr_t expi_attr,				/* colors for expanded headers */
			bool clreoln)					/* force clear to eoln */
{
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	if (offsety)
		printf("\e[%dB", offsety);

	while (row < maxy )
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
			rowstr = lnb->rows[lnb_row++];
		else
			rowstr = NULL;

		active_attr = line_attr;
		printf("%s", ansi_attr(active_attr));

		row += 1;

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
			}
			else
			{
				fix_line_attr_style = effective_row == desc->border_top_row ||
											effective_row == desc->border_head_row ||
											effective_row >= desc->border_bottom_row;
				is_expand_head = false;
			}

			if (offsetx != 0)
				printf("\e[%dC", offsetx);

			/* skip first srcx chars */
			for (i = 0; i < srcx; i++)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
					rowstr += utf8charlen(*rowstr);
				else
					break;
			}

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0' && *ptr != '\n')
			{
				for (i = 0; i < maxx; i++)
				{
					if (is_expand_head)
					{
						int		pos = srcx + i;
						int		new_attr;

						new_attr = pos >= ei_min && pos <= ei_max ? expi_attr : line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								printf("%.*s", bytes, rowstr);
								rowstr += bytes;
								bytes = 0;
							}

							/* active new style */
							active_attr = new_attr;
							printf("%s", ansi_attr(active_attr));
						}
					}
					else if (!fix_line_attr_style && desc->headline_transl != NULL)
					{
						int htrpos = srcx + i;

						if (htrpos < desc->headline_char_size)
						{
							int		new_attr;

							new_attr = desc->headline_transl[htrpos] == 'd' ? data_attr : line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									//waddnstr(win, rowstr, bytes);
									printf("%.*s", bytes, rowstr);
									rowstr += bytes;
									bytes = 0;
								}

								/* active new style */
								active_attr = new_attr;
								printf("%s", ansi_attr(active_attr));
							}
						}
					}

					if (*ptr != '\0' && *ptr != '\n')
					{
						int len  = utf8charlen(*ptr);
						ptr += len;
						bytes += len;
					}
					else
						break;
				}
			}

			if (bytes > 0)
			{
				printf("%.*s", bytes, rowstr);
				if (clreoln)
					printf("\e[K");
				printf("\n");
			}
		}
		else
			break;
	}
}

static void
draw_data(ScrDesc *scrdesc, DataDesc *desc,
		  int first_row, int cursor_col)
{
	struct winsize size;
	int		i;
	short int		fc, bc;

	if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
	{

		for (i = 0; i < min_int(size.ws_row - 2, desc->last_row); i++)
			printf("\eD");

		/* Go wit cursor to up */
		printf("\e[%dA", min_int(size.ws_row - 2, desc->last_row));

		/* Save cursor */
		printf("\e[s");

		if (scrdesc->fix_cols_cols > 0)
		{
			draw_rectange(scrdesc->fix_rows_rows, 0,
						  min_int(size.ws_row - 2, desc->last_row)  - scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  scrdesc->fix_rows_rows + desc->title_rows + first_row, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);

			printf("\e[u\e[s");
		}

		if (scrdesc->fix_rows_rows > 0)
		{
			draw_rectange(0, scrdesc->fix_cols_cols,
						  scrdesc->fix_rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  desc->title_rows, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  true);

			printf("\e[u\e[s");
		}

		if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
		{
			draw_rectange(0, 0,
						  scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  desc->title_rows, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);

			printf("\e[u\e[s");
		}

		draw_rectange(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
					  min_int(size.ws_row - 2, desc->last_row) - scrdesc->fix_rows_rows, size.ws_col - scrdesc->fix_cols_cols,
					  scrdesc->fix_rows_rows + desc->title_rows + first_row, scrdesc->fix_cols_cols + cursor_col,
					  desc,
					  scrdesc->theme == 2 ? 0 | A_BOLD : 0,
					  scrdesc->theme == 2 && (desc->headline_transl == NULL) ? A_BOLD : 0,
					  COLOR_PAIR(8) | A_BOLD,
					  true);

		/* reset */
		printf("\e[0m\r");
	}
}


/*
 * Prepare dimensions of windows layout
 */
static void
create_layout_dimensions(ScrDesc *scrdesc, DataDesc *desc,
				   int fixCols, int fixRows,
				   int maxy, int maxx)
{
	scrdesc->maxy = maxy;
	scrdesc->maxx = maxx;

	scrdesc->fix_cols_cols = 0;

	if (fixCols == -1)
		fixCols = 1;

	/* search end of fixCol'th column */
	if (desc->headline_transl != NULL && fixCols > 0)
	{
		char   *c = desc->headline_transl;

		while (*c != 0)
		{
			if (*c == 'I' && --fixCols == 0)
			{
				scrdesc->fix_cols_cols = c - desc->headline_transl + 1;
				break;
			}
			c += 1;
		}
	}

	scrdesc->fix_rows_rows = 0;

	if (fixRows != -1)
	{
		scrdesc->fix_rows_rows = fixRows;
	}
	else if (!desc->is_expanded_mode && desc->border_head_row != -1)
	{
		scrdesc->fix_rows_rows = desc->border_head_row + 1 - desc->title_rows;
	}

	/* disable fixed parts when is not possible draw in screen */
	if (scrdesc->fix_cols_cols > maxx)
		scrdesc->fix_cols_cols = 0;

	if (scrdesc->fix_rows_rows > maxy)
		scrdesc->fix_rows_rows = 0;

	if (scrdesc->fix_rows_rows == 0 && !desc->is_expanded_mode)
	{
		scrdesc->fix_rows_rows = 0;
		desc->title_rows = 0;
		desc->title[0] = '\0';
	}
}

static void
create_layout(ScrDesc *scrdesc, DataDesc *desc)
{
	if (scrdesc->luc != NULL)
	{
		delwin(scrdesc->luc);
		scrdesc->luc = NULL;
	}
	if (scrdesc->fix_rows != NULL)
	{
		delwin(scrdesc->fix_rows);
		scrdesc->fix_rows = NULL;
	}
	if (scrdesc->fix_cols != NULL)
	{
		delwin(scrdesc->fix_cols);
		scrdesc->fix_cols = NULL;
	}
	if (scrdesc->rows != NULL)
	{
		delwin(scrdesc->rows);
		scrdesc->rows = NULL;
	}

	if (scrdesc->fix_rows_rows > 0)
	{
		scrdesc->fix_rows = newwin(scrdesc->fix_rows_rows,
								   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
								   1, scrdesc->fix_cols_cols);
	}
	if (scrdesc->fix_cols_cols > 0)
	{
		scrdesc->fix_cols = newwin(scrdesc->maxy - scrdesc->fix_rows_rows - 2, scrdesc->fix_cols_cols, scrdesc->fix_rows_rows + 1, 0);
	}
	if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
	{
		scrdesc->luc = newwin(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols, 1, 0);
	}
	scrdesc->rows = newwin(scrdesc->maxy - scrdesc->fix_rows_rows - 2,
						   min_int(scrdesc->maxx - scrdesc->fix_cols_cols, scrdesc->maxx - scrdesc->fix_cols_cols + 1),
						   scrdesc->fix_rows_rows + 1, scrdesc->fix_cols_cols);

#ifdef DEBUG_COLORS

	if (scrdesc->rows != NULL)
		wbkgd(scrdesc->rows, COLOR_PAIR(2));
	if (scrdesc->luc != NULL)
		wbkgd(scrdesc->luc, COLOR_PAIR(10));
	if (scrdesc->fix_cols != NULL)
		wbkgd(scrdesc->fix_cols, COLOR_PAIR(11));
	if (scrdesc->fix_rows != NULL)
		wbkgd(scrdesc->fix_rows, COLOR_PAIR(12));

#endif
}

/*
 * Rewresh aux windows like top bar or bottom bar.
 */
static void
refresh_aux_windows(ScrDesc *scrdesc, DataDesc *desc)
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

	if (desc->headline_transl != NULL)
	{
		wattron(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
		mvwaddstr(scrdesc->bottom_bar, 0, 7, "0..4");
		wattroff(scrdesc->bottom_bar, A_BOLD | COLOR_PAIR(5));
		wattron(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
		mvwprintw(scrdesc->bottom_bar, 0, 11, "%s", " Col.Freeze ");
		wattroff(scrdesc->bottom_bar, COLOR_PAIR(6) | A_BOLD);
		wrefresh(scrdesc->bottom_bar);
	}
}

static int
number_width(int num)
{
	if (num < 10)
		return 1;
	if (num < 100)
		return 2;
	if (num < 1000)
		return 3;
	if (num < 10000)
		return 4;
	if (num < 100000)
		return 5;
	if (num < 1000000)
		return 6;
	if (num < 10000000)
		return 7;
}



static void
print_top_window_context(ScrDesc *scrdesc, DataDesc *desc,
						 int cursor_row, int cursor_col, int first_row)
{
	int		maxy, maxx;
	int		smaxy, smaxx;
	char	buffer[200];

	getmaxyx(scrdesc->top_bar, maxy, maxx);
	getmaxyx(stdscr, smaxy, smaxx);

	if (scrdesc->theme == 2)
		wattron(scrdesc->top_bar, A_BOLD | COLOR_PAIR(7));

	if (desc->title[0] != '\0')
		mvwprintw(scrdesc->top_bar, 0, 0, "%s", desc->title);
	else if (desc->filename[0] != '\0')
		mvwprintw(scrdesc->top_bar, 0, 0, "%s", desc->filename);

	if (scrdesc->theme == 2)
		wattroff(scrdesc->top_bar, A_BOLD | COLOR_PAIR(7));

	if (desc->headline_transl)
	{
		snprintf(buffer, 199, "FC:%*d C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
							number_width(desc->headline_char_size), scrdesc->fix_cols_cols,
							number_width(desc->headline_char_size), cursor_col + scrdesc->fix_cols_cols + 1,
							number_width(desc->headline_char_size), min_int(smaxx + cursor_col, desc->headline_char_size),
							number_width(desc->headline_char_size), desc->headline_char_size,
							number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + 1,
							number_width(smaxy), cursor_row - first_row,
							number_width(desc->maxy - scrdesc->fix_rows_rows - 1), cursor_row + 1,
							number_width(desc->maxy - scrdesc->fix_rows_rows - 1), desc->maxy - scrdesc->fix_rows_rows - 1,
							(cursor_row + 1) / ((double) (desc->maxy - scrdesc->fix_rows_rows - 1)) * 100.0);
	}
	else
	{
		snprintf(buffer, 199, "C:%*d..%*d/%*d  L:[%*d + %*d  %*d/%*d] %3.0f%%",
							number_width(desc->maxx), cursor_col + scrdesc->fix_cols_cols + 1,
							number_width(desc->maxx), min_int(smaxx + cursor_col, desc->maxx),
							number_width(desc->maxx), desc->maxx,
							number_width(desc->maxy - scrdesc->fix_rows_rows), first_row + 1,
							number_width(smaxy), cursor_row - first_row,
							number_width(desc->maxy - scrdesc->fix_rows_rows), cursor_row,
							number_width(desc->maxy - scrdesc->fix_rows_rows), desc->maxy - scrdesc->fix_rows_rows - 1,
							((cursor_row) / ((double) (desc->maxy - scrdesc->fix_rows_rows - 1))) * 100.0);
	}

	mvwprintw(scrdesc->top_bar, 0, maxx - strlen(buffer), "%s", buffer);
	wrefresh(scrdesc->top_bar);
}

int
main(int argc, char *argv[])
{
	int		maxx, maxy;
	int		c;
	int		cursor_row = 0;
	int		cursor_col = 0;
	int		prev_cursor_row;
	int		first_row = 0;
	int		prev_first_row;
	int		i;
	char   str[120];
	int		style = STYLE;
	DataDesc		desc;
	ScrDesc			scrdesc;
	int		_columns = -1;			/* default will be 1 if screen width will be enough */
	int		fixedRows = -1;			/* detect automaticly */
	FILE   *fp = NULL;
	int		stacked_mouse_event = -1;
	bool	detected_format = false;
	bool	no_alternate_screen = false;

	char	buffer[2048];

	int		opt;

	while ((opt = getopt(argc, argv, "bs:c:df:X")) != -1)
	{
		int		n;

		switch (opt)
		{
			case 'X':
				no_alternate_screen = true;
				break;
			case 'b':
				style = 0;
				break;
			case 's':
				n = atoi(optarg);
				if (n < 0 || n > 9)
				{
					fprintf(stderr, "Only color schemas 0 .. 9 are supported.\n");
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
				_columns = n;
				break;
			case 'd':
				fp = fopen(FILENAME, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s\n", FILENAME);
					exit(1);
				}
				break;
			case 'f':
				fp = fopen(optarg, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "cannot to read file: %s\n", FILENAME);
					exit(1);
				}
				break;
			default:
				fprintf(stderr, "Usage: %s [-b] [-s n] [-c n] [file...] [-X]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	setlocale(LC_ALL, "");

	readfile(fp, &desc);

	initscr();

	if(!has_colors())
	{
		endwin();
		fprintf(stderr, "Your terminal does not support color\n");
		exit(1);
	}

	start_color();
	initialize_color_pairs(style);

	clear();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	noecho();

	mousemask(ALL_MOUSE_EVENTS, NULL);
	mouseinterval(50);

	if (desc.headline != NULL)
		detected_format = translate_headline(&desc);

	memset(&scrdesc, sizeof(ScrDesc), 0);
	scrdesc.theme = style;
	refresh_aux_windows(&scrdesc, &desc);
	getmaxyx(stdscr, maxy, maxx);

	/* some corrections */
	if (detected_format)
	{
		if (desc.is_expanded_mode)
		{
			if (strchr(desc.headline_transl,'I') == NULL)
			{
				char *str = desc.rows.rows[desc.title_rows + 1];
				int pos = 0;

				/* fallback point, didn't find separator already */
				while (pos < 40)
				{
					if ((desc.linestyle == 'a' && *str == '|' && pos > 1) ||
					    (desc.linestyle == 'u' && pos > 1 &&
					    (strncmp(str, /* │ */ "\342\224\202", 3) == 0 ||
					     strncmp(str, /* ║ */ "\342\225\221", 3) == 0)))
					{
						desc.headline_transl[pos] = 'I';
						break;
					}
					pos += 1;
					str += utf8charlen(*str);
				}
			}
		}
		else
		{
			if (desc.border_type != 2)
			{
				if (desc.border_bottom_row == -1)
				{
					desc.border_bottom_row = desc.last_data_row;
					desc.last_data_row -= 1;
				}
			}
		}
	}

	create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
	create_layout(&scrdesc, &desc);

	print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row);

	if (no_alternate_screen)
	{
		endwin();
	}

	while (true)
	{
		bool		refresh_scr = false;
		bool		resize_scr = false;
		int			fixed_columns;
		bool		generic_pager = desc.headline_transl == NULL;

		window_fill(scrdesc.luc, desc.title_rows, 0, -1, &desc, COLOR_PAIR(4) | A_BOLD, 0, 0, 0, 0, 10);
		window_fill(scrdesc.rows, scrdesc.fix_rows_rows + first_row + desc.title_rows, scrdesc.fix_cols_cols + cursor_col, cursor_row - first_row, &desc,
					scrdesc.theme == 2 ? 0 | A_BOLD : 0,
					scrdesc.theme == 2 && generic_pager ? A_BOLD : 0,
					COLOR_PAIR(8) | A_BOLD,
					COLOR_PAIR(6) | A_BOLD, generic_pager ? A_BOLD | COLOR_PAIR(6) : COLOR_PAIR(6),
					COLOR_PAIR(6) | A_BOLD);
		window_fill(scrdesc.fix_cols, scrdesc.fix_rows_rows + first_row + desc.title_rows, 0, cursor_row - first_row, &desc,
					COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
					COLOR_PAIR(5) | A_BOLD, COLOR_PAIR(6),
					COLOR_PAIR(6) | A_BOLD);
		window_fill(scrdesc.fix_rows, desc.title_rows, scrdesc.fix_cols_cols + cursor_col, -1, &desc, COLOR_PAIR(4) | A_BOLD, 0, 0, 0, 0, 0);

		if (scrdesc.luc != NULL)
			wrefresh(scrdesc.luc);
		if (scrdesc.rows != NULL)
			wrefresh(scrdesc.rows);
		if (scrdesc.fix_cols != NULL)
			wrefresh(scrdesc.fix_cols);
		if (scrdesc.fix_rows != NULL)
			wrefresh(scrdesc.fix_rows);

		refresh();

		c = getch();
		if (c == 'q' || c == KEY_F(10))
			break;

		prev_cursor_row = cursor_row;
		prev_first_row = first_row;

		switch (c)
		{
			case KEY_UP:
			case 'k':
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
				_columns = c - '0';
				cursor_col = 0;
				refresh_scr = true;
				break;

			case KEY_DOWN:
			case 'j':
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
			case 'h':
				{
					int		move_left = 30;

					if (cursor_col == 0)
						break;

					if (desc.headline_transl != NULL)
					{
						int		i;

						for (i = 1; i <= 30; i++)
						{
							int		pos = scrdesc.fix_cols_cols + cursor_col - i;

							if (pos < 0)
								break;

							if (desc.headline_transl[i] == 'I')
							{
								move_left = i;
								break;
							}
						}
					}

					cursor_col -= move_left;
					if (cursor_col < 3)
						cursor_col = 0;
				}
				break;

			case KEY_RIGHT:
			case 'l':
				{
					int		move_right = 30;
					int		max_cursor_col;

					if (desc.headline_transl != NULL)
					{
						int		i;
						char   *str = &desc.headline_transl[scrdesc.fix_cols_cols + cursor_col];

						for (i = 1; i <= 30; i++)
						{
							if (str[i] == 'I')
							{
								move_right = i + 1;
								break;
							}
						}
					}

					cursor_col += move_right;

					if (desc.headline_transl != NULL)
						max_cursor_col = desc.headline_char_size - maxx;
					else
						max_cursor_col = desc.maxx - maxx - 1;

					max_cursor_col = max_cursor_col > 0 ? max_cursor_col : 0;

					if (cursor_col > max_cursor_col)
						cursor_col = max_cursor_col;
				}
				break;

			case 538:		/* CTRL HOME */
			case 'g':
				cursor_row = 0;
				first_row = 0;
				break;

			case 533:		/* CTRL END */
			case 'G':
				cursor_row = desc.last_row - scrdesc.fix_rows_rows - 1;
				first_row = desc.last_row - maxy + 2 - desc.title_rows;
				break;

			case 'H':
				cursor_row = first_row;
				break;
			case 'L':
				cursor_row = first_row + maxy - scrdesc.fix_rows_rows + desc.title_rows - 3;
				break;
			case 'M':
				cursor_row = first_row + (maxy - scrdesc.fix_rows_rows + desc.title_rows - 3 >> 1);
				break;

			case KEY_PPAGE:
			case 2:		/* CTRL B */
				if (first_row > 0)
				{
					first_row -= maxy - scrdesc.fix_rows_rows - 2;
					if (first_row < 0)
						first_row = 0;
				}
				if (cursor_row > 0)
				{
					cursor_row -= maxy - scrdesc.fix_rows_rows - 2;
					if (cursor_row < 0)
						cursor_row = 0;
				}
				break;

			case KEY_NPAGE:
			case ' ':
			case 6:		/* CTRL F */
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
				resize_scr = true;
			break;

			case KEY_HOME:
			case '^':
				cursor_col = 0;
				break;

			case KEY_END:
			case '$':
					if (desc.headline != NULL)
						cursor_col = desc.headline_char_size - maxx;
					else
						cursor_col = desc.maxx - maxx - 1;

					cursor_col = cursor_col > 0 ? cursor_col : 0;
				break;

			case '/':
					mvwprintw(scrdesc.bottom_bar, 0, 0, "%s", "/");
					wclrtoeol(scrdesc.bottom_bar);
					echo();
					wgetnstr(scrdesc.bottom_bar, scrdesc.searchterm, sizeof(scrdesc.searchterm) - 1);
					noecho();
					/* continue to find next: */
			case 'n':
				{
					int current_row = scrdesc.fix_rows_rows;
					int nrows;
					LineBuffer *rows = &desc.rows;

					for (nrows = 0; nrows <= desc.last_data_row - scrdesc.fix_rows_rows; nrows++, current_row++)
					{
						if (current_row == 1000)
						{
							rows = rows->next;
							current_row = 0;
						}

						if (nrows <= cursor_row) /* skip to start */
							continue;
						if (!strstr(rows->rows[current_row], scrdesc.searchterm))
							continue;

						cursor_row = nrows;

						int bottom_row = cursor_row - (maxy - scrdesc.fix_rows_rows + desc.title_rows - 3);
						if (first_row < bottom_row)
							first_row = bottom_row;
						break;
					}
					refresh_scr = true;
				}
				break;

			case KEY_MOUSE:
				{
					MEVENT		event;

					if (getmouse(&event) == OK)
					{
						int		x = event.bstate;

#ifdef MOUSE_WHEEL_BUTTONS

						if (event.bstate & BUTTON5_PRESSED)
						{
							int		max_cursor_row;
							int		max_first_row;
							int		offset = 1;

							max_first_row = desc.last_row - maxy + 2 - desc.title_rows;
							if (max_first_row < 0)
								max_first_row = 0;

							if (desc.headline_transl != NULL)
								offset = (maxy - scrdesc.fix_rows_rows - 2) / 3;

							if (first_row + offset > max_first_row)
								offset = 1;

							first_row += offset;
							cursor_row += offset;

							max_cursor_row = desc.last_row - scrdesc.fix_rows_rows - 1;
							if (cursor_row > max_cursor_row)
								cursor_row = max_cursor_row;

							if (cursor_row - first_row > maxy - scrdesc.fix_rows_rows + desc.title_rows - 3)
								first_row += 1;

							if (first_row > max_first_row)
								first_row = max_first_row;
						}
						else if (event.bstate & BUTTON4_PRESSED)
						{
							int		offset = 1;

							if (desc.headline_transl != NULL)
								offset = (maxy - scrdesc.fix_rows_rows - 2) / 3;

							if (first_row <= offset)
								offset = 1;

							if (first_row > 0)
							{
								first_row -= offset;
								if (first_row < 0)
									first_row = 0;
							}
							if (cursor_row > 0)
							{
								cursor_row -= offset;
								if (cursor_row < 0)
									cursor_row = 0;
							}
						}
						else
#endif

						if (event.bstate & BUTTON1_PRESSED)
						{
							int		max_cursor_row;
							int		max_first_row;

							cursor_row = event.y - scrdesc.fix_rows_rows - 1 + first_row;
							if (cursor_row < 0)
								cursor_row = 0;

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
					}
				}
				break;
		}

		print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row);

		if (refresh_scr)
		{
			if (resize_scr)
			{
				struct winsize size;

				/*
				 * Workaround - the variables COLS, LINES are not refreshed
				 * when pager is resized and executed inside psql.
				 */
				if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
				{
					resize_term(size.ws_row, size.ws_col);
					clear();
				}

				resize_scr = false;
			}

			getmaxyx(stdscr, maxy, maxx);

			refresh_aux_windows(&scrdesc, &desc);
			create_layout_dimensions(&scrdesc, &desc, _columns, fixedRows, maxy, maxx);
			create_layout(&scrdesc, &desc);
			print_top_window_context(&scrdesc, &desc, cursor_row, cursor_col, first_row);

			refresh_scr = false;
		}
	}

	endwin();

	if (no_alternate_screen)
	{
		draw_data(&scrdesc, &desc, first_row, cursor_col);
	}

	return 0;
}
