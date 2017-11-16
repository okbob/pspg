
all:

# Include setting from the configure script
-include config.make

all: pspg

pspg: src/pspg.c config.make
	$(CC) src/pspg.c -o pspg $(CFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	$(RM) pspg
	
distclean: clean
	$(RM) -r autom4te.cache
	$(RM) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

install: all
	tools/install.sh bin pspg "$(DESTDIR)$(bindir)"
