# -*- makefile-gmake -*-

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
mandir = ${datarootdir}/man
docdir = ${datarootdir}/doc/${PACKAGE_TARNAME}
datarootdir = ${prefix}/share
sysconfdir = ${prefix}/etc

CC = gcc
CFLAGS = -g -O2   
CPPFLAGS =  -DHAVE_CONFIG_H
LDFLAGS = 
LDLIBS =  -lncursesw -ltinfo 

config.status: configure
	./config.status --recheck

config.make: config.status
	./config.status $@

config.make: config.make.in
