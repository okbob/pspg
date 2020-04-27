#include <ctype.h>
#include "st_curses.h"
#include "st_panel.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

#ifdef HAVE_LIBUNISTRING

/* libunistring */
#include <unicase.h>
#include <unistr.h>
#include <uniwidth.h>

#else

/* Alternative minimalistic own unicode support */

#include "unicode.h"

#endif

#include "st_menu.h"

/*
 * This window is main application window. It is used for taking content
 * for shadow drawing.
 */
static WINDOW *desktop_win = NULL;

typedef struct
{
	char	*c;
	int		length;
	int		row;
} ST_MENU_ACCELERATOR;

struct ST_MENU
{
	ST_MENU_ITEM	   *menu_items;
	WINDOW	   *draw_area;
	WINDOW	   *window;
	PANEL	   *panel;
	WINDOW	   *shadow_window;
	PANEL	   *shadow_panel;
	int			first_row;						/* first visible row */
	int			cursor_row;
	int			mouse_row;						/* mouse row where button1 was pressed */
	int		   *options;						/* state options, initially copyied from menu */
	int		  **refvals;						/* referenced values */
	ST_MENU_ACCELERATOR		*accelerators;
	int			naccelerators;
	ST_MENU_CONFIG *config;
	int			shortcut_x_pos;
	int			item_x_pos;
	int			nitems;							/* number of menu items */
	int		   *bar_fields_x_pos;				/* array of x positions of menubar fields */
	int			ideal_y_pos;					/* y pos when is enough space */
	int			ideal_x_pos;					/* x pos when is enough space */
	int			rows;							/* number of rows */
	int			cols;							/* number of columns */
	int			focus;							/* identify possible event filtering */
	char	   *title;
	bool		is_menubar;
	struct ST_MENU	*active_submenu;
	struct ST_MENU	**submenus;
};

struct ST_CMDBAR
{
	ST_CMDBAR_ITEM	   *cmdbar_items;
	WINDOW	   *window;
	PANEL	   *panel;
	ST_MENU_CONFIG *config;
	int			nitems;
	int		   *positions;
	char	  **labels;
	ST_CMDBAR_ITEM	   **ordered_items;
};

static struct ST_CMDBAR   *active_cmdbar = NULL;

static ST_MENU_ITEM		   *selected_item = NULL;
static ST_CMDBAR_ITEM	   *selected_command = NULL;
static int					selected_options = 0;
static int				   *selected_refval = NULL;

static bool			press_accelerator = false;
static bool			button1_clicked = false;
static bool			press_enter = false;

static bool			command_was_activated = false;

static inline int char_length(ST_MENU_CONFIG *config, const char *c);
static inline int char_width(ST_MENU_CONFIG *config, char *c);
static inline int str_width(ST_MENU_CONFIG *config, char *str);
static inline char *chr_casexfrm(ST_MENU_CONFIG *config, char *str);
static inline int wchar_to_utf8(ST_MENU_CONFIG *config, char *str, int n, wchar_t wch);

static bool _st_menu_driver(struct ST_MENU *menu, int c, bool alt, MEVENT *mevent, bool is_top, bool is_nested_pulldown, bool *unpost_submenu);
static void _st_menu_free(struct ST_MENU *menu);

static int menutext_displaywidth(ST_MENU_CONFIG *config, char *text, char **accelerator, bool *extern_accel);
static void pulldownmenu_content_size(ST_MENU_CONFIG *config, ST_MENU_ITEM *menu_items,
										int *rows, int *columns, int *shortcut_x_pos, int *item_x_pos,
										ST_MENU_ACCELERATOR *accelerators, int *naccelerators, int *first_row);

static void pulldownmenu_draw_shadow(struct ST_MENU *menu);
static void menubar_draw(struct ST_MENU *menu);
static void pulldownmenu_draw(struct ST_MENU *menu, bool is_top);
static void cmdbar_draw(struct ST_CMDBAR *cmdbar);
static bool cmdbar_driver(struct ST_CMDBAR *cmdbar, int c, bool alt, MEVENT *mevent);

static void subtract_correction(WINDOW *s, int *y, int *x);

/*
 * Generic functions
 */
static inline int
max_int(int a, int b)
{
	return a > b ? a : b;
}

static inline int
min_int(int a, int b)
{
	return a < b ? a : b;
}

static void *
safe_malloc(size_t size)
{
	void *ptr = malloc(size);

	if (!ptr)
	{
		endwin();
		printf("FATAL: Out of memory\n");
		exit(1);
	}

	memset(ptr, 0, size);

	return ptr;
}

#ifdef PDCURSES

/*
 Created a new version of newwin() because PDCurses (unlike ncurses)
 will not allocate a Window if:
 (begin_y + rows > SP->lines || begin_x + cols > SP->cols)
 Therefore we will make an attempt to reduce the rows/cols if needed
 to get the allocation to pass for *most* cases.
*/
static WINDOW*
newwin2(int* rows, int* cols, int begin_y, int begin_x)
{
	int SPlines, SPcols;

	getmaxyx(stdscr, SPlines, SPcols);
	if (begin_y + *rows > SPlines)
	{
		*rows = SPlines - begin_y;
	}
	if (begin_x + *cols > SPcols)
	{
		*cols = SPcols - begin_x;
	}

	return newwin(*rows, *cols, begin_y, begin_x);
}

#else

#define newwin2(rows, cols, begin_y, begin_x)  newwin(*rows, *cols, begin_y, begin_x)

#endif

/*
 * Returns bytes of multibyte char
 */
static inline int
char_length(ST_MENU_CONFIG *config, const char *c)
{
	int		result;

	if (!config->force8bit)
	{
		/*
		 * I would not to check validity of UTF8 char. So I pass
		 * 4 as n (max possible size of UTF8 char in bytes). When
		 * in this case, u8_mblen should to return possitive number,
		 * but be sure, and return 1 everytime.
		 *
		 * This functionality can be enhanced to check real size
		 * of utf8 string.
		 */
#ifdef HAVE_LIBUNISTRING

		result = u8_mblen((const uint8_t *) c, 4);

#else

		result = utf8charlen(*((char *) c));

#endif

		if (result > 0)
			return result;
	}

	return 1;
}

/*
 * Retuns display width of char
 */
static inline int
char_width(ST_MENU_CONFIG *config, char *c)
{
	if (!config->force8bit)
#ifdef HAVE_LIBUNISTRING

		return u8_width((const uint8_t *) c, 1, config->encoding);

#else

		return utf_dsplen((const char *) c);

#endif

	return 1;
}

/*
 * returns display width of string
 */
static inline int
str_width(ST_MENU_CONFIG *config, char *str)
{
	if (!config->force8bit)
#ifdef HAVE_LIBUNISTRING

		return u8_strwidth((const uint8_t *) str, config->encoding);

#else

		return utf_string_dsplen((const char *) str, strlen(str));

#endif

	return strlen(str);
}

/*
 * Transform string to simply compareable case insensitive string
 */
static inline char *
chr_casexfrm(ST_MENU_CONFIG *config, char *str)
{
	char	buffer[20];
	char   *result;

	if (!config->force8bit)
	{
#ifdef HAVE_LIBUNISTRING

		size_t	length;

		length = sizeof(buffer);
		result = u8_casexfrm((const uint8_t *) str,
								char_length(config, str),
									config->language, NULL,
									buffer, &length);
		if (result == buffer)
		{
			result = strdup(buffer);
			if (!result)
			{
				endwin();
				printf("FATAL: out of memory\n");
				exit(1);
			}
		}

#else

		char buffer2[10];
		int chrl = utf8charlen(*str);
		int	fold;

		strncpy(buffer2, str, chrl);
		buffer2[chrl] = '\0';

		fold  = utf8_tofold((const char *) buffer2);

		*((int *) buffer) = fold;
		buffer[sizeof(int)] = '\0';

		result = strdup(buffer);
		if (!result)
		{
			endwin();
			printf("FATAL: out of memory\n");
			exit(1);
		}

#endif
	}
	else
	{
		buffer[0] = tolower(str[0]);
		buffer[1] = '\0';

		result = strdup(buffer);
		if (!result)
		{
			endwin();
			printf("FATAL: out of memory\n");
			exit(1);
		}
	}

	return result;
}

/*
 * Convert wide char to multibyte encoding
 */
static inline int
wchar_to_utf8(ST_MENU_CONFIG *config, char *str, int n, wchar_t wch)
{
	int		result;

	if (!config->force8bit)
	{
#ifdef HAVE_LIBUNISTRING

		result = u8_uctomb((uint8_t *) str, (ucs4_t) wch, n);

#else

		/* be compiler quite */
		(void) n;

		unicode_to_utf8(wch, (unsigned char *) str, &result);

#endif
	}
	else
	{
		*str = (char) wch;
		result = 1;
	}

	return result;
}

/*
 * Workhorse for st_menu_save
 */
static int
_save_menustate(struct ST_MENU *menu, int *cursor_rows, int max_rows, int write_pos)
{
	int		active_row = -1;
	int		i;

	if (write_pos + 1 + menu->nitems >= max_rows)
	{
		endwin();
		printf("FATAL: Cannot save menu positions, too complex menu.\n");
		exit(1);
	}

	cursor_rows[write_pos++] = menu->cursor_row;
	cursor_rows[write_pos++] = menu->first_row;

	if (menu->submenus)
	{
		for (i = 0; i < menu->nitems; i++)
		{
			if (menu->submenus[i])
			{
				write_pos = _save_menustate(menu->submenus[i], cursor_rows, max_rows, write_pos);

				if (menu->active_submenu == menu->submenus[i])
					active_row = i + 1;
			}
		}
	}

	for (i = 0; i < menu->nitems; i++)
		cursor_rows[write_pos++] = menu->options[i];

	cursor_rows[write_pos++] = active_row;

	return write_pos;
}

