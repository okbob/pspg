# pspg - Postgres Pager

Everybody who uses `psql` uses `less` pager. It is working well, but there is not any special
support for tabular data. I found few projects, but no one was completed for this purpose.
I decided to write some small specialized pager for usage as `psql` pager.

This pager can be used from `mysql` and `pgcli` clients too.

## Main target
* possibility to freeze first few rows, first few columns
* possibility to use fancy colors - like `mcview` or `FoxPro`


## Screenshots
![Screenshot](screenshots/theme1.gif)

![Screenshot](screenshots/theme3.gif)


## Options

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
* `--no-mouse`  without own mouse handling (cannot be changed in app)
* `--no-sound`  without sound effect
* `-F`, `--quit-if-one-screen`  quit if content is one screen
* `-V`, `--version`  show version

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


see http://okbob.blogspot.cz/2017/07/i-hope-so-every-who-uses-psql-uses-less.html


## Keyboard commands

* <kbd>0</kbd>, <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd>, <kbd>4</kbd> - freeze first N columns
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
* <kbd>Alt</kbd>+<kbd>m</kbd> - switch (on, off) own mouse handler
* Mouse button wheel - scroll vertical
* <kbd>Alt</kbd>+ Mouse button wheel - scroll horizontal
* <kbd>q</kbd>, <kbd>F10</kbd>, <kbd>Esc</kbd> <kbd>Esc</kbd>, <kbd>Esc</kbd> <kbd>0</kbd> - quit
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
/home/user/Src/pspg-0.6/src/pspg.c:2403: undefined reference to `set_escdelay'
</pre>
In this case comment line with function set_escdelay

# Note - Installation

The pspg is available from community repository https://yum.postgresql.org/packages.php

## Gentoo

    # emerge -av dev-db/pspg

## FreeBSD

    # pkg install pspg
    
## macOS (MacPorts)

    # port install pspg

# Note

If you like it, send a postcard from your home country to my address, please:

    Pavel Stehule
    Skalice 12
    256 01 Benesov u Prahy
    Czech Republic


I invite any questions, comments, bug reports, patches on mail address pavel.stehule@gmail.com
