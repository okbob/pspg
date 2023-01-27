Compilation
-----------
I tested build and execution against pdcursesmod 4.3.5, and it works. In this moment
(Thu Dec 22 05:22:51 CET 2022), pdcurses should be patched (https://github.com/Bill-Gray/PDCursesMod/issues/256),
but with patch (or after fixing this issue), pspg can be compiled against pdcurses
and works well.

Compilation of pdcurses on Linux (pdcurses for VT)
--------------------------------------------------

    cd ~/src/PDCursesMod/vt
    make clean
    make DLL=Y WIDE=Y UTF8=Y DEBUG=Y
    sudo make DLL=Y WIDE=Y UTF8=Y DEBUG=Y install
    sudo cp libpdcurses.so /usr/lib
    sudo ldconfig /usr/lib

Maybe can be necessary to copy header files to `/usr/local/include/pdcurses`.

Note - the directory should be fresh after git sync without running cmake.

Compilation of pspg on pdcurses
-------------------------------

    PANEL_LIBS=-lpdcurses CURSES_LIBS=-lpdcurses\
      CURSES_CFLAGS="-I/usr/local/include/pdcurses -DPDC_WIDE=Y -DPDC_FORCE_UTF8=Y -L/usr/lib"\
      ./configure -libdir=/usr/lib
    make