static int
_save_refvals(struct ST_MENU *menu, int **refvals, int max_refvals, int write_pos)
{
	int		i;

	if (write_pos + menu->nitems >= max_refvals)
	{
		endwin();
		printf("FATAL: Cannot save menu refvals, too complex menu.\n");
		exit(1);
	}

	if (menu->submenus)
	{
		for (i = 0; i < menu->nitems; i++)
		{
			if (menu->submenus[i])
				write_pos = _save_refvals(menu->submenus[i], refvals, max_refvals, write_pos);
		}
	}

	for (i = 0; i < menu->nitems; i++)
		refvals[write_pos++] = menu->refvals[i];

	return write_pos;
}

/*
 * Workhorse for st_menu_load
 */
static int
_load_menustate(struct ST_MENU *menu, int *cursor_rows, int read_pos)
{
	int		active_row;
	int		i;

	menu->cursor_row = cursor_rows[read_pos++];
	menu->first_row = cursor_rows[read_pos++];

	if (menu->submenus)
	{
		for (i = 0; i < menu->nitems; i++)
		{
			if (menu->submenus[i])
			{
				read_pos = _load_menustate(menu->submenus[i], cursor_rows, read_pos);
			}
		}
	}

	for (i = 0; i < menu->nitems; i++)
		menu->options[i] = cursor_rows[read_pos++];

	active_row = cursor_rows[read_pos++];
	if (active_row != -1)
		menu->active_submenu = menu->submenus[active_row - 1];

	return read_pos;
}

static int
_load_refvals(struct ST_MENU *menu, int **refvals, int read_pos)
{
	int		i;

	if (menu->submenus)
	{
		for (i = 0; i < menu->nitems; i++)
		{
			if (menu->submenus[i])
			{
				read_pos = _load_refvals(menu->submenus[i], refvals, read_pos);
			}
		}
	}

	for (i = 0; i < menu->nitems; i++)
		menu->refvals[i] = refvals[read_pos++];

	return read_pos;
}


/*
 * Serialize important fields of menustate to cursor_rows array.
 */
void
st_menu_save(struct ST_MENU *menu, int *cursor_rows, int **refvals, int max_items)
{
	_save_menustate(menu, cursor_rows, max_items, 0);
	_save_refvals(menu, refvals, max_items, 0);
}

/*
 * Load cursor positions and active submenu from safe
 */
void
st_menu_load(struct ST_MENU *menu, int *cursor_rows, int **refvals)
{
	_load_menustate(menu, cursor_rows, 0);
	_load_refvals(menu, refvals, 0);
}


/*
 * Returns display length of some text. ~ char is ignored.
 * ~~ is used as ~.
 */
static int
menutext_displaywidth(ST_MENU_CONFIG *config, char *text, char **accelerator, bool *extern_accel)
{
	int		result = 0;
	bool	_extern_accel = false;
	char   *_accelerator = NULL;
	bool	first_char = true;
	int		bytes;

	while (*text != '\0')
	{
		if (*text == '~' || (*text == '_' && first_char))
		{
			/*
			 * ~~ or __ disable effect of special chars ~ and _. ~x~ defines
			 * internal accelerator (inside menu item text). _x_ defines
			 * external accelerator (displayed before menu item text) _ has this
			 * effect only when it is first char of menu item text.
			 */
			if (text[1] == *text)
			{
				result += 1;
				text += 2;
			}
			else
			{
				if (*text == '_')
					_extern_accel = true;

				text += 1;
				if (_accelerator == NULL)
					_accelerator = text;

				/* eat second '_' */
				if (_extern_accel)
					text += 1;
			}

			first_char = false;
			continue;
		}

		bytes = char_length(config, text);
		result += char_width(config, text);
		text += bytes;

		first_char = false;
	}

	if (extern_accel)
		*extern_accel = _extern_accel;
	if (accelerator)
		*accelerator = _accelerator;

	return result;
}

/*
 * Collect display info about pulldown menu
 */
static void
pulldownmenu_content_size(ST_MENU_CONFIG *config, ST_MENU_ITEM *menu_items,
								int *rows, int *columns, int *shortcut_x_pos, int *item_x_pos,
								ST_MENU_ACCELERATOR *accelerators, int *naccelerators,
								int *first_row)
{
	char	*accelerator;
	bool	has_extern_accel = false;
	int	max_text_width = 0;
	int max_shortcut_width = 0;
	int		naccel = 0;
	int		default_row = -1;

	*rows = 0;
	*columns = 0;
	*shortcut_x_pos = 0;
	*first_row = -1;

	*naccelerators = 0;

	while (menu_items->text)
	{
		bool	extern_accel;

		*rows += 1;
		if (*menu_items->text && strncmp(menu_items->text, "--", 2) != 0)
		{
			int text_width = 0;
			int shortcut_width = 0;

			if (*first_row == -1)
				*first_row = *rows;

			text_width = menutext_displaywidth(config, menu_items->text, &accelerator, &extern_accel);

			if (extern_accel)
				has_extern_accel = true;

			if (accelerator != NULL)
			{
				accelerators[naccel].c = chr_casexfrm(config, accelerator);
				accelerators[naccel].length = strlen(accelerators[naccel].c);
				accelerators[naccel++].row = *rows;
			}

			if (menu_items->shortcut)
				shortcut_width = str_width(config, menu_items->shortcut);

			if (menu_items->submenu)
				shortcut_width += shortcut_width > 0 ? 2 : 1;

			if (menu_items->options & ST_MENU_OPTION_DEFAULT && default_row == -1)
				default_row = *rows;

			/*
			 * left alligned shortcuts are used by MC style
			 */
			if (config->left_alligned_shortcuts)
			{
				max_text_width = max_int(max_text_width, 1 + text_width + 2);
				max_shortcut_width = max_int(max_shortcut_width, shortcut_width);
			}
			else
				*columns = max_int(*columns,
											1 + text_width + 1
											  + (config->extra_inner_space ? 2 : 0)
											  + (shortcut_width > 0 ? shortcut_width + 4 : 0));
		}

		menu_items += 1;
	}

	if (config->left_alligned_shortcuts)
	{
		*columns = max_text_width + (max_shortcut_width > 0 ? max_shortcut_width + 1 : 0);
		*shortcut_x_pos = max_text_width;
	}
	else
		*shortcut_x_pos = -1;

	*naccelerators = naccel;

	/*
	 * When external accelerators are used, move content to right
	 */
	if (has_extern_accel)
	{
		*columns += config->extern_accel_text_space + 1;
		if (*shortcut_x_pos != -1)
			*shortcut_x_pos += config->extern_accel_text_space + 1;
		*item_x_pos = config->extern_accel_text_space + 1;
	}
	else
		*item_x_pos = 1;

	if (default_row != -1)
		*first_row = default_row;
}

/*
 * Draw menubar
 */
