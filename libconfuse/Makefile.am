lib_LTLIBRARIES	         = libgnconfuse.la
include_HEADERS     	 = $(top_srcdir)/common/confuse.h
libgnconfuse_la_SOURCES  = confuse.c compat.h lexer.l
libgnconfuse_la_CPPFLAGS = -D_GNU_SOURCE -DBUILDING_DLL
libgnconfuse_la_LIBADD   = $(LTLIBINTL)
# -no-undefined is required for windows DLL support
libgnconfuse_la_LDFLAGS  = $(AM_LDFLAGS) -no-undefined -version-info 1:0:0

datadir                = @datadir@
localedir              = $(datadir)/locale
DEFS                   = -DLOCALEDIR=\"$(localedir)\" @DEFS@
LIBS                   = @LIBS@
AM_LFLAGS              = @DEFS@ -Pcfg_yy -olex.yy.c
CLEANFILES             = *~ \#*\#

lexer.c: lexer.l
