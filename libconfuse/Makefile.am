noinst_LTLIBRARIES = libconfuse.la

libconfuse_la_SOURCES = confuse.c lexer.l $(top_srcdir)/common/confuse.h
libconfuse_la_LIBADD = $(LTLIBINTL)

#include_HEADERS = confuse.h
AM_CPPFLAGS = -I$(top_srcdir)/common

datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

LIBS = @LIBS@

CLEANFILES=*~ \#*\#
AM_LFLAGS=-Pcfg_yy -olex.yy.c
lexer.c: lexer.l

celan: clean
