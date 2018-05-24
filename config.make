# -*- makefile-gmake -*-

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
mandir = ${datarootdir}/man
docdir = ${datarootdir}/doc/${PACKAGE_TARNAME}
datarootdir = ${prefix}/share
sysconfdir = ${prefix}/etc

COMPILE_MENU = 1

CC = gcc
CFLAGS = -g -O2 -DCOMPILE_MENU   -D_GNU_SOURCE -D_DEFAULT_SOURCE  -DPACKAGE_NAME=\"pspg\" -DPACKAGE_TARNAME=\"pspg\" -DPACKAGE_VERSION=\"0\" -DPACKAGE_STRING=\"pspg\ 0\" -DPACKAGE_BUGREPORT=\"pavel.stehule@gmail.com\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_NCURSESW=1 -DHAVE_CURSES=1 -DHAVE_CURSES_ENHANCED=1 -DHAVE_CURSES_COLOR=1 -DHAVE_CURSES_OBSOLETE=1 -DHAVE_NCURSESW_CURSES_H=1 -DHAVE_CURSES_ENHANCED=1 -DHAVE_CURSES_COLOR=1 -DHAVE_CURSES_OBSOLETE=1 -DHAVE_NCURSES_H=1 -DHAVE_PANEL=1 -DHAVE_NCURSESW_PANEL_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_SYS_STAT_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DHAVE_LIBREADLINE=1 -DHAVE_READLINE_READLINE_H=1 -DHAVE_READLINE_HISTORY=1 -DHAVE_READLINE_HISTORY_H=1
LDFLAGS = 
LDLIBS =  -lreadline -lpanelw -lncursesw -ltinfo 

config.status: configure
	./config.status --recheck

config.make: config.status
	./config.status $@

config.make: config.make.in
