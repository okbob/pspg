/*-------------------------------------------------------------------------
 *
 * theme_loader.c
 *	  a routines for loading theme (style) definition
 *
 * Portions Copyright (c) 2017-2023 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/theme_loader.c
 *
 *-------------------------------------------------------------------------
 */
#include "pspg.h"
#include "themes.h"

#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

typedef enum
{
	TOKEN_CHAR,
	TOKEN_WORD,
	TOKEN_NUMBER
} TokenType;

typedef struct
{
	int		value;
	int		size;
	TokenType typ;
	char   *str;
} Token;

typedef struct
{
	char	*start;
	char	*current;
	bool	is_error;

	Token	saved_token;
	bool	saved_token_is_valid;
} Tokenizer;

static void
initTokenizer(Tokenizer *tokenizer, char *str)
{
	tokenizer->start = str;
	tokenizer->current = str;
	tokenizer->is_error = false;
	tokenizer->saved_token_is_valid = false;
}

static bool
iskeychar(int c)
{
	return (c >= 'a' && c <= 'z') ||
		   (c >= 'A' && c <= 'Z') ||
		   c == '_';
}



#define IS_KEYWORD(t, k)		(((t)->size == strlen(k)) && strncasecmp((t)->str, k, (t)->size) == 0)

static Token *
ThemeLoaderGetToken(Tokenizer *tokenizer, Token *token)
{
	char		c;

	if (tokenizer->saved_token_is_valid)
	{
		memcpy(token, &tokenizer->saved_token, sizeof(Token));
		tokenizer->saved_token_is_valid = false;
		return token;
	}

	while (isspace(*tokenizer->current))
		tokenizer->current++;

	if (*tokenizer->current == '\0')
		return NULL;

	token->str = tokenizer->current;
	c = *tokenizer->current++;

	if (c == '#')
	{
		token->str = tokenizer->current;
		while (isxdigit(*tokenizer->current))
			tokenizer->current++;

		if ((tokenizer->current - token->str) != 6)
		{
			log_row("theme loader: syntax error (broken format of rgb color)");
			tokenizer->is_error = true;
			return NULL;
		}

		token->typ = TOKEN_NUMBER;
		token->size = 6;
		token->value = (int) strtol(token->str, NULL, 16);
	}
	else if (isdigit(c))
	{
		while (isdigit(*tokenizer->current))
			tokenizer->current++;

		token->typ = TOKEN_NUMBER;
		token->size = 6;
		token->value = (int) strtol(token->str, NULL, 10);
	}
	else if (iskeychar(c))
	{
		while (iskeychar(*tokenizer->current))
			tokenizer->current++;

		token->typ = TOKEN_WORD;
		token->size = tokenizer->current - token->str;
	}
	else
	{
		token->typ = TOKEN_CHAR;
		token->value = c;
	}

	return token;
}

static void
ThemeLoaderPushBackToken(Tokenizer *tokenizer, Token *token)
{
	tokenizer->saved_token_is_valid = true;
	memcpy(&tokenizer->saved_token, token, sizeof(Token));
}

typedef enum
{
	TL_THEME_ELEMENT,
	TL_MENU_INDEX,
	TL_TEMPLATE_INDEX
} ThemeLoaderKeyType;

typedef struct
{
	ThemeLoaderKeyType key_type;
	PspgThemeElements te_type;
} ThemeLoaderKey;

