/*-------------------------------------------------------------------------
 *
 * config.h
 *	  load/save configuration
 *
 * Portions Copyright (c) 2017-2022 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/config.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PSPG_CONFIG_H
#define PSPG_CONFIG_H

#include <stdbool.h>

#define MAX_STYLE					23

typedef enum
{
	CLIPBOARD_FORMAT_CSV,
	CLIPBOARD_FORMAT_TSVC,
	CLIPBOARD_FORMAT_TEXT,
	CLIPBOARD_FORMAT_PIPE_SEPARATED,
	CLIPBOARD_FORMAT_SQL_VALUES,
	CLIPBOARD_FORMAT_INSERT,
	CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS
} ClipboardFormat;

#define DSV_FORMAT_TYPE(f)		(f == CLIPBOARD_FORMAT_CSV || f == CLIPBOARD_FORMAT_TSVC || f == CLIPBOARD_FORMAT_SQL_VALUES)
#define INSERT_FORMAT_TYPE(f)	(f == CLIPBOARD_FORMAT_INSERT || f == CLIPBOARD_FORMAT_INSERT_WITH_COMMENTS)

typedef enum
{
	COPY_TARGET_FILE,
	COPY_TARGET_CLIPBOARD
} CopyTarget;

typedef struct
{
	char   *pathname;
	char   *log_pathname;
	bool	ignore_case;
	bool	ignore_lower_case;
	bool	no_mouse;
	bool	less_status_bar;
	bool	no_highlight_search;
	bool	no_highlight_lines;
	bool	force_uniborder;
	bool	no_commandbar;
	bool	no_topbar;
	bool	show_rownum;
	bool	no_cursor;
	bool	vertical_cursor;
	bool	show_scrollbar;
	bool	tabular_cursor;
	bool	force_ascii_art;
	int		theme;
	int		freezed_cols;
	bool	bold_labels;
	bool	bold_cursor;
	bool	tsv_format;
	bool	csv_format;
	char	csv_separator;
	char	csv_header;			/* a - auto, - off, + on */
	char   *nullstr;
	char   *csv_skip_columns_like;
	bool	ignore_short_rows;
	bool	pgcli_fix;			/* hints for using from pgcli */
	bool	double_header;
	int		border_type;
	bool	on_sigint_exit;
	bool	no_sigint_search_reset;
	char   *query;
	int		watch_time;
	char   *host;
	char   *username;
	char   *port;
	bool	force_password_prompt;
	char   *password;
	char   *dbname;
	bool	watch_file;
	bool	quit_on_f3;
	ClipboardFormat clipboard_format;
	CopyTarget copy_target;
	bool	empty_string_is_null;
	bool	xterm_mouse_mode;
	int		clipboard_app;
	bool	no_sleep;
	bool	querystream;
	bool	menu_always;
	bool	last_row_search;
	int		hist_size;
	bool	progressive_load_mode;
	char   *custom_theme_name;
	bool	highlight_odd_rec;
	bool	hide_header_line;
} Options;

extern bool save_config(char *path, Options *opts);
extern bool load_config(char *path, Options *opts);

#endif
