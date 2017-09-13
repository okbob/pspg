# pspg - Postgres Pager

Everybody who uses `psql` uses `less` pager. It is working well, but there is not any support for tabular data. I found few projects, but no one was well specialized for this purpose. So I decided to write some small specialized pager for usage as `psql` pager.

## Main target
* possibility to freeze first few rows, first few colums
* possibility to use fancy colors - like `mcview` or `FoxPro`

## Screenshots
![Screenshot](screenshots/theme1.gif)

![Screenshot](screenshots/theme3.gif)

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

see http://okbob.blogspot.cz/2017/07/i-hope-so-every-who-uses-psql-uses-less.html

## Keyboard commands

* 0, 1, 2, 3, 4 - freeze first N columns
* KEY_UP, k - navigate backward by one line
* KEY_DOWN, j - navigate forward by one line
* KEY_LEFT, h - scroll to left
* KEY_RIGHT, l - scroll to right
* Ctrl Home, g - go to the start of file
* Ctrl End, G - go to the end of file
* H - go to first line of current window
* M - go to half of current window
* L - go to end of current window
* PPAGE, Ctrl B - backward one window
* NPAGE, Ctrl F, space - forward one window
* HOME, ^ - go to begin of line, first column
* END, $ - go to end of line, last column
* s - save content to file
* / - search for a pattern which will take you to the next occurrence
* ? - search for a pattern which will take you to the previous occurrence
* n - for next match in backward direction
* N - for previous match in forward direction

# Recommended psql configuration

* linestyle unicode
* border 2

# Note - compilation issue

Some linker issues can be fixed by:
<pre>
I changed 
gcc -lncursesw pager.c -o pspg -ggdb 
to 
gcc pager.c -o pspg -ggdb -lncursesw
</pre>

# Note

If you like it, send a postcard to address

    Pavel Stehule
    Skalice 12
    256 01 Benesov u Prahy
    Czech Republic


I invite any questions, comments, bug reports, patches on mail address pavel.stehule@gmail.com