static attr_t
GetAttr(Tokenizer *tokenizer)
{
	attr_t		result = 0;

	while (1)
	{
		Token		token, *_token;

		_token = ThemeLoaderGetToken(tokenizer, &token);
		if (!_token)
			return result;

		if (_token->typ == TOKEN_WORD)
		{
			if (IS_KEYWORD(_token, "bold"))
				result |= A_BOLD;
			else if (IS_KEYWORD(_token, "italic"))
				result |= A_ITALIC;
			else if (IS_KEYWORD(_token, "underline"))
				result |= A_UNDERLINE;
			else if (IS_KEYWORD(_token, "reverse"))
				result |= A_REVERSE;
			else if (IS_KEYWORD(_token, "standout"))
				result |= A_STANDOUT;
			else if (IS_KEYWORD(_token, "dim"))
				result |= A_DIM;
			else
			{
				log_row("theme loader: unknown attribute \"%.*s\"", _token->size, _token->str);
				tokenizer->is_error = true;
				return 0;
			}
		}
		else
		{
			log_row("theme loader: unexpected token (expected style attribute)");
			tokenizer->is_error = true;
			return 0;
		}

		/* ignore comma */
		_token = ThemeLoaderGetToken(tokenizer, &token);
		if (_token && !(_token->typ == TOKEN_CHAR && _token->value == ','))
			ThemeLoaderPushBackToken(tokenizer, &token);
	}

	return result;
}

static ThemeLoaderKey *
GetKey(Tokenizer *tokenizer)
{
	static ThemeLoaderKey tlk;

	if (!tokenizer->is_error)
	{
		Token		token, *_token;

		_token = ThemeLoaderGetToken(tokenizer, &token);
		if (!_token)
			return NULL;

		if (_token->typ == TOKEN_WORD)
		{
			if (IS_KEYWORD(_token, "template"))
				tlk.key_type = TL_TEMPLATE_INDEX;
			else if (IS_KEYWORD(_token, "template_menu"))
				tlk.key_type = TL_MENU_INDEX;
			else
			{
				tlk.key_type = TL_THEME_ELEMENT;
				tlk.te_type = PspgTheme_none;

				if (IS_KEYWORD(_token, "background"))
					tlk.te_type = PspgTheme_background;
				else if (IS_KEYWORD(_token, "data"))
					tlk.te_type = PspgTheme_data;
				else if (IS_KEYWORD(_token, "border"))
					tlk.te_type = PspgTheme_border;
				else if (IS_KEYWORD(_token, "label"))
					tlk.te_type = PspgTheme_label;
				else if (IS_KEYWORD(_token, "row_number"))
					tlk.te_type = PspgTheme_rownum;
				else if (IS_KEYWORD(_token, "record_number"))
					tlk.te_type = PspgTheme_recnum;
				else if (IS_KEYWORD(_token, "selected_area"))
					tlk.te_type = PspgTheme_selection;
				else if (IS_KEYWORD(_token, "footer"))
					tlk.te_type = PspgTheme_footer;
				else if (IS_KEYWORD(_token, "cursor_data"))
					tlk.te_type = PspgTheme_cursor_data;
				else if (IS_KEYWORD(_token, "cursor_border"))
					tlk.te_type = PspgTheme_cursor_border;
				else if (IS_KEYWORD(_token, "cursor_label"))
					tlk.te_type = PspgTheme_cursor_label;
				else if (IS_KEYWORD(_token, "cursor_row_number"))
					tlk.te_type = PspgTheme_cursor_rownum;
				else if (IS_KEYWORD(_token, "cursor_record_number"))
					tlk.te_type = PspgTheme_cursor_recnum;
				else if (IS_KEYWORD(_token, "cursor_selected_area"))
					tlk.te_type = PspgTheme_cursor_selection;
				else if (IS_KEYWORD(_token, "cursor_footer"))
					tlk.te_type = PspgTheme_cursor_footer;
				else if (IS_KEYWORD(_token, "scrollbar_arrows"))
					tlk.te_type = PspgTheme_scrollbar_arrows;
				else if (IS_KEYWORD(_token, "scrollbar_background"))
					tlk.te_type = PspgTheme_scrollbar_background;
				else if (IS_KEYWORD(_token, "scrollbar_slider"))
					tlk.te_type = PspgTheme_scrollbar_slider;
				else if (IS_KEYWORD(_token, "scrollbar_active_slider"))
					tlk.te_type = PspgTheme_scrollbar_active_slider;
				else if (IS_KEYWORD(_token, "title"))
					tlk.te_type = PspgTheme_title;
				else if (IS_KEYWORD(_token, "status_bar"))
					tlk.te_type = PspgTheme_status_bar;
				else if (IS_KEYWORD(_token, "prompt_bar"))
					tlk.te_type = PspgTheme_prompt_bar;
				else if (IS_KEYWORD(_token, "info_bar"))
					tlk.te_type = PspgTheme_info_bar;
				else if (IS_KEYWORD(_token, "error_bar"))
					tlk.te_type = PspgTheme_error_bar;
				else if (IS_KEYWORD(_token, "input_bar"))
					tlk.te_type = PspgTheme_input_bar;
				else if (IS_KEYWORD(_token, "bookmark"))
					tlk.te_type = PspgTheme_bookmark;
				else if (IS_KEYWORD(_token, "bookmark_border"))
					tlk.te_type = PspgTheme_bookmark_border;
				else if (IS_KEYWORD(_token, "cursor_bookmark"))
					tlk.te_type = PspgTheme_cursor_bookmark;
				else if (IS_KEYWORD(_token, "cross_cursor"))
					tlk.te_type = PspgTheme_cross_cursor;
				else if (IS_KEYWORD(_token, "cross_cursor_border"))
					tlk.te_type = PspgTheme_cross_cursor_border;
				else if (IS_KEYWORD(_token, "matched_pattern"))
					tlk.te_type = PspgTheme_pattern;
				else if (IS_KEYWORD(_token, "mathed_pattern_nohl"))
					tlk.te_type = PspgTheme_pattern_nohl;
				else if (IS_KEYWORD(_token, "matched_line"))
					tlk.te_type = PspgTheme_pattern_line;
				else if (IS_KEYWORD(_token, "matched_line_border"))
					tlk.te_type = PspgTheme_pattern_line_border;
				else if (IS_KEYWORD(_token, "matched_pattern_cursor"))
					tlk.te_type = PspgTheme_pattern_cursor;
				else if (IS_KEYWORD(_token, "matched_line_vertical_cursor"))
					tlk.te_type = PspgTheme_pattern_line_vertical_cursor;
				else if (IS_KEYWORD(_token, "matched_line_vertical_cursor_border"))
					tlk.te_type = PspgTheme_pattern_line_vertical_cursor_border;
				else if (IS_KEYWORD(_token, "error"))
					tlk.te_type = PspgTheme_error;
				else
				{
					log_row("theme loader: unknown key \"%.*s\"",
							  _token->size, _token->str);
					tokenizer->is_error = true;
					return NULL;
				}
			}
		}
		else
		{
			log_row("theme loader: unexpected token (expected key name)");
			tokenizer->is_error = true;
			return NULL;
		}
	}
	else
		return NULL;

	return &tlk;
}

