Compilation
-----------
I tested build and execution against pdcursesmod 4.3.5, and it works. The mouse
worked partially (wheel button  doesn't work), but all other things worked well.
For correct UTF8, I had to force UTF8 mode.


    PANEL_LIBS=-lpdcurses CURSES_LIBS=-lpdcurses CURSES_CFLAGS="-I/usr/local/include/pdcurses -DPDC_WIDE=Y -DPDC_FORCE_UTF8=Y -L/usr/local/lib" ./configure -libdir=/usr/local/lib
    make

