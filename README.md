[![Build Status](https://travis-ci.org/okbob/pspg.svg?branch=master)](https://travis-ci.org/okbob/pspg)

# pspg - Postgres Pager
Everybody who uses `psql` uses `less` pager. It is working well, but there is not any special
support for tabular data. I found few projects, but no one was completed for this purpose.
I decided to write some small specialized pager for usage as `psql` pager.

This pager can be used from `mysql` and `pgcli` clients too.

## Main target
* possibility to freeze first few rows, first few columns
* possibility to use fancy colors - like `mcview` or `FoxPro`


## Screenshots
![Screenshot](screenshots/pspg-modern.png)

![Screenshot](screenshots/theme1.gif)

![Screenshot](screenshots/theme3.gif)


## Options
* `-a`       menu will use ascii borders
* `-b`       black/white theme
* `-X`       doesn't clean screen on the end
* `-s N`     use theme (default theme is mc theme)
* `-c N`     freeze first N columns
* `-f file`  open file (default stdin)
* `--force-uniborder`  replace ascii border by unicode borders
* `-g --hilite-search`  don't highlight lines for searches
* `-G --HILITE-SEARCH`  don't highlight lines for searches ever
* `--help`   show this help
* `-i --ignore-case`  ignore case in searches that do not contain uppercase
* `-I --IGNORE-CASE`  ignore case in all searches
* `--less-status-bar`  status bar like less pager
* `--line-numbers`  show line number column
* `--no-mouse`  without own mouse handling (cannot be changed in app)
* `--no-sound`  without sound effect
* `-F`, `--quit-if-one-screen`  quit if content is one screen
* `-V`, `--version`  show version
* `--no-cursor`  the line cursor will be hidden
* `--no-commandbar`  the bottom bar will be hidden
* `--no-topbar`  the top bar will be hidden
* `--no-bars`  both bars will be hidden
* `--tabular-cursor`  cursor is displayed only for table
* `--only-for-tables`  use std pager when content is not a table
* `--bold-labels`  bold font for row, column labels
* `--bold-cursor`  bold font for cursor


## Themes
0. black & white
1. Midnight Commander like
2. FoxPro like
3. Pdmenu like
4. White theme
5. Mutt like
6. PCFand like
7. Green theme
8. Blue theme
9. Word Perfect like
10. Low contrast blue theme
11. Dark cyan/black mode
12. Paradox like
13. dBase IV retro style
14. dBase IV retro style (Magenta labels)
15. Red white theme
16. Simple theme
17. Solarized dark theme 
18. Solarized light theme
19. Gruvbox light theme
20. Tao Light theme

see http://okbob.blogspot.cz/2017/07/i-hope-so-every-who-uses-psql-uses-less.html


## Keyboard commands
* <kbd>0</kbd>, <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd>, .., <kbd>9</kbd> - freeze first N columns
* <kbd>KEY_UP</kbd>, <kbd>k</kbd> - navigate backward by one line
* <kbd>KEY_DOWN</kbd>, <kbd>j</kbd> - navigate forward by one line
* <kbd>KEY_LEFT</kbd>, <kbd>h</kbd> - scroll to left
* <kbd>KEY_RIGHT</kbd>, <kbd>l</kbd> - scroll to right
* <kbd>Ctrl</kbd>+<kbd>Home</kbd>, <kbd>g</kbd> - go to the start of file
* <kbd>Ctrl</kbd>+<kbd>End</kbd>, <kbd>G</kbd> - go to the end of file
* <kbd>H</kbd> - go to first line of current window
* <kbd>M</kbd> - go to half of current window
* <kbd>L</kbd> - go to end of current window
* <kbd>PPAGE</kbd>, <kbd>Ctrl</kbd>+<kbd>b</kbd> - backward one window
* <kbd>NPAGE</kbd>, <kbd>Ctrl</kbd>+<kbd>f</kbd>, <kbd>space</kbd> - forward one window
* <kbd>HOME</kbd>, <kbd>^</kbd> - go to begin of line, first column
* <kbd>END</kbd>, <kbd>$</kbd> - go to end of line, last column
* <kbd>Ctrl</kbd>+<kbd>e</kbd> - scroll a window down
* <kbd>Ctrl</kbd>+<kbd>y</kbd> - scroll a window up
* <kbd>Ctrl</kbd>+<kbd>d</kbd> - forward a half window
* <kbd>Ctrl</kbd>+<kbd>u</kbd> - backward a half window
* <kbd>s</kbd> - save content to file
* <kbd>/</kbd> - search for a pattern which will take you to the next occurrence
* <kbd>?</kbd> - search for a pattern which will take you to the previous occurrence
* <kbd>n</kbd> - for next match
* <kbd>N</kbd> - for next match in reverse direction
* <kbd>Alt</kbd>+<kbd>c</kbd> - switch (on, off) drawing line cursor
* <kbd>Alt</kbd>+<kbd>m</kbd> - switch (on, off) own mouse handler
* <kbd>Alt</kbd>+<kbd>n</kbd> - switch (on, off) drawing line numbers
* Mouse button wheel - scroll vertical
* <kbd>Alt</kbd>+ Mouse button wheel - scroll horizontal
* <kbd>F9</kbd> - show menu
* <kbd>q</kbd>, <kbd>F10</kbd>, <kbd>Esc</kbd> <kbd>Esc</kbd>, <kbd>Esc</kbd> <kbd>0</kbd> - quit
* <kbd>Alt</kbd>+<kbd>q</kbd> - quit wit raw (unformatted) output
* <kbd>Alt</kbd>+<kbd>k</kbd> - switch bookmark
* <kbd>Alt</kbd>+<kbd>j</kbd> - go to next bookmark
* <kbd>Alt</kbd>+<kbd>i</kbd> - go to previous bookmark

# Recommended psql configuration
<pre>
\pset linestyle unicode
\pset border 2
</pre>

some possible configuration:
<pre>
-- Switch pagers with :x and :xx commands
\set x '\\setenv PAGER less'
\set xx '\\setenv PAGER \'pspg -bX --no-mouse\''
:xx
</pre>

# MySQL usage
<pre>
MariaDB [sakila]> pager pspg -s 14 -X --force-uniborder --quit-if-one-screen
PAGER set to 'pspg -s 14 -X --force-uniborder --quit-if-one-screen'
MariaDB [sakila]> select now();
MariaDB [sakila]> select * from nicer_but_slower_film_list limit 100;
</pre>

`LC_CTYPE` should be correct.

# Note - compilation issue
Some linker issues can be fixed by:
<pre>
I changed 
gcc -lncursesw pager.c -o pspg -ggdb
to
gcc pager.c -o pspg -ggdb -lncursesw
</pre>

On some old systems a compilation fails with error
<pre>
/home/user/Src/pspg-0.6/src/pspg.c:2403: undefined reference to `set_escdelay`
</pre>
In this case comment line with function set_escdelay

# Note - Installation
When you compile code from source, run ./configure first. Sometimes ./autogen.sh first

## Fedora (28 and later)

    # dnf install pspg
    
## RPM (CentOS/openSUSE/…)
The pspg is available from community repository https://yum.postgresql.org/packages.php

## Alpine Linux

    # apk add pspg

## Gentoo

    # emerge -av dev-db/pspg

## Arch Linux

The Arch User Repository contains two versions:

* [pspg](https://aur.archlinux.org/packages/pspg/) is a fixed release.
* [pspg-git](https://aur.archlinux.org/packages/pspg-git/) tracks the `master` branch.

Use the AUR helper of your choice or git and `makepkg` to install pspg.

## FreeBSD

    # pkg install pspg
    
## Linuxbrew

    # brew install pspg

## macOS

### Using Homebrew

    # brew install pspg

### Using MacPorts

    # port install pspg

## Solaris

There are few issues requires manual code changes for successful compilation - we successfully
tested `pspg`, but although `pspg` was linked with ncursesw libraries, the utf8 encoding support
didn't work fully correctly - probably due some issues in `libc` library. There are problems with
chars encoded to 3bytes - unicode borders, .. Two bytes unicode chars should be displayed well.

You can use `pspg` with usual accented chars, but unicode bordes should not be used. Replacement
ascii borders by special borders chars (by ncurses technology) works well - looks on `Options|Force unicode borders`
option.

* Solaris `make` doesn't support conditional statements - should be removed So, remove unsupported
  functionality from `Makefile` (`ifdef`,`endif`), replace `-include` by `include` first.

* After running `configure` remove link on `termcap` library from `config.make`. It is garabage
  produced by `readline` automake script. Combination with `ncurses` libraries makes some
  linking issues.

### builtin libraries

    export CURSES_CFLAGS="-I/usr/include/ncurses/"
    export PANEL_LIBS="-lpanelw"
    ./configure

### OpenCSW development

    export CFLAGS="-m64 -I/opt/csw/include"
    export LDFLAGS="-L/opt/csw/lib/64 -R/opt/csw/lib/64"
    export PKG_CONFIG_PATH="/opt/csw/lib/64/pkgconfig"
    ./configure

# Possible ToDo

* Store data in some column format (now data are stored like array of rows). With this change can
  be possible to operate over columns - hide columns, change width, cyclic iteration over columns,
  change order of columns, mark columns and export only selected columns (selected rows).

# st_menu

This project uses st_menu library - implementation of CUA menubar and pulldown menu for ncurses
https://github.com/okbob/ncurses-st-menu

# Note

If you like it, send a postcard from your home country to my address, please:

    Pavel Stehule
    Skalice 12
    256 01 Benesov u Prahy
    Czech Republic


I invite any questions, comments, bug reports, patches on mail address pavel.stehule@gmail.com