static PspgColor *
GetColorDef(Tokenizer *tokenizer)
{
	Token token, *_token;

	if (!tokenizer->is_error)
	{
		_token = ThemeLoaderGetToken(tokenizer, &token);
		if (!_token)
			return NULL;
		if (_token->typ == TOKEN_CHAR)
		{
			log_row("theme loader: unexpected token \"%c\"", _token->value);
			tokenizer->is_error = true;
			return NULL;
		}
		else if (_token->typ == TOKEN_WORD)
		{
			if (IS_KEYWORD(_token, "Black"))
				return (PspgColor *) &PspgBlack;
			else if (IS_KEYWORD(_token, "Red"))
				return (PspgColor *) &PspgRed;
			else if (IS_KEYWORD(_token, "Green"))
				return (PspgColor *) &PspgGreen;
			else if (IS_KEYWORD(_token, "Brown"))
				return (PspgColor *) &PspgBrown;
			else if (IS_KEYWORD(_token, "Blue"))
				return (PspgColor *) &PspgBlue;
			else if (IS_KEYWORD(_token, "Magenta"))
				return (PspgColor *) &PspgMagenta;
			else if (IS_KEYWORD(_token, "Cyan"))
				return (PspgColor *) &PspgCyan;
			else if (IS_KEYWORD(_token, "LightGray"))
				return (PspgColor *) &PspgLightGray;
			else if (IS_KEYWORD(_token, "Gray"))
				return (PspgColor *) &PspgGray;
			else if (IS_KEYWORD(_token, "BrightRed"))
				return (PspgColor *) &PspgBrightRed;
			else if (IS_KEYWORD(_token, "BrightGreen"))
				return (PspgColor *) &PspgBrightGreen;
			else if (IS_KEYWORD(_token, "Yellow"))
				return (PspgColor *) &PspgYellow;
			else if (IS_KEYWORD(_token, "BrightBlue"))
				return (PspgColor *) &PspgBrightBlue;
			else if (IS_KEYWORD(_token, "BrightMagenta"))
				return (PspgColor *) &PspgBrightMagenta;
			else if (IS_KEYWORD(_token, "BrightCyan"))
				return (PspgColor *) &PspgBrightCyan;
			else if (IS_KEYWORD(_token, "White"))
				return (PspgColor *) &PspgWhite;
			else if (IS_KEYWORD(_token, "Default"))
				return (PspgColor *) &PspgDefault;
			else
			{
				log_row("theme loader: unknown color \"%.*s\"", _token->size, _token->str);
				tokenizer->is_error = true;
				return NULL;
			}
		}
		else
		{
			static PspgColor result;

			if (COLORS == 8)
			{
				log_row("theme loader: cannot to display RGB color");
				tokenizer->is_error = true;
				return NULL;
			}

			result.cp = PSPG_COLOR_RGB;
			result.rgb = _token->value;

			return &result;
		}
	}

	return NULL;
}


