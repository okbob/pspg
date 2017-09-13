
all:

# Include setting from the configure script
-include config.make

all: pspg

pspg: src/pspg.c
	$(CC) src/pspg.c -o pspg $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	rm ./pspg
	
distclean: clean
	$(RM) -r autom4te.cache
	$(RM) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

install: all
	tools/install.sh bin pspg "$(DESTDIR)$(bindir)"
