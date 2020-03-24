
#if defined HAVE_NCURSESW_PANEL_H
#include <ncursesw/panel.h>
#elif defined HAVE_NCURSES_PANEL_H
#include <ncurses/panel.h>
#elif defined HAVE_PANEL_H
#include <panel.h>
#else
/* fallback */
#include <ncurses/panel.h>
#endif

