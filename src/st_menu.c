#include <ctype.h>
#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "unicode.h"

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
	int			cursor_row;
	int			mouse_row;						/* mouse row where button1 was pressed */
	int		   *options;						/* state options, initially copyied from menu */
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
	char	   *title;
	bool		is_menubar;
	struct ST_MENU	*active_submenu;
	struct ST_MENU	**submenus;
};

static ST_MENU_ITEM	   *selected_item = NULL;
static bool			press_accelerator = false;
static bool			button1_clicked = false;
static bool			press_enter = false;

static inline int char_length(ST_MENU_CONFIG *config, const char *c);
static inline int char_width(ST_MENU_CONFIG *config, char *c, int bytes);
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
		result = utf8charlen(*((char *) c));
		if (result > 0)
			return result;
	}

	return 1;
}

/*
 * Retuns display width of char
 */
static inline int
char_width(ST_MENU_CONFIG *config, char *c, int bytes)
{
	if (!config->force8bit)
		return utf_dsplen((const char *) c);

	return 1;
}

/*
 * returns display width of string
 */
static inline int
str_width(ST_MENU_CONFIG *config, char *str)
{
	if (!config->force8bit)
		return utf_string_dsplen((const char *) str, strlen(str));

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
	size_t	length;

	if (!config->force8bit)
	{
		int	fold = utf8_tofold((const char *) str);

		*((int *) buffer) = fold;
		buffer[sizeof(int)] = '\0';

		result = strdup(buffer);
	}
	else
	{
		buffer[0] = tolower(str[0]);
		buffer[1] = '\0';

		result = strdup(buffer);
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
		char *ptr = unicode_to_utf8(wch, (unsigned char *) str);
		result = ptr - str;
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

	if (write_pos >= max_rows)
	{
		endwin();
		printf("FATAL: Cannot save menu positions, too complex menu.\n");
		exit(1);
	}

	cursor_rows[write_pos++] = menu->cursor_row;

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

/*
 * Workhorse for st_menu_load
 */
static int
_load_menustate(struct ST_MENU *menu, int *cursor_rows, int read_pos)
{
	int		active_row;
	int		i;

	menu->cursor_row = cursor_rows[read_pos++];

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

/*
 * Serialize important fields of menustate to cursor_rows array.
 */
void
st_menu_save(struct ST_MENU *menu, int *cursor_rows, int max_rows)
{
	_save_menustate(menu, cursor_rows, max_rows, 0);
}

/*
 * Load cursor positions and active submenu from safe
 */
void
st_menu_load(struct ST_MENU *menu, int *cursor_rows)
{
	_load_menustate(menu, cursor_rows, 0);
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
		result += char_width(config, text, bytes);
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

	int		i;

	selected_item = NULL;

	werase(menu->window);

	i = 0;
	while (menu_item->text)
	{
		char	*text = menu_item->text;
		bool	highlight = false;
		bool	is_cursor_row = menu->cursor_row == i + 1;
		bool	is_disabled = menu_item->options & ST_MENU_OPTION_DISABLED;
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

				if (!is_disabled)
				{
					if (!highlight)
					{
						wattron(menu->window,
							COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
									   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
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

/*
 * adjust pulldown possition - move panels from ideal position to any position
 * where can be fully displayed.
 */
static void
pulldownmenu_ajust_position(struct ST_MENU *menu, int maxy, int maxx)
{
	ST_MENU_CONFIG	*config = menu->config;

	int		y, x;
	int		new_y, new_x;

	getbegyx(menu->window, y, x);

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

			if (new_cols != smaxx || new_rows != smaxy)
			{
				WINDOW   *new_shadow_window;

				new_shadow_window = newwin(new_rows, new_cols, new_y + 1, new_x + config->shadow_width);

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
		int		i;

		getmaxyx(menu->shadow_window, smaxy, smaxx);

		show_panel(menu->shadow_panel);
		top_panel(menu->shadow_panel);

		/* desktop_win must be global */
		if (desktop_win)
			overwrite(desktop_win, menu->shadow_window);
		else
			werase(menu->shadow_window);

		for (i = 0; i <= smaxy; i++)
			mvwchgat(menu->shadow_window, i, 0, smaxx,
							config->menu_shadow_attr,
							config->menu_shadow_cpn,
							NULL);

		wnoutrefresh(menu->shadow_window);
	}

	if (menu->active_submenu)
		pulldownmenu_draw_shadow(menu->active_submenu);
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
	int		row = 1;
	int		maxy, maxx;
	int		text_min_x, text_max_x;

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

	werase(menu->window);

	getmaxyx(draw_area, maxy, maxx);

	/* be compiler quiet */
	(void) maxy;

	if (draw_box)
		box(draw_area, 0, 0);

	text_min_x = (draw_box ? 1 : 0) + (config->extra_inner_space ? 1 : 0);
	text_max_x = maxx - (draw_box ? 1 : 0) - (config->extra_inner_space ? 1 : 0);

	while (menu_items->text != NULL)
	{
		bool	has_submenu = menu_items->submenu ? true : false;
		bool	is_disabled = menu_items->options & ST_MENU_OPTION_DISABLED;

		if (*menu_items->text == '\0' || strncmp(menu_items->text, "--", 2) == 0)
		{
			int		i;

			if (draw_box)
			{
				wmove(draw_area, row, 0);
				waddch(draw_area, ACS_LTEE);
			}
			else
				wmove(draw_area, row - 1, 0);

			for(i = 0; i < maxx - 1 - (draw_box ? 1 : -1); i++)
				waddch(draw_area, ACS_HLINE);

			if (draw_box)
				waddch(draw_area, ACS_RTEE);
		}
		else
		{
			char	*text = menu_items->text;
			bool	highlight = false;
			bool	is_cursor_row = menu->cursor_row == row;
			bool	first_char = true;
			bool	is_extern_accel;

			if (is_cursor_row)
			{
				mvwchgat(draw_area, row - (draw_box ? 0 : 1), text_min_x, text_max_x - text_min_x,
						config->cursor_attr, config->cursor_cpn, NULL);
				wattron(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

				selected_item = menu_items;
			}

			if (is_disabled)
				wattron(menu->window, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);

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

			if (is_cursor_row)
				wattroff(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

			if (is_disabled)
				wattroff(menu->window, COLOR_PAIR(config->disabled_cpn) | config->disabled_attr);
		}

		menu_items += 1;
		row += 1;
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
st_menu_set_desktop_panel(PANEL *pan)
{
	desktop_win = panel_window(pan);
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
	ST_MENU_CONFIG	*config = menu->config;

	int		cursor_row = menu->cursor_row;		/* number of active menu item */
	bool	is_menubar = menu->is_menubar;		/* true, when processed object is menu bar */
	int		first_row = -1;							/* number of row of first enabled item */
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

	/*
	 * Propagate event to nested active object first. When nested object would be
	 * closed, close it. When nested object read event, go to end
	 */
	if (menu->active_submenu)
	{
		bool	_is_nested_pulldown = is_nested_pulldown ? true : (is_menubar ? false : true);
		bool	unpost_submenu = false;

		/*
		 * Key right is used in pulldown menu for access to nested menu.
		 * When nested menu is active already, then there is not any
		 * work on this level. KEY_RIGHT can buble to menubar and can
		 * be processed there.
		 */
		if (!is_menubar && c == KEY_RIGHT)
			goto draw_object;

		/*
		 * Submenu cannot be top object. When now is not menu bar, then now should be
		 * pulldown menu, and nested object should be nested pulldown menu.
		 */
		processed = _st_menu_driver(menu->active_submenu, c, alt, mevent,
												false, _is_nested_pulldown, &unpost_submenu);

		if (unpost_submenu)
		{
			st_menu_unpost(menu->active_submenu, false);
			menu->active_submenu = NULL;
		}

		/*
		 * When we close some object, then we did some work on this
		 * level, and we should not do more work here.
		 */
		if (processed)
			goto draw_object;
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
			return true;
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

						minx = i > 0 ? (menu->bar_fields_x_pos[i] - chars_before) : 0;
						maxx = menu->bar_fields_x_pos[i + 1] - chars_before;

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
				int		row, col;

				row = mevent->y;
				col = mevent->x;

				/* fix mouse coordinates, if draw_area has "wrong" coordinates */
				add_correction(menu->draw_area, &row, &col);

				/* calculate row from transformed mouse event */
				if (wmouse_trafo(menu->draw_area, &row, &col, false))
						mouse_row = row + 1 - (config->draw_box ? 1:0);
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

draw_object:

	/*
	 * show content, only top object is can do this - nested objects
	 * are displayed recursivly.
	 */
	if (is_top)
	{
		if (menu->is_menubar)
			menubar_draw(menu);
		else
			pulldownmenu_draw(menu, true);
	}

	return processed;
}

bool
st_menu_driver(struct ST_MENU *menu, int c, bool alt, MEVENT *mevent)
{
	bool aux_unpost_submenu = false;

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
		menu->shadow_window = newwin(rows, cols, begin_y + 1, begin_x + config->shadow_width);
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

	menu->window = newwin(rows, cols, begin_y, begin_x);

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
struct
ST_MENU *st_menu_new_menubar2(ST_MENU_CONFIG *barcfg, ST_MENU_CONFIG *pdcfg, ST_MENU_ITEM *menu_items)
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

	/* by compiler quiet */
	(void) maxy;

	menu = safe_malloc(sizeof(struct ST_MENU));

	menu->window = newwin(1, maxx, 0, 0);
	menu->panel = new_panel(menu->window);

	/* there are not shadows */
	menu->shadow_window = NULL;
	menu->shadow_panel = NULL;

	menu->config = barcfg;
	menu->menu_items = menu_items;
	menu->cursor_row = 1;
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
st_menu_set_option(struct ST_MENU *menu, int code, int option)
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
			if (st_menu_set_option(menu->submenus[i], code, option))
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
			if (st_menu_set_option(menu->submenus[i], code, option))
				return true;

		menu_items += 1;
		i += 1;
	}

	return false;
}