static void
menubar_draw(struct ST_MENU *menu)
{
	ST_MENU_ITEM	   *menu_item = menu->menu_items;
	ST_MENU_CONFIG	*config = menu->config;
	bool	has_focus;
	bool	has_accelerators;
	int		i;

	selected_item = NULL;
	selected_options = 0;
	selected_refval = NULL;

	/* do nothing when content is invisible */
	if (menu->focus == ST_MENU_FOCUS_NONE)
		return;

	show_panel(menu->panel);
	top_panel(menu->panel);

	update_panels();

	has_focus = menu->focus == ST_MENU_FOCUS_FULL;
	has_accelerators = menu->focus == ST_MENU_FOCUS_FULL || 
								menu->focus == ST_MENU_FOCUS_ALT_MOUSE;

	if (has_focus)
		wbkgd(menu->window, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);
	else
		wbkgd(menu->window, COLOR_PAIR(config->menu_unfocused_cpn) | config->menu_unfocused_attr);

	werase(menu->window);

	i = 0;
	while (menu_item->text)
	{
		char	*text = menu_item->text;
		bool	highlight = false;
		bool	is_cursor_row = menu->cursor_row == i + 1 && has_focus;
		bool	is_disabled = menu->options[i] & ST_MENU_OPTION_DISABLED;
		int		current_pos;

		/* bar_fields_x_pos holds x positions of menubar items */
		current_pos = menu->bar_fields_x_pos[i];

		if (is_cursor_row)
		{
			wmove(menu->window, 0, current_pos - 1);
			wattron(menu->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
			waddstr(menu->window, " ");

			selected_item = menu_item;
		}
		else
			wmove(menu->window, 0, current_pos);

		if (is_disabled)
			wattron(menu->window, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);

		while (*text)
		{
			/* there are not external accelerators */
			if (*text == '~')
			{
				if (text[1] == '~')
				{
					waddstr(menu->window, "~");
					text += 2;
					continue;
				}

				if (!is_disabled && has_accelerators)
				{
					if (!highlight)
					{
						wattron(menu->window,
							COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
									   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr) );
					}
					else
					{
						wattroff(menu->window,
							COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
									   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
						if (is_cursor_row)
							wattron(menu->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
					}

					highlight = !highlight;
				}
				text += 1;
			}
			else
			{
				int chlen = char_length(config, text);

				waddnstr(menu->window, text, chlen);
				text += chlen;
			}
		}

		if (is_cursor_row)
		{
			waddstr(menu->window, " ");
			wattroff(menu->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
		}

		if (is_disabled)
			wattroff(menu->window, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);

		menu_item += 1;
		i += 1;
	}

	wnoutrefresh(menu->window);


	if (menu->active_submenu)
		pulldownmenu_draw(menu->active_submenu, true);
}

#ifdef DEBUG_PIPE

static void
debug_print_size(WINDOW *window, char *name)
{
	int		rows, cols;
	int		y, x;

	getbegyx(window, y, x);
	getmaxyx(window, rows, cols);

	fprintf(debug_pipe, "window \"%s\" y: %d, x: %d, rows: %d. cols: %d\n", name, y, x, rows, cols);
}

#endif

/*
 * adjust pulldown possition - move panels from ideal position to any position
 * where can be fully displayed.
 */
static void
pulldownmenu_ajust_position(struct ST_MENU *menu, int maxy, int maxx)
{
	ST_MENU_CONFIG	*config = menu->config;

	int		rows, cols;
	int		new_y, new_x;
	int		y, x;

	getbegyx(menu->window, y, x);
	getmaxyx(menu->window, rows, cols);
	subtract_correction(menu->window, &y, &x);

	/*
	 * Hypothesis: when panel is moved, then assigned windows is moved
	 * to and possibly reduced (when terminal is smaller). But when terminal
	 * grows, then size of assigned window grows too, more than was original
	 * size. So we should to recheck size of assigned window, and fix it, when
	 * it is bigger than should be.
	 */
	if (rows != menu->rows || cols != menu->cols)
	{
		int		new_rows = y + menu->rows <= maxy ? menu->rows : maxy - y + 1;
		int		new_cols = x + menu->cols <= maxx ? menu->cols : maxx - x + 1;

		if (new_rows != rows || new_cols != cols)
			wresize(menu->window, new_rows, new_cols);
	}

#ifdef DEBUG_PIPE

	debug_print_size(menu->window, "menu window");

#endif

	/*
	 * Previous issue is same for shadow window. But with different timing.
	 * Visibility of shadow needs one row and one column more. The begin of
	 * shadow window should be in relation to begin of menu window.
	 */
	if (config->shadow_width > 0)
	{
		int		srows, scols;

		/* panel move can move shadow window. Force correct position now */

		getmaxyx(menu->shadow_window, srows, scols);
		mvwin(menu->shadow_window, y+1 , x+config->shadow_width);

		if (srows != menu->rows || scols != menu->cols)
		{
			int		new_srows, new_scols;

			new_srows = y + 1 + menu->rows <= maxy ? menu->rows :  maxy - y - 1;
			new_scols = x + config->shadow_width + menu->cols <= maxx ? menu->cols : maxx - x - config->shadow_width;

			wresize(menu->shadow_window, new_srows, new_scols);
		}
	}

#ifdef DEBUG_PIPE

	debug_print_size(menu->shadow_window, "menu shadow window");

#endif

	if (menu->ideal_x_pos + menu->cols > maxx)
	{
		new_x = maxx - menu->cols;
		if (new_x < 0)
			new_x = 0;
	}
	else
		new_x = menu->ideal_x_pos;

	if (menu->ideal_y_pos + menu->rows > maxy)
	{
		new_y = maxy - menu->rows;
		if (new_y < 1)
			new_y = 1;
	}
	else
		new_y = menu->ideal_y_pos;

	if (new_y != y || new_x != x)
	{
		int result;

		result = move_panel(menu->panel, new_y, new_x);

		/*
		 * This is maybe ugly hack. move_panel fails when
		 * attached window cannot be displayed. So we can try
		 * resize attached window first.
		 */
		if (result != OK)
		{
			WINDOW *pw = panel_window(menu->panel);

			wresize(pw, maxy - new_y, menu->cols);
			replace_panel(menu->panel, pw);
		}

		result = move_panel(menu->panel, new_y, new_x);

		/*
		 * move_panel fails when it cannot be displayed completly.
		 * This is problem for shadow window because is n char right,
		 * over left border. So we have to create new window with
		 * different sizes.
		 * Don't try move shadow panel, when a move of menu panel
		 * failed.
		 */
		if (result == OK && menu->shadow_panel)
		{
			int		new_rows, new_cols;
			int		smaxy, smaxx;

			new_cols = menu->cols - (new_x == menu->ideal_x_pos ? 0 : config->shadow_width);
			new_rows = menu->rows - (maxy >= new_y + menu->rows + 1 ? 0 : 1);

			getmaxyx(menu->shadow_window, smaxy, smaxx);

			if (new_cols <= smaxx || new_rows <= smaxy)
			{
				WINDOW   *new_shadow_window;

				new_shadow_window = newwin2(&new_rows, &new_cols, new_y + 1, new_x + config->shadow_width);

				/* There are no other possibility to resize panel */
				replace_panel(menu->shadow_panel, new_shadow_window);

				delwin(menu->shadow_window);
				menu->shadow_window = new_shadow_window;

				wbkgd(menu->shadow_window, COLOR_PAIR(config->menu_shadow_cpn) | config->menu_shadow_attr);

				wnoutrefresh(menu->shadow_window);
			}

			move_panel(menu->shadow_panel, new_y + 1, new_x + config->shadow_width);
		}
	}

	if (menu->active_submenu)
		pulldownmenu_ajust_position(menu->active_submenu, maxy, maxx);

	update_panels();
}

/*
 * Draw shadow
 */
static void
pulldownmenu_draw_shadow(struct ST_MENU *menu)
{
	ST_MENU_CONFIG	*config = menu->config;

	if (menu->shadow_window)
	{
		int		smaxy, smaxx;
		int		i, j;
		int		wmaxy, wmaxx;
		attr_t	shadow_attr;

		shadow_attr = config->menu_shadow_attr | A_DIM;

		getmaxyx(menu->shadow_window, smaxy, smaxx);

		show_panel(menu->shadow_panel);
		top_panel(menu->shadow_panel);

		/* desktop_win must be global */
		werase(menu->shadow_window);

		if (desktop_win)
			overwrite(desktop_win, menu->shadow_window);
		if (active_cmdbar)
			overwrite(active_cmdbar->window, menu->shadow_window);

		wmaxy = smaxy - 1;
		wmaxx = smaxx - config->shadow_width;

		for (i = 0; i <= smaxy; i++)
			for (j = 0; j <= smaxx; j++)
			{

				if (i < wmaxy && j < wmaxx)
					continue;

				if (mvwinch(menu->shadow_window, i, j) & A_ALTCHARSET)
					mvwchgat(menu->shadow_window, i, j, 1,
								shadow_attr | A_ALTCHARSET,
								config->menu_shadow_cpn,
								NULL);
				else
					mvwchgat(menu->shadow_window, i, j, 1,
								shadow_attr,
								config->menu_shadow_cpn,
								NULL);
			}

		wnoutrefresh(menu->shadow_window);
	}

	if (menu->active_submenu)
		pulldownmenu_draw_shadow(menu->active_submenu);
}

/*
 * Early search of selected refval items. Is necessary to
 * change state before next drawing.
 */
static void
searching_selected_refval_items(struct ST_MENU *menu)
{
	ST_MENU_ITEM	   *menu_items = menu->menu_items;
	int		row = 1;

	while (menu_items->text != NULL)
	{
		int		offset = menu_items - menu->menu_items;

		if (IS_REF_OPTION(menu->options[offset]))
		{
			if (menu->cursor_row == row)
			{
				selected_item = menu_items;
				selected_options = menu->options[offset];
				selected_refval= menu->refvals[offset];
			}
		}

		menu_items += 1;
		row += 1;
	}

	if (menu->active_submenu)
		searching_selected_refval_items(menu->active_submenu);
}

/*
 * pulldown menu bar draw
 */
static void
pulldownmenu_draw(struct ST_MENU *menu, bool is_top)
{
	bool	draw_box = menu->config->draw_box;
	ST_MENU_ITEM	   *menu_items = menu->menu_items;
	ST_MENU_CONFIG	*config = menu->config;
	WINDOW	   *draw_area = menu->draw_area;
	WINDOW	   *loc_draw_area = NULL;
	int		row = 1;
	int		maxy, maxx;
	int		dmaxy, dmaxx, dy, dx;
	int		text_min_x, text_max_x;
	int		*options = menu->options;
	bool	force_ascii_art = config->force_ascii_art;
	int		max_draw_rows = menu->rows;
	int		i;

	selected_item = NULL;

	if (is_top)
	{
		int	stdscr_maxy, stdscr_maxx;

		/* adjust positions of pulldown menus */
		getmaxyx(stdscr, stdscr_maxy, stdscr_maxx);
		pulldownmenu_ajust_position(menu, stdscr_maxy, stdscr_maxx);

		/* Draw shadows of window and all nested active pull down menu */
		pulldownmenu_draw_shadow(menu);
	}

	show_panel(menu->panel);
	top_panel(menu->panel);

	update_panels();

	/* clean menu background */
	werase(menu->window);

	/*
	 * Now, we would to check if is possible to draw complete draw area on
	 * screen, and if draw area is good enough for all menu's items.
	 */
	getmaxyx(stdscr, maxy, maxx);
	getmaxyx(draw_area, dmaxy, dmaxx);
	getbegyx(draw_area, dy, dx);

	subtract_correction(draw_area, &dy, &dx);

	if (dy + dmaxy > maxy || dmaxy < menu->rows )
	{
		dmaxy = min_int(maxy - dy, dmaxy);
		max_draw_rows = draw_box ? (dmaxy - 2) : dmaxy;

		loc_draw_area = subwin(menu->window, dmaxy, dmaxx, dy, dx);
		draw_area = loc_draw_area;

		if (menu->cursor_row < menu->first_row)
			menu->first_row = menu->cursor_row;

		if (menu->cursor_row > menu->first_row + max_draw_rows - 1)
			menu->first_row = menu->cursor_row - max_draw_rows + 1;
	}
	else
		menu->first_row = 1;

	getmaxyx(draw_area, maxy, maxx);

	/* be compiler quiet */
	(void) maxy;

	if (draw_box)
	{
		if (!force_ascii_art)
			box(draw_area, 0, 0);
		else
			wborder(draw_area, '|', '|','-','-','+','+','+','+');
	}

	text_min_x = (draw_box ? 1 : 0) + (config->extra_inner_space ? 1 : 0);
	text_max_x = maxx - (draw_box ? 1 : 0) - (config->extra_inner_space ? 1 : 0);

	/* skip first firt_row rows from menu */
	for (i = 1; i < menu->first_row; i++)
		if (menu_items->text != NULL)
			menu_items += 1;

	while (menu_items->text != NULL)
	{
		int		offset = menu_items - menu->menu_items;
		bool	has_submenu = menu_items->submenu ? true : false;
		bool	is_disabled = false;
		bool	is_marked = false;
		int		mark_tag;

		if (options)
		{
			int		option = options[offset];

			is_disabled = option & ST_MENU_OPTION_DISABLED;

			if (option & ST_MENU_OPTION_MARKED)
			{
				mark_tag = config->mark_tag;
				is_marked = true;
			}
			else if (option & ST_MENU_OPTION_MARKED_REF)
			{
				int   *refval = menu->refvals[offset];

				if (*refval == menu_items->data)
				{
					is_marked = true;
					mark_tag = config->mark_tag;
				}
			}
			else if (option & ST_MENU_OPTION_SWITCH2_REF)
			{
				int   *refval = menu->refvals[offset];

				is_marked = true;
				mark_tag = (*refval == 1) ? config->switch_tag_1 : config->switch_tag_0;
			}
			else if (option & ST_MENU_OPTION_SWITCH3_REF)
			{
				int   *refval = menu->refvals[offset];

				is_marked = true;
				switch (*refval)
				{
					case 1:
						mark_tag = config->switch_tag_1;
						break;
					case 0:
						mark_tag = config->switch_tag_0;
						break;
					default:
						mark_tag = config->switch_tag_n1;
						break;
				}
			}
		}

		if (*menu_items->text == '\0' || strncmp(menu_items->text, "--", 2) == 0)
		{
			if (draw_box)
			{
				wmove(draw_area, row, 0);
				if (!force_ascii_art)
					waddch(draw_area, ACS_LTEE);
				else
					waddch(draw_area, '|');
			}
			else
				wmove(draw_area, row - 1, 0);

			for(i = 0; i < maxx - 1 - (draw_box ? 1 : -1); i++)
			{
				if (!force_ascii_art)
					waddch(draw_area, ACS_HLINE);
				else
					waddch(draw_area, '-');
			}

			if (draw_box)
			{
				if (!force_ascii_art)
					waddch(draw_area, ACS_RTEE);
				else
					waddch(draw_area, '|');
			}
		}
		else
		{
			char	*text = menu_items->text;
			bool	highlight = false;
			bool	is_cursor_row = menu->cursor_row == offset + 1;
			bool	first_char = true;
			bool	is_extern_accel;
			int		text_y = -1;
			int		text_x = -1;

			if (is_cursor_row)
			{
				mvwchgat(draw_area, row - (draw_box ? 0 : 1), text_min_x, text_max_x - text_min_x,
						config->cursor_attr, config->cursor_cpn, NULL);
				wattron(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

				selected_item = menu_items;
			}

			if (is_disabled)
				wattron(draw_area, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);

			is_extern_accel = (*text == '_' && text[1] != '_');

			if (menu->item_x_pos != 1 && !is_extern_accel)
			{
				wmove(draw_area, row - (draw_box ? 0 : 1), text_min_x + 1 + menu->item_x_pos);
			}
			else
				wmove(draw_area, row - (draw_box ? 0 : 1), text_min_x + 1);

			while (*text)
			{
				if (*text == '~' || (*text == '_' && (first_char || highlight)))
				{
					if (text[1] == *text)
					{
						waddnstr(draw_area, text, 1);
						text += 2;
						first_char = false;
						continue;
					}

					if (!is_disabled)
					{
						if (!highlight)
						{
							wattron(draw_area,
								COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
										   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
						}
						else
						{
							wattroff(draw_area,
								COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
										   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
							if (is_cursor_row)
								wattron(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

							if (is_extern_accel)
							{
								int		y, x;

								getyx(draw_area, y, x);
								wmove(draw_area, y, x + config->extern_accel_text_space);
							}
						}

						highlight = !highlight;
					}
					text += 1;
				}
				else
				{
					int chlen = char_length(config, text);

					/* Save initial position of text. This first char, when is not
					 * external accelerator used, or first char after highlighted char
					 * when extern accelerator is used.
					 */
					if (text_y == -1 && text_x == -1)
					{
						if (!is_extern_accel || !highlight)
							getyx(draw_area, text_y, text_x);
					}

					waddnstr(draw_area, text, chlen);
					text += chlen;
				}

				first_char = false;
			}

			if (menu_items->shortcut != NULL)
			{
				if (menu->shortcut_x_pos != -1)
				{
					wmove(draw_area, row - (draw_box ? 0 : 1), menu->shortcut_x_pos + (draw_box ? 1 : 0));
				}
				else
				{
					int dspl = str_width(config, menu_items->shortcut);

					wmove(draw_area,
							  row - (draw_box ? 0 : 1),
							  text_max_x - dspl - 1 - (has_submenu ? 2 : 0));
				}

				waddstr(draw_area, menu_items->shortcut);
			}

			if (has_submenu)
			{
				mvwprintw(draw_area,
								row - (draw_box ? 0 : 1),
								text_max_x - 2,
									"%lc", config->submenu_tag);
			}

			if (is_marked)
			{
				mvwprintw(draw_area,
								row - (draw_box ? 0 : 1),
								text_x - 1,
									"%lc", mark_tag);
			}

			if (is_cursor_row)
				wattroff(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

			if (is_disabled)
				wattroff(draw_area, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);
		}

		menu_items += 1;
		row += 1;

		if (row > max_draw_rows)
			break;
	}

	if (draw_box)
	{
		if (menu->first_row > 1)
			mvwprintw(draw_area, 1, maxx - 1, "%lc", config->scroll_up_tag);

		if (menu->first_row + max_draw_rows - 1 < menu->nitems)
			mvwprintw(draw_area, maxy - 2, maxx - 1, "%lc", config->scroll_down_tag);
	}

	if (loc_draw_area)
	{
		wnoutrefresh(loc_draw_area);
		delwin(loc_draw_area);
	}

	wnoutrefresh(menu->window);

	if (menu->active_submenu)
		pulldownmenu_draw(menu->active_submenu, false);
}

/*
 * Sets desktop window - it is used to draw shadow. The window
 * should be panelized.
 */
void
st_menu_set_desktop_window(WINDOW *win)
{
	desktop_win = win;
}

/*
 * Show menu - pull down or menu bar.
 */
void
st_menu_post(struct ST_MENU *menu)
{
	curs_set(0);
	noecho();

	menu->mouse_row = -1;

	/* show menu */
	if (menu->is_menubar)
		menubar_draw(menu);
	else
		pulldownmenu_draw(menu, true);
}

/*
 * Hide menu. When close_active_submenu is true, then the path
 * of active submenu is destroyed - it doesn't rememeber opened
 * submenus.
 */
void
st_menu_unpost(struct ST_MENU *menu, bool close_active_submenu)
{
	/* hide active submenu */
	if (menu->active_submenu)
	{
		st_menu_unpost(menu->active_submenu, close_active_submenu);
		if (close_active_submenu)
			menu->active_submenu = NULL;
	}

	menu->mouse_row = -1;

	hide_panel(menu->panel);
	if (menu->shadow_panel)
		hide_panel(menu->shadow_panel);

	update_panels();
}

/*
 * Allow to set focus level for menu objects. This allow to
 * redirect events to some else where focus is not full.
 */
void
st_menu_set_focus(struct ST_MENU *menu, int focus)
{
	menu->focus = focus;
}

/*
 * The coordinates of subwin are not "correctly" refreshed, when
 * parent panel is moved. Maybe it is bug in ncurses, maybe not.
 * The new coordinates are calculated from parent and offset to parent
 * and difference between new and old coordinates is applyed on
 * x, y points.
 */
static void
add_correction(WINDOW *s, int *y, int *x)
{
	WINDOW *p = wgetparent(s);
	/*
	 * Note: function is_subwin is unknown on some
	 * older ncurses implementations. Don't use it.
	 */
	if (p)
	{
		int	py, px, sy, sx, oy, ox;
		int fix_y, fix_x;

		getbegyx(p, py, px);
		getbegyx(s, sy, sx);
		getparyx(s, oy, ox);

		fix_y = sy - (py + oy);
		fix_x = sx - (px + ox);

		*y += fix_y;
		*x += fix_x;
	}
}

/*
 * It is correction for window begxy, begx when panel contained
 * this window was moved.
 */
static void
subtract_correction(WINDOW *s, int *y, int *x)
{
	WINDOW *p = wgetparent(s);
	/*
	 * Note: function is_subwin is unknown on some
	 * older ncurses implementations. Don't use it.
	 */
	if (p)
	{
		int	py, px, sy, sx, oy, ox;
		int fix_y, fix_x;

		getbegyx(p, py, px);
		getbegyx(s, sy, sx);
		getparyx(s, oy, ox);

		fix_y = sy - (py + oy);
		fix_x = sx - (px + ox);

		*y -= fix_y;
		*x -= fix_x;
	}
}

/*
 * Handle any outer event - pressed key, or mouse event. This driver
 * doesn't handle shortcuts - shortcuts are displayed only.
 * is_top is true, when _st_menu_driver is called first time, when
 * it is called recursivly, then it is false. Only in top call, the
 * draw routines can be called.
 * when unpost_submenu is true, then driver should to unpost active
 * subwindow of current menu. This info is propagated back - the nested
 * element tell to owner, close me.
 */
static bool
_st_menu_driver(struct ST_MENU *menu, int c, bool alt, MEVENT *mevent,
					bool is_top, bool is_nested_pulldown,
					bool *unpost_submenu)
{
	ST_MENU_CONFIG	*config;

	int		cursor_row;				/* number of active menu item */
	bool	is_menubar;				/* true, when processed object is menu bar */
	int		first_row = -1;			/* number of row of first enabled item */
	int		last_row = -1;			/* last menu item */
	int		mouse_row = -1;			/* item number selected by mouse */
	int		search_row = -1;		/* code menu selected by accelerator */
	bool	found_row = false;		/* we found new active menu item (row var will be valid) */
	bool	post_menu = false;			/* when it is true, then assiciated pulldown menu will be posted */
	int		row;
	bool	processed = false;
	ST_MENU_ITEM	   *menu_items;

	/* reset globals */
	selected_item = NULL;
	press_accelerator = false;
	press_enter = false;
	button1_clicked = false;

	*unpost_submenu = false;

	/* maybe only cmdbar is used */
	if (!menu)
		goto post_process;

	config = menu->config;
	cursor_row = menu->cursor_row;		/* number of active menu item */
	is_menubar = menu->is_menubar;		/* true, when processed object is menu bar */

	/* Fucus filter */
	if ((menu->focus == ST_MENU_FOCUS_MOUSE_ONLY && c != KEY_MOUSE) ||
		(menu->focus == ST_MENU_FOCUS_ALT_MOUSE && c != KEY_MOUSE && !alt) ||
		(menu->focus == ST_MENU_FOCUS_NONE))
		goto post_process;

	/*
	 * Propagate event to nested active object first. When nested object would be
	 * closed, close it. When nested object read event, go to end
	 */
	if (menu->active_submenu)
	{
		bool	_is_nested_pulldown = is_nested_pulldown ? true : (is_menubar ? false : true);
		bool	unpost_submenu_loc = false;

		/*
		 * Key right is used in pulldown menu for access to nested menu.
		 * When nested menu is active already, then there is not any
		 * work on this level. KEY_RIGHT can buble to menubar and can
		 * be processed there.
		 */
		if (!is_menubar && c == KEY_RIGHT)
			goto post_process;

		/*
		 * Submenu cannot be top object. When now is not menu bar, then now should be
		 * pulldown menu, and nested object should be nested pulldown menu.
		 */
		processed = _st_menu_driver(menu->active_submenu, c, alt, mevent,
												false, _is_nested_pulldown, &unpost_submenu_loc);

		if (unpost_submenu_loc)
		{
			st_menu_unpost(menu->active_submenu, false);
			menu->active_submenu = NULL;
		}

		/*
		 * When we close some object, then we did some work on this
		 * level, and we should not do more work here.
		 */
		if (processed)
			goto post_process;
	}

	/*
	 * The checks of events, that can unpost this level menu. For unposting top
	 * object is responsible the user.
	 */
	if (!is_top)
	{
		if (c == ST_MENU_ESCAPE)
		{
			*unpost_submenu = true;

			/*
			 * escape should to close all opened objects, so we cannot to
			 * returns true (processed) due we are in top object.
			 */
			return is_top ? true : false;
		}

		if (c == KEY_MOUSE && mevent->bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED))
		{
			int		y = mevent->y;
			int		x = mevent->x;

			/*
			 * For some styles, the window is different than draw_area. The
			 * draw_area is subwindow of window. When main window is moved
			 * due moving panel (see adjust position), then subwindow has not
			 * expected coordinates. Following routine calculate fix between
			 * current draw_area coordinates and expected coordinates. Then
			 * apply this fix on mouse position.
			 */
			add_correction(menu->draw_area, &y, &x);

			if (!is_menubar && !wenclose(menu->draw_area, y, x))
			{
				*unpost_submenu = true;
				return false;
			}
		}

		/*
		 * Nested submenu can be unposted by pressing key left
		 */
		if (c == KEY_LEFT && is_nested_pulldown)
		{
			*unpost_submenu = true;
			return true;
		}
	}

	if (c == KEY_MOUSE)
	{

#if NCURSES_MOUSE_VERSION > 1

		if (mevent->bstate & BUTTON5_PRESSED)
		{
				c = KEY_DOWN;
		}
		else if (mevent->bstate & BUTTON4_PRESSED)
		{
			c = KEY_UP;
		}
		else

#endif

		if (mevent->bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED))
		{
			if (is_menubar)
			{
				/*
				 * On menubar level we can process mouse event if row is zero, or
				 * we can check if mouse is positioned inside draw area or active
				 * submenu. If not, then we should to unpost active submenu.
				 */
				if (mevent->y == 0)
				{
					int		i = 0;
					int		chars_before;

					/*
					 * How much chars before active item and following item
					 * specify range of menubar item.
					 */
					chars_before = (config->text_space != -1) ? (config->text_space / 2) : 1;

					menu_items = menu->menu_items;
					while (menu_items->text)
					{
						int		minx, maxx;

						/* first menubar field get mouse from left corner */
						minx = i > 0 ? (menu->bar_fields_x_pos[i] - chars_before) : 0;

						/* last menubar field get mouse to right corner */
						maxx = i + 1 < menu->nitems ? menu->bar_fields_x_pos[i + 1] - chars_before : mevent->x + 1;

						/* transform possitions to target menu code */
						if (mevent->x >= minx && mevent->x < maxx)
						{
							mouse_row = i + 1;
							break;
						}

						menu_items += 1;
						i = i + 1;
					}
				}
			}
			else
			{
				int		row_loc, col_loc;

				row_loc = mevent->y;
				col_loc = mevent->x;

				/* fix mouse coordinates, if draw_area has "wrong" coordinates */
				add_correction(menu->draw_area, &row_loc, &col_loc);

				/* calculate row from transformed mouse event */
				if (wmouse_trafo(menu->draw_area, &row_loc, &col_loc, false))
						mouse_row = row_loc + 1 - (config->draw_box ? 1:0) + (menu->first_row - 1);
			}
		}
	}
	else
		/* there are no mouse event, reset prev mouse row */
		menu->mouse_row = -1;

	/*
	 * Try to check if key is accelerator. This check should be on last level.
	 * So don't do it if submenu is active.
	 */
	if (c != KEY_MOUSE
			&& c != KEY_HOME && c != KEY_END
			&& c != KEY_UP && c != KEY_DOWN
			&& c != KEY_LEFT && c != KEY_RIGHT)
	{
		char	buffer[20];
		char   *pressed;
		int		l_pressed;
		int		i;

		/*
		 * accelerator can be alt accelerator for menuber or non alt, and
		 * the menu should not to have active submenu.
		 */
		if ((!alt && !menu->active_submenu) ||
				(alt && is_menubar))
		{
			l_pressed = wchar_to_utf8(config, buffer, 20, (wchar_t) c);
			buffer[l_pressed] = '\0';

			pressed = chr_casexfrm(config, (char *) buffer);
			l_pressed = strlen(pressed);

			for (i = 0; i < menu->naccelerators; i++)
			{
				if (menu->accelerators[i].length == l_pressed &&
					memcmp(menu->accelerators[i].c, pressed, l_pressed) == 0)
				{
					/* check if row is enabled */
					search_row = menu->accelerators[i].row;
					if (menu->options[search_row - 1] & ST_MENU_OPTION_DISABLED)
						/* revert back, found accelerator is for disabled menu item */
						search_row = -1;
					else
						break;
				}
			}

			free(pressed);

			/* Process key in this case only when we found accelerator */
			if (search_row != -1)
				processed = true;
		}
	}

	/*
	 * Iterate over menu items, and try to find next or previous row, code, or mouse
	 * row.
	 */
	menu_items = menu->menu_items; row = 1;
	while (menu_items->text != 0)
	{
		if (*menu_items->text != '\0' &&
				(strncmp(menu_items->text, "--", 2) != 0) &&
				((menu->options[row - 1] & ST_MENU_OPTION_DISABLED) == 0))
		{
			if (first_row == -1)
			{
				first_row = row;

				if (c == KEY_HOME && !is_menubar)
				{
					menu->cursor_row = first_row;
					found_row = true;
					processed = true;
					break;
				}
			}

			if (is_menubar)
			{
				if (c == KEY_RIGHT && row > cursor_row) 
				{
					menu->cursor_row = row;
					found_row = true;
					processed = true;
					break;
				}
				else if (c == KEY_LEFT && row == cursor_row)
				{
					if (last_row != -1)
					{
						menu->cursor_row = last_row;
						found_row = true;
						processed = true;
						break;
					}
				}
			}
			else
			{
				/* Is not menubar */
				if (c == KEY_DOWN && row > cursor_row)
				{
					menu->cursor_row = row;
					processed = true;
					found_row = true;
					break;
				}
				else if (c == KEY_UP && row == cursor_row)
				{
					if (last_row != -1)
					{
						menu->cursor_row = last_row;
						found_row = true;
						processed = true;
						break;
					}
					else
						c = KEY_END;
				}
			}

			if (mouse_row != -1 && row == mouse_row)
			{
				menu->cursor_row = row;
				found_row = true;
				processed = true;
				post_menu = true;

				if (mevent->bstate & BUTTON1_PRESSED)
				{
					menu->mouse_row = mouse_row;
				}
				else
				{
					/*
					 * Fully valid release event for transformation to
					 * clicked event is only event, when PRESSED row
					 * and released row is same.
					 */
					if (mevent->bstate& BUTTON1_RELEASED &&
							menu->mouse_row == mouse_row)
					{
						button1_clicked = true;
					}
					menu->mouse_row = -1;
				}
				break;
			}
			else if (search_row != -1 && row == search_row)
			{
				menu->cursor_row = row;
				press_accelerator = true;

				found_row = true;
				post_menu = true;
				processed = true;
				break;
			}

			last_row = row;
		}
		menu_items += 1;
		row += 1;
	}

	/*
	 * When rows not found, maybe we would to search limit points
	 * or we would to return back in ring buffer of menu items.
	 */
	if (!found_row)
	{
		if (is_menubar)
		{
			if (c == KEY_RIGHT)
			{
				menu->cursor_row = first_row;
				processed = true;
			}
			else if (c == KEY_LEFT)
			{
				menu->cursor_row = last_row;
				processed = true;
			}
		}
		else
		{
			if (c == KEY_END)
			{
				menu->cursor_row = last_row;
				processed = true;
			}
			else if (c == KEY_DOWN)
			{
				menu->cursor_row = first_row;
				processed = true;
			}
		}
	}



	/* when menubar is changed, unpost active pulldown submenu */
	if (menu->active_submenu && cursor_row != menu->cursor_row)
	{
		st_menu_unpost(menu->active_submenu, false);
		menu->active_submenu = NULL;

		/* remember, submenu was visible */
		post_menu = true;
	}

	/* enter has sense only on selectable menu item */
	if (c == 10 && menu->cursor_row != -1)
		press_enter = true;

	/*
	 * Some actions can activate submenu, check it and open it, if it
	 * is required.
	 */
	if (press_accelerator || 
			  (c == KEY_DOWN && is_menubar) ||
			  (c == KEY_RIGHT && !is_menubar) ||
			  c == 10 || post_menu)
	{
		menu->active_submenu = menu->submenus[menu->cursor_row - 1];
		if (menu->active_submenu)
		{
			/* when submenu is active, then reset accelerator and mouse flags */
			press_accelerator = false;
			press_enter = false;
			button1_clicked = false;
		}

		/*
		 * When mouse event opens or reopens submenu, then we take
		 * this event as processed event. Valid accelerator is processed
		 * always. Enter (c == 10) is processed always too.
		 */
		if (press_accelerator || c == 10)
			processed = true;
		else
			processed = menu->active_submenu != NULL;
	}

	/*
	 * We can set processed flag, when some mouse row was founded. That means
	 * so mouse click was somewhere to draw area.
	 */
	if (mouse_row != -1)
		processed = true;

post_process:

	/*
	 * show content, only top object is can do this - nested objects
	 * are displayed recursivly.
	 */
	if (is_top)
	{
		/* when we processed some event, then we usually got a full focus */
		if (processed)
			menu->focus = ST_MENU_FOCUS_FULL;
		else
		{
			/*
			 * When event was not processed by menubar, then we
			 * we can try to sent it to command bar. But with
			 * full focus, the menubar is hungry, and we send nothing.
			 */
			if (active_cmdbar)
			{
				if (!menu || menu->focus != ST_MENU_FOCUS_FULL)
					processed = cmdbar_driver(active_cmdbar, c, alt, mevent);
			}
		}

		if (processed)
		{
			/* try to search selected item */
			if (menu)
				searching_selected_refval_items(menu);
		}

		/* postprocess for referenced values */
		if (selected_item && (press_accelerator || press_enter || button1_clicked))
		{
			if (IS_REF_OPTION(selected_options))
			{
				if (selected_refval == NULL)
				{
					endwin();
					fprintf(stderr, "detected referenced option without referenced value");
					exit(1);
				}

				if (selected_options & ST_MENU_OPTION_MARKED_REF)
				{
					*selected_refval = selected_item->data;
				}
				else if ((selected_options & ST_MENU_OPTION_SWITCH2_REF) ||
						 (selected_options & ST_MENU_OPTION_SWITCH3_REF))
				{
					*selected_refval = (*selected_refval == 1) ? 0 : 1;
				}
			}
		}

		/*
		 * command bar should be drawed first - because it is deeper
		 * than pulldown menu
		 */
		if (active_cmdbar)
			cmdbar_draw(active_cmdbar);

		if (menu)
		{
			if (menu->is_menubar)
				menubar_draw(menu);
			else
				pulldownmenu_draw(menu, true);

			/* eat all keyboard input, when focus is full on top level */
			if (c != KEY_MOUSE && c != KEY_RESIZE &&
					c != ST_MENU_ESCAPE &&
					menu->focus == ST_MENU_FOCUS_FULL)
				processed = true;
		}
	}

	return processed;
}

bool
st_menu_driver(struct ST_MENU *menu, int c, bool alt, MEVENT *mevent)
{
	bool		aux_unpost_submenu = false;

	/*
	 * We should to complete mouse click based on two
	 * events. Mouse click is valid if press, release
	 * was related with same command. Now, when mouse
	 * is pressed, we don't know a related object, but
	 * we can reset selected_command variable.
	 */
	if (mevent->bstate & BUTTON1_PRESSED)
		selected_command = NULL;

	/*
	 * We would to close pulldown menus on F10 key - similar behave
	 * like SCAPE, so we can translate F10 event to ST_MENU_ESCAPE,
	 * when menubar can accept these keys (based on focus).
	 */
	if (menu && KEY_F(10) == c && menu->focus == ST_MENU_FOCUS_FULL)
		c = ST_MENU_ESCAPE;

	return _st_menu_driver(menu, c, alt, mevent, true, false, &aux_unpost_submenu);
}

/*
 * Create state variable for pulldown menu. It based on template - a array of ST_MENU_ITEM fields.
 * The initial position can be specified. The config (specify desplay properties) should be
 * passed. The config can be own or preloaded from preddefined styles by function st_menu_load_style.
 * a title is not supported yet.
 */
struct ST_MENU *
st_menu_new(ST_MENU_CONFIG *config, ST_MENU_ITEM *menu_items, int begin_y, int begin_x, char *title)
{
	struct ST_MENU *menu;
	int		rows, cols;
	ST_MENU_ITEM *menu_item;
	int		menu_fields = 0;
	int		i;

	menu = safe_malloc(sizeof(struct ST_MENU));

	menu->menu_items = menu_items;
	menu->config = config;
	menu->title = title;
	menu->naccelerators = 0;
	menu->is_menubar = false;
	menu->mouse_row = -1;
	menu->first_row = 1;

	/* how much items are in template */
	menu_item = menu_items;
	while (menu_item->text != NULL)
	{
		menu_fields += 1;
		menu_item += 1;
	}

	/* preallocate good enough memory */
	menu->accelerators = safe_malloc(sizeof(ST_MENU_ACCELERATOR) * menu_fields);
	menu->submenus = safe_malloc(sizeof(struct ST_MENU) * menu_fields);
	menu->options = safe_malloc(sizeof(int) * menu_fields);
	menu->refvals = safe_malloc(sizeof(int*) * menu_fields);

	menu->nitems = menu_fields;

	/* get pull down menu dimensions */
	pulldownmenu_content_size(config, menu_items, &rows, &cols,
							&menu->shortcut_x_pos, &menu->item_x_pos,
							menu->accelerators, &menu->naccelerators,
							&menu->cursor_row);

	if (config->draw_box)
	{
		rows += 2;
		cols += 2;
	}

	if (config->wide_vborders)
		cols += 2;
	if (config->wide_hborders)
		rows += 2;

	/* Prepare property for menu shadow */
	if (config->shadow_width > 0)
	{
		menu->shadow_window = newwin2(&rows, &cols, begin_y + 1, begin_x + config->shadow_width);
		menu->shadow_panel = new_panel(menu->shadow_window);

		hide_panel(menu->shadow_panel);
		wbkgd(menu->shadow_window, COLOR_PAIR(config->menu_shadow_cpn) | config->menu_shadow_attr);

		wnoutrefresh(menu->shadow_window);
	}
	else
	{
		menu->shadow_window = NULL;
		menu->shadow_panel = NULL;
	}

	menu->window = newwin2(&rows, &cols, begin_y, begin_x);

	menu->ideal_y_pos = begin_y;
	menu->ideal_x_pos = begin_x;
	menu->rows = rows;
	menu->cols = cols;

	wbkgd(menu->window, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);
	wnoutrefresh(menu->window);

	/*
	 * Initialize submenu states (nested submenus)
	 */
	menu_item = menu_items;
	i = 0;
	while (menu_item->text)
	{
		if (menu_item->submenu)
		{
			menu->submenus[i] = 
					st_menu_new(config, menu_item->submenu,
										begin_y + i + config->submenu_offset_y
										+ (config->draw_box ? 1 : 0)
										+ (config->wide_vborders ? 1 : 0),
										begin_x + cols + config->submenu_offset_x,
										NULL);
		}
		else
			menu->submenus[i] = NULL;

		menu->options[i] = menu_item->options;
		menu->refvals[i] = NULL;

		menu_item += 1;
		i += 1;
	}

	/* draw area can be same like window or smaller */
	if (config->wide_vborders || config->wide_hborders)
	{
		menu->draw_area = derwin(menu->window,
			rows - (config->wide_hborders ? 2 : 0),
			cols - (config->wide_vborders ? 2 : 0),
			config->wide_hborders ? 1 : 0,
			config->wide_vborders ? 1 : 0);

		wbkgd(menu->draw_area, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);

		wnoutrefresh(menu->draw_area);
	}
	else
		menu->draw_area = menu->window;

	menu->panel = new_panel(menu->window);
	hide_panel(menu->panel);

	return menu;
}

/*
 * Create state variable for menubar based on template (array) of ST_MENU_ITEM
 */
struct ST_MENU *
st_menu_new_menubar2(ST_MENU_CONFIG *barcfg, ST_MENU_CONFIG *pdcfg, ST_MENU_ITEM *menu_items)
{
	struct ST_MENU *menu;
	int		maxy, maxx;
	ST_MENU_ITEM *menu_item;
	int		menu_fields = 0;
	int		aux_width = 0;
	int		text_space;
	int		current_pos;
	int		i = 0;
	int		naccel = 0;

	if (pdcfg == NULL)
		pdcfg = barcfg;

	getmaxyx(stdscr, maxy, maxx);

	menu = safe_malloc(sizeof(struct ST_MENU));

	maxy = 1;
	menu->window = newwin2(&maxy, &maxx, 0, 0);
	menu->panel = new_panel(menu->window);

	/* there are not shadows */
	menu->shadow_window = NULL;
	menu->shadow_panel = NULL;

	menu->config = barcfg;
	menu->menu_items = menu_items;
	menu->cursor_row = 1;
	menu->first_row = 1;
	menu->active_submenu = NULL;

	menu->is_menubar = true;
	menu->mouse_row = -1;

	wbkgd(menu->window, COLOR_PAIR(barcfg->menu_background_cpn) | barcfg->menu_background_attr);

	menu_item = menu_items;
	while (menu_item->text)
	{
		menu_fields += 1;

		if (barcfg->text_space == -1)
			aux_width += menutext_displaywidth(barcfg, menu_item->text, NULL, NULL);

		menu_item += 1;
	}

	/*
	 * last bar position is hypotetical - we should not to calculate length of last field
	 * every time.
	 */
	menu->bar_fields_x_pos = safe_malloc(sizeof(int) * (menu_fields + 1));
	menu->submenus = safe_malloc(sizeof(struct ST_MENU) * menu_fields);
	menu->accelerators = safe_malloc(sizeof(ST_MENU_ACCELERATOR) * menu_fields);
	menu->options = safe_malloc(sizeof(int) * menu_fields);
	menu->refvals = safe_malloc(sizeof(int*) * menu_fields);

	menu->nitems = menu_fields; 

	/*
	 * When text_space is not defined, then try to vallign menu items
	 */
	if (barcfg->text_space == -1)
	{
		text_space = (maxx + 1 - aux_width) / (menu_fields + 1);
		if (text_space < 4)
			text_space = 4;
		else if (text_space > 15)
			text_space = 15;
		current_pos = text_space;
	}
	else
	{
		text_space = barcfg->text_space;
		current_pos = barcfg->init_text_space;
	}

	/* Initialize submenu */
	menu_item = menu_items; i = 0;
	while (menu_item->text)
	{
		char	*accelerator;

		menu->bar_fields_x_pos[i] = current_pos;
		current_pos += menutext_displaywidth(barcfg, menu_item->text, &accelerator, NULL);
		current_pos += text_space;
		if (menu_item->submenu)
		{
			menu->submenus[i] = 
					st_menu_new(pdcfg, menu_item->submenu,
										1, menu->bar_fields_x_pos[i] + 
										pdcfg->menu_bar_menu_offset
										- (pdcfg->draw_box ? 1 : 0)
										- (pdcfg->wide_vborders ? 1 : 0)
										- (pdcfg->extra_inner_space ? 1 : 0) - 1, NULL);
		}
		else
			menu->submenus[i] = NULL;

		if (accelerator)
		{
			menu->accelerators[naccel].c = chr_casexfrm(barcfg, accelerator);
			menu->accelerators[naccel].length = strlen(menu->accelerators[naccel].c);
			menu->accelerators[naccel++].row = i + 1;
		}

		menu->naccelerators = naccel;

		menu->options[i] = menu_item->options;
		menu->refvals[i] = NULL;

		menu_item += 1;
		i += 1;
	}

	/*
	 * store hypotetical x bar position
	 */
	menu->bar_fields_x_pos[i] = current_pos;

	return menu;
}

struct ST_MENU *
st_menu_new_menubar(ST_MENU_CONFIG *config, ST_MENU_ITEM *menu_items)
{
	return st_menu_new_menubar2(config, NULL, menu_items);
}

/*
 * Remove all objects allocated by menu and nested objects
 * it is workhorse for st_menu_free
 */
static void
_st_menu_free(struct ST_MENU *menu)
{
	int		i;

	if (menu)
	{
		if (menu->submenus)
		{
			for (i = 0; i < menu->nitems; i++)
				st_menu_free(menu->submenus[i]);

			free(menu->submenus);
		}

		if (menu->accelerators)
			free(menu->accelerators);

		if (menu->shadow_panel)
			del_panel(menu->shadow_panel);
		if (menu->shadow_window)
			delwin(menu->shadow_window);

		del_panel(menu->panel);
		delwin(menu->window);

		free(menu);
	}
}

void
st_menu_free(struct ST_MENU *menu)
{
	_st_menu_free(menu);

	update_panels();
}

/*
 * Returns active item and info about selecting of active item
 */
ST_MENU_ITEM *
st_menu_selected_item(bool *activated)
{
	/*
	 * Activated can be true only when selected_item is valid
	 */
	if (selected_item)
		*activated = press_accelerator || press_enter || button1_clicked;
	else
		*activated = false;

	return selected_item;
}

/*
 * Set flag of first menu item specified by code
 */
bool
st_menu_enable_option(struct ST_MENU *menu, int code, int option)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	while (menu_items->text)
	{
		if (menu_items->code == code)
		{
			menu->options[i] |= option;
			return true;
		}

		if (menu->submenus[i])
			if (st_menu_enable_option(menu->submenus[i], code, option))
				return true;

		menu_items += 1;
		i += 1;
	}

	return false;
}

/*
 * Reset flag of first menu item specified by code
 */
bool
st_menu_reset_option(struct ST_MENU *menu, int code, int option)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	while (menu_items->text)
	{
		if (menu_items->code == code)
		{
			menu->options[i] &= ~option;
			return true;
		}

		if (menu->submenus[i])
			if (st_menu_reset_option(menu->submenus[i], code, option))
				return true;

		menu_items += 1;
		i += 1;
	}

	return false;
}

/*
 * Reset flag of first menu item specified by code
 */
bool
st_menu_reset_all_options(struct ST_MENU *menu, int option)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	while (menu_items->text)
	{
		menu->options[i] &= ~option;

		if (menu->submenus[i])
			st_menu_reset_all_options(menu->submenus[i], option);

		menu_items += 1;
		i += 1;
	}

	return false;
}


/*
 * Reset flag of first menu item specified by code
 */
bool
st_menu_reset_all_submenu_options(struct ST_MENU *menu, int menu_code, int option)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	while (menu_items->text)
	{
		if (menu->submenus[i])
		{
			if (menu_items->code == menu_code)
			{
				st_menu_reset_all_options(menu->submenus[i], option);
				return true;
			}

			if (st_menu_reset_all_submenu_options(menu->submenus[i], menu_code, option))
				return true;
		}

		menu_items += 1;
		i += 1;
	}

	return false;
}

/*
 * Set flag of first menu item specified by code
 */
bool
st_menu_set_option(struct ST_MENU *menu, int code, int option, bool value)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	while (menu_items->text)
	{
		if (menu_items->code == code)
		{
			if (value)
				menu->options[i] |= option;
			else
				menu->options[i] &= ~option;

			return true;
		}

		if (menu->submenus[i])
			if (st_menu_set_option(menu->submenus[i], code, option, value))
				return true;

		menu_items += 1;
		i += 1;
	}

	return false;
}

/*
 * Reduce string to expected display width. The buffer should be
 * preallocated on good enough length - size of src.
 */
static void
reduce_string(ST_MENU_CONFIG *config, int display_width, char *dest, char *src)
{
	int current_width = str_width(config, src);
	int		char_count = 0;

	while (src && display_width > 0)
	{
		if (current_width <= display_width)
		{
			strcpy(dest, src);
			return;
		}
		else
		{
			int		chrlen = char_length(config, src);
			int		dw = char_width(config, src);

			if (char_count < 2)
			{
				memcpy(dest, src, chrlen);
				dest += chrlen;
				display_width -= dw;
			}
			else if (char_count == 2)
			{
				*dest++ = '~';
				display_width -= 1;
			}

			char_count += 1;
			current_width -= dw;
			src += chrlen;
		}
	}

	*dest = '\0';
}

static void
cmdbar_draw(struct ST_CMDBAR *cmdbar)
{
	ST_MENU_CONFIG *config = cmdbar->config;
	int		i;

	show_panel(cmdbar->panel);
	top_panel(cmdbar->panel);

	update_panels();

	werase(cmdbar->window);

	if (config->funckey_bar_style)
	{
		for (i = 0; i < cmdbar->nitems; i++)
		{
			wmove(cmdbar->window, 0, cmdbar->positions[i]);
			wattron(cmdbar->window,
					  COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

			wprintw(cmdbar->window, "%2d", i+1);

			wattroff(cmdbar->window,
					  COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

			if (cmdbar->labels[i])
				waddstr(cmdbar->window, cmdbar->labels[i]);
		}
	}
	else
	{
		for (i = 0; i < cmdbar->nitems; i++)
		{
			bool	need_sep = false;
			bool	marked = false;
			int		accel_prop;
			int		text_prop;

			marked = &cmdbar->cmdbar_items[i] == selected_command && !command_was_activated;

			if (marked)
			{
				mvwchgat(cmdbar->window, 0,
							cmdbar->positions[i] - 1,
							cmdbar->positions[i+1] - config->text_space + 1 - cmdbar->positions[i] + 1,
							config->cursor_attr,
							config->cursor_cpn, NULL);

				accel_prop = COLOR_PAIR(config->cursor_accel_cpn) | config->cursor_accel_attr;
				text_prop = COLOR_PAIR(config->cursor_cpn) | config->cursor_attr;
			}
			else
			{

				accel_prop = COLOR_PAIR(config->accelerator_cpn) | config->accelerator_attr;
				text_prop = COLOR_PAIR(config->menu_unfocused_cpn) | config->menu_unfocused_attr;
			}

			wmove(cmdbar->window, 0, cmdbar->positions[i]);

			wattron(cmdbar->window, accel_prop);

			if (cmdbar->cmdbar_items[i].alt)
			{
				need_sep = true;
				waddstr(cmdbar->window, "M-");
			}
			if (cmdbar->cmdbar_items[i].fkey > 0)
			{
				need_sep = true;
				wprintw(cmdbar->window, "F%d", cmdbar->cmdbar_items[i].fkey);
			}

			wattroff(cmdbar->window, accel_prop);

			wattron(cmdbar->window, text_prop);

			if (need_sep)
				waddstr(cmdbar->window, " ");

			waddstr(cmdbar->window, cmdbar->cmdbar_items[i].text);

			wattroff(cmdbar->window, text_prop);
		}
	}

	wnoutrefresh(cmdbar->window);
}

static bool
cmdbar_driver(struct ST_CMDBAR *cmdbar, int c, bool alt, MEVENT *mevent)
{
	ST_CMDBAR_ITEM *cmdbar_item = cmdbar->cmdbar_items;
	ST_MENU_CONFIG *config = cmdbar->config;

	if (c == KEY_MOUSE && mevent->bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED))
	{
		int		y = mevent->y;
		int		x = mevent->x;
		int		i;

		if (!wenclose(cmdbar->window, y, x))
		{
			command_was_activated = true;
			return false;
		}

		for (i = 0; i < cmdbar->nitems; i++)
		{
			int		begin_x = i > 0 ? cmdbar->positions[i] : 0;
			int		next_begin_x = cmdbar->positions[i + 1];

			if (!config->funckey_bar_style)
			{
				begin_x -= 1;
				next_begin_x -= 1;
			}

			if (begin_x <= x && x < next_begin_x)
			{
				if (config->funckey_bar_style)
				{
					if (cmdbar->labels[i])
					{
						/*
						 * This design is not exact, but it is good enough.
						 * The click is valid, when press and release is over same
						 * object.
						 */
						if (mevent->bstate & BUTTON1_PRESSED)
						{
							command_was_activated = false;
							selected_command = cmdbar->ordered_items[i];
							return true;
						}
						else if (mevent->bstate & BUTTON1_RELEASED)
						{
							if (selected_command == cmdbar->ordered_items[i])
							{
								command_was_activated = true;
								return true;
							}
						}
					}
				}
				else
				{
					if (mevent->bstate & BUTTON1_PRESSED)
					{
						command_was_activated = false;
						selected_command = &cmdbar->cmdbar_items[i];
						return true;
					}
					else if (mevent->bstate & BUTTON1_RELEASED)
					{
						if (selected_command == &cmdbar->cmdbar_items[i])
						{
							command_was_activated = true;
							return true;
						}
					}
				}
			}
		}

		selected_command = NULL;
		return true;
	}
	else
	{
		while (cmdbar_item->text)
		{
			if (cmdbar_item->alt == alt && KEY_F(cmdbar_item->fkey) == c)
			{
				command_was_activated = true;
				selected_command = cmdbar_item;
				return true;
			}
			cmdbar_item += 1;
		}
	}

	return false;
}

ST_CMDBAR_ITEM *
st_menu_selected_command(bool *activated)
{
	*activated = selected_command != NULL && command_was_activated;

	return selected_command;
}

/*
 * Create state variable for commandbar. It based on template - a array of ST_CMDBAR_ITEM fields.
 */
struct ST_CMDBAR *
st_cmdbar_new(ST_MENU_CONFIG *config, ST_CMDBAR_ITEM *cmdbar_items)
{
	struct ST_CMDBAR *cmdbar;
	ST_CMDBAR_ITEM *cmdbar_item;
	int		maxy, maxx, tmpy;
	int		i;
	int		last_position;

	cmdbar = safe_malloc(sizeof(struct ST_CMDBAR));

	cmdbar->cmdbar_items = cmdbar_items;
	cmdbar->config = config;

	getmaxyx(stdscr, maxy, maxx);

	tmpy = 1;
	cmdbar->window = newwin2(&tmpy, &maxx, maxy - 1, 0);
	cmdbar->panel = new_panel(cmdbar->window);

	wbkgd(cmdbar->window,
					  COLOR_PAIR(config->menu_unfocused_cpn) |
					  config->menu_unfocused_attr);

	werase(cmdbar->window);

	cmdbar->nitems = 0;

	cmdbar_item = cmdbar_items;

	if (!config->funckey_bar_style)
	{
		while (cmdbar_item->text)
		{
			cmdbar->nitems += 1;
			cmdbar_item += 1;
		}
	}
	else
		cmdbar->nitems = 10;

	cmdbar->positions = safe_malloc(sizeof(int) * (cmdbar->nitems + 1));
	cmdbar->labels = safe_malloc(sizeof(char*) * cmdbar->nitems);
	cmdbar->ordered_items = safe_malloc(sizeof(ST_CMDBAR_ITEM *) * cmdbar->nitems);
	last_position = 0;

	if (config->funckey_bar_style)
	{
		int		width = maxx / 10;
		double	extra_width = (maxx % 10) / 10.0;
		double	extra_width_sum = 0;

		if (width < 7)
		{
			/* when terminal is too thin, don't show all fields */
			cmdbar->nitems = maxx / 7;

			width = maxx / cmdbar->nitems;
			extra_width = (maxx % cmdbar->nitems) / (cmdbar->nitems * 1.0);
			extra_width_sum = 0;
		}

		for (i = 0; i < cmdbar->nitems; i++)
		{
			cmdbar->positions[i] = last_position;
			last_position += width;
			extra_width_sum += extra_width;
			if (extra_width_sum > 1.0)
			{
				last_position += 1;
				extra_width_sum -= 1;
			}
		}

		cmdbar->positions[cmdbar->nitems] = maxx + 1;

		cmdbar_item = cmdbar_items;
		while (cmdbar_item->text)
		{
			int		fkey = cmdbar_item->fkey;
			int		display_width;

			if (cmdbar_item->alt)
			{
				endwin();
				fprintf(stderr, "Alt is not supported in funckey bar style");
				exit(1);
			}

			if (fkey < 1 || fkey > 10)
			{
				endwin();
				fprintf(stderr, "fkey code should be between 1 and 10");
				exit(1);
			}

			/* don't display keys in reduced bar */
			if (fkey > cmdbar->nitems)
			{
				cmdbar_item += 1;
				continue;
			}

			if (cmdbar->labels[fkey - 1])
			{
				endwin();
				fprintf(stderr, "multiple assigned items inside funckey bar");
				exit(1);
			}

			cmdbar->ordered_items[fkey - 1] = cmdbar_item;

			display_width = cmdbar->positions[fkey] - cmdbar->positions[fkey - 1] - 2;
			cmdbar->labels[fkey - 1] = safe_malloc(strlen(cmdbar_item->text) + 1);
			reduce_string(config, display_width, cmdbar->labels[fkey - 1], cmdbar_item->text);

			cmdbar_item += 1;
		}
	}
	else
	{
		last_position = config->init_text_space;

		for (i = 0; i < cmdbar->nitems; i++)
		{
			cmdbar_item = &cmdbar_items[i];

			cmdbar->positions[i] = last_position;

			if (cmdbar_item->alt)
				last_position += strlen("M-");
			if (cmdbar_item->fkey > 0)
				last_position += strlen("Fx");
			if (cmdbar_item->fkey > 9)
				last_position += strlen("0");

			if (cmdbar->positions[i] != last_position)
				last_position += 1;

			last_position += str_width(config, cmdbar_item->text);
			last_position += config->text_space != -1 ? config->text_space : 3;
		}

		cmdbar->positions[cmdbar->nitems] = last_position;
	}

	return cmdbar;
}

void
st_cmdbar_post(struct ST_CMDBAR *cmdbar)
{
	active_cmdbar = cmdbar;
	cmdbar_draw(cmdbar);
}

void
st_cmdbar_unpost(struct ST_CMDBAR *cmdbar)
{
	active_cmdbar = NULL;

	hide_panel(cmdbar->panel);
	update_panels();
}

void
st_cmdbar_free(struct ST_CMDBAR *cmdbar)
{
	int		i;

	active_cmdbar = NULL;

	del_panel(cmdbar->panel);
	delwin(cmdbar->window);

	for (i = 0; i < cmdbar->nitems; i++)
		if (cmdbar->labels[i])
			free(cmdbar->labels[i]);

	free(cmdbar->labels);
	free(cmdbar->positions);
	free(cmdbar->ordered_items);
	free(cmdbar);

	update_panels();
}

/*
 * Set reference to some variable for menu's option
 */
bool
st_menu_set_ref_option(struct ST_MENU *menu,
					   int code,
					   int option,
					   int *refvalue)
{
	ST_MENU_ITEM *menu_items = menu->menu_items;
	int		i = 0;

	if (!IS_REF_OPTION(option))
	{
		endwin();
		fprintf(stderr, "cannot assign reference value with not reference option");
		exit(1);
	}

	while (menu_items->text)
	{
		if (menu_items->code == code)
		{
			menu->options[i] |= option;
			menu->refvals[i] = refvalue;

			return true;
		}

		if (menu->submenus[i])
			if (st_menu_set_ref_option(menu->submenus[i], code, option, refvalue))
				return true;

		menu_items += 1;
		i += 1;
	}

	return false;
}
