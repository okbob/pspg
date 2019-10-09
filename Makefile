all:

# Include setting from the configure script
-include config.make

ifdef COMPILE_MENU
ST_MENU_OFILES=st_menu.o st_menu_styles.o
endif

PSPG_OFILES=csv.o print.o commands.o unicode.o themes.o pspg.o config.o sort.o menu.o

all: pspg

st_menu_styles.o: src/st_menu_styles.c config.make
	$(CC) -O3 src/st_menu_styles.c -c $(CPPFLAGS) $(CFLAGS)

st_menu.o: src/st_menu.c config.make
	$(CC) -O3 src/st_menu.c -c $(CPPFLAGS) $(CFLAGS)

csv.o: src/pspg.h src/unicode.h src/pretty-csv.c
	$(CC) -O3 -c  src/pretty-csv.c -o csv.o

print.o: src/pspg.h src/unicode.h src/print.c
	$(CC) -O3 -c  src/print.c -o print.o

commands.o: src/pspg.h src/commands.h src/commands.c
	$(CC) -O3 -c src/commands.c -o commands.o

config.o: src/config.h src/config.c
	$(CC) -O3 -c src/config.c -o config.o

unicode.o: src/unicode.h src/unicode.c
	$(CC) -O3 -c src/unicode.c -o unicode.o

themes.o: src/themes.h src/themes.o
	$(CC) -O3 -c src/themes.c -o themes.o $(CPPFLAGS) $(CFLAGS)

sort.o: src/pspg.h src/sort.c
	$(CC) -O3 -c src/sort.c -o sort.o

menu.o: src/pspg.h src/st_menu.h src/commands.h src/menu.c
	$(CC) -O3 -c src/menu.c -o menu.o $(CPPFLAGS) $(CFLAGS)


pspg.o: src/commands.h src/config.h src/unicode.h src/themes.h src/pspg.c
	$(CC) -O3 -c src/pspg.c -o pspg.o $(CPPFLAGS) $(CFLAGS)


pspg:  $(PSPG_OFILES) $(ST_MENU_OFILES) config.make
	$(CC) -O3 $(PSPG_OFILES) $(ST_MENU_OFILES) -o pspg $(LDFLAGS) $(LDLIBS)

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