/*
 * Theme loader try to work in tolerant mode, broken lines
 * are ignored. An information about broken lines are printed to log.
 *
 * This function returns true, when theme was loaded. An warning can
 * be raised by setting output argument is_warning to true.
 *
 * tle2 is used for odd records.
 */
bool
theme_loader(FILE *theme,
			 PspgThemeLoaderElement *tle,
			 PspgThemeLoaderElement *tle2,
			 int *template,
			 int *menu,
			 bool *is_warning)
{
	char	   *line = NULL;
	ssize_t		read;
	size_t		len;
	int			lineno = 0;

	if (THEMEDEF_SIZE <= PspgTheme_error)
		leave("theme loader: internal error (the size of theme loader table is too small)");

	*is_warning = false;
	memset(tle, 0, THEMEDEF_SIZE * sizeof(PspgThemeLoaderElement));
	memset(tle2, 0, THEMEDEF_SIZE * sizeof(PspgThemeLoaderElement));

	*template = 6;
	*menu = 2;

	errno = 0;

	while ((read = getline(&line, &len, theme)) != -1)
	{
		Tokenizer tokenizer;
		Token token, *_token;
		ThemeLoaderKey *key;
		PspgThemeLoaderElement *_tle = tle;

		lineno += 1;

		/* strip new line on the end */
		if (line[read - 1] == '\n')
			line[read - 1] = '\0';

		initTokenizer(&tokenizer, line);
		key = GetKey(&tokenizer);

		if (key)
		{
			int		int_value = 0;
			PspgThemeElement te;

			te.attr = 0;
			te.fg = PspgWhite;
			te.bg = PspgBlack;

			_token = ThemeLoaderGetToken(&tokenizer, &token);
			if (_token)
			{
				if (_token->typ == TOKEN_CHAR && _token->value == '*')
					_tle = tle2;
				else
					ThemeLoaderPushBackToken(&tokenizer, &token);
			}

			_token = ThemeLoaderGetToken(&tokenizer, &token);
			if (!_token)
			{
				log_row("theme loader: syntax error (missing \"=\")");
				goto err;
			}
			else if (_token->typ != TOKEN_CHAR || _token->value != '=')
			{
				log_row("theme loader: unexepected token (expected \"=\")");
				goto err;
			}

			if (key->key_type == TL_TEMPLATE_INDEX || key->key_type == TL_MENU_INDEX)
			{
				_token = ThemeLoaderGetToken(&tokenizer, &token);
				if (!_token)
				{
					log_row("theme loader: missing number");
					goto err;
				}
				if (_token->typ != TOKEN_NUMBER)
				{
					log_row("theme loader: unexpected token (expected number)");
					goto err;
				}

				if (key->key_type == TL_TEMPLATE_INDEX &&
					(_token->value < 0 || _token->value > MAX_STYLE))
				{
					log_row("theme loader: template style number is out of limit (%d)", MAX_STYLE);
					goto err;
				}
				else if (key->key_type == TL_MENU_INDEX &&
					(_token->value < 0 || _token->value > ST_MENU_LAST_STYLE))
				{
					log_row("theme loader: menu template style number is out of limit (%d)", ST_MENU_LAST_STYLE);
					goto err;
				}

				int_value = _token->value;
			}
			else
			{
				static PspgColor *color;

				color = GetColorDef(&tokenizer);
				if (tokenizer.is_error)
					goto err;

				if (!color)
				{
					log_row("theme loader: missing foreground color definition");
					goto err;
				}

				te.fg = *color;
				_token = ThemeLoaderGetToken(&tokenizer, &token);
				if (!_token || (_token->typ != TOKEN_CHAR || _token->value != ','))
				{
					log_row("theme loader: syntax error (missing \",\")");
					goto err;
				}

				color = GetColorDef(&tokenizer);
				if (tokenizer.is_error)
					goto err;

				if (!color)
				{
					log_row("theme loader: missing background color definition");
					goto err;
				}

				te.bg = *color;

				_token = ThemeLoaderGetToken(&tokenizer, &token);
				if (_token)
				{
					if (_token->typ != TOKEN_CHAR || _token->value != ',')
					{
						log_row("theme loader: syntax error (missing \",\")");
						goto err;
					}

					te.attr = GetAttr(&tokenizer);
				}
			}

			if (!tokenizer.is_error)
			{
				_token = ThemeLoaderGetToken(&tokenizer, &token);
				if (_token)
				{
					log_row("theme loader: unexpected token before end of line");
					goto err;
				}

				if (key->key_type == TL_MENU_INDEX)
					*menu = int_value;
				else if (key->key_type == TL_TEMPLATE_INDEX)
					*template = int_value;
				else
				{
					_tle[key->te_type].te = te;
					_tle[key->te_type].used = true;
				}
			}
			else
				goto err;
		}
		else
		{
			if (tokenizer.is_error)
				goto err;
		}

		continue;

err:

		log_row("theme loader: skips line %d due error", lineno);
		log_row("%d: \"%s\"", lineno, line);
		*is_warning = true;
	}

	free(line);

	if (errno != 0)
	{
		format_error("cannot to read from theme description file (%s)", strerror(errno));
		return false;
	}

	return true;
}

FILE *
open_theme_desc(char *name)
{
	const char *PSPG_CONF;
	char *transf_path;
	char *dir;
	char buffer[MAXPATHLEN];
	FILE	   *result;

	PSPG_CONF = getenv("PSPG_CONF");
	if (!PSPG_CONF)
		PSPG_CONF = "~/.pspgconf";

	transf_path = tilde(NULL, PSPG_CONF);
	dir = dirname(transf_path);

	snprintf(buffer, MAXPATHLEN - 1, "%s/.pspg_theme_%s", dir, name);
	log_row("opening custom theme style \"%s\"", buffer);

	errno = 0;

	result = fopen(buffer, "r");
	if (!result)
		format_error("cannot to open theme description file \"%s\" (%s)", buffer, strerror(errno));

	return result;
}
