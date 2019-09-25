all:

# Include setting from the configure script
-include config.make

ifdef COMPILE_MENU
ST_MENU_OFILES=st_menu.o st_menu_styles.o
endif

all: pspg

st_menu_styles.o: src/st_menu_styles.c config.make
	$(CC) -O3 src/st_menu_styles.c -c $(CPPFLAGS) $(CFLAGS)

st_menu.o: src/st_menu.c config.make
	$(CC) -O3 src/st_menu.c -c $(CPPFLAGS) $(CFLAGS)

pspg: src/pspg.h src/unicode.h src/pspg.c src/unicode.c src/themes.c src/print.c src/config.c src/commands.h src/menu.c src/commands.c src/sort.c src/pretty-csv.c $(ST_MENU_OFILES) config.make
	$(CC) -O3 src/pspg.c src/print.c src/unicode.c src/themes.c src/config.c src/menu.c src/commands.c src/sort.c src/pretty-csv.c $(ST_MENU_OFILES) -o pspg $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	$(RM) $(ST_MENU_OFILES)
	$(RM) pspg

distclean: clean
	$(RM) -r autom4te.cache
	$(RM) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

install: all
	tools/install.sh bin pspg "$(DESTDIR)$(bindir)"
