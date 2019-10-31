all:

# Include setting from the configure script
-include config.make

ifdef COMPILE_MENU
ST_MENU_OFILES=st_menu.o st_menu_styles.o
endif

PSPG_OFILES=csv.o print.o commands.o unicode.o themes.o pspg.o config.o sort.o menu.o pgclient.o

all: pspg

st_menu_styles.o: src/st_menu_styles.c config.make
	$(CC) -O3 src/st_menu_styles.c -c $(CPPFLAGS) $(CFLAGS)

st_menu.o: src/st_menu.c config.make
	$(CC) -O3 src/st_menu.c -c $(CPPFLAGS) $(CFLAGS)

csv.o: src/pspg.h src/unicode.h src/pretty-csv.c
	$(CC) -O3 -c  src/pretty-csv.c -o csv.o $(CPPFLAGS) $(CFLAGS)

print.o: src/pspg.h src/unicode.h src/print.c
	$(CC) -O3 -c  src/print.c -o print.o $(CPPFLAGS) $(CFLAGS)

commands.o: src/pspg.h src/commands.h src/commands.c
	$(CC) -O3 -c src/commands.c -o commands.o $(CPPFLAGS) $(CFLAGS)

config.o: src/config.h src/config.c
	$(CC) -O3 -c src/config.c -o config.o $(CPPFLAGS) $(CFLAGS)

unicode.o: src/unicode.h src/unicode.c
	$(CC) -O3 -c src/unicode.c -o unicode.o $(CPPFLAGS) $(CFLAGS)

themes.o: src/themes.h src/themes.c
	$(CC) -O3 -c src/themes.c -o themes.o $(CPPFLAGS) $(CFLAGS)

sort.o: src/pspg.h src/sort.c
	$(CC) -O3 -c src/sort.c -o sort.o $(CPPFLAGS) $(CFLAGS)

menu.o: src/pspg.h src/st_menu.h src/commands.h src/menu.c
	$(CC) -O3 -c src/menu.c -o menu.o $(CPPFLAGS) $(CFLAGS)

pgclient.o: src/pspg.h src/pgclient.c
	$(CC) -O3 -c src/pgclient.c -o pgclient.o $(CPPFLAGS) $(CFLAGS) $(PG_CFLAGS) -DPG_VERSION="$(PG_VERSION)"


pspg.o: src/commands.h src/config.h src/unicode.h src/themes.h src/pspg.c
	$(CC) -O3 -c src/pspg.c -o pspg.o $(CPPFLAGS) $(CFLAGS)

pspg:  $(PSPG_OFILES) $(ST_MENU_OFILES) config.make
	$(CC) -O3 $(PSPG_OFILES) $(ST_MENU_OFILES) -o pspg $(LDFLAGS) $(LDLIBS) $(PG_LFLAGS)

clean:
	$(RM) $(ST_MENU_OFILES)
	$(RM) $(PSPG_OFILES)
	$(RM) pspg

distclean: clean
	$(RM) -r autom4te.cache
	$(RM) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

install: all
	tools/install.sh bin pspg "$(DESTDIR)$(bindir)"
