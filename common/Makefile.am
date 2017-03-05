lib_LTLIBRARIES 	= libgnhast.la
pkginclude_HEADERS 	= \
	collcmd.h \
	commands.h \
	common.h \
	confparser.h \
	genconn.h \
	gncoll.h \
	gnhast.h \
	http_func.h \
	jsmn_func.h \
	ssdp.h \
	30303_disc.h \
	$(top_srcdir)/jsmn/jsmn.h

ALLCOMMON_SRC 	= \
	alarms.c \
	collcmd.c \
	common.c \
	confparser.c \
	devices.c \
	genconn.c \
	gncoll.c \
	http_func.c \
	jsmn_func.c \
	netparser.c \
	serial_common.c \
	ssdp.c \
	30303_disc.c \
	$(top_srcdir)/jsmn/jsmn.c

RBTREE_SRCS = \
        $(top_srcdir)/linux/bswap16.c \
        $(top_srcdir)/linux/bswap32.c \
        $(top_srcdir)/linux/bswap64.c \
        $(top_srcdir)/linux/rb.c

RBTREE_INCLUDES = \
        $(top_srcdir)/linux/queue.h \
        $(top_srcdir)/linux/endian.h \
        $(top_srcdir)/linux/rbtree.h \
        $(top_srcdir)/linux/time.h

libgnhast_la_LIBADD	=
libgnhast_la_LDFLAGS	=  $(AM_LDFLAGS) -version-info 0:1:0
libgnhast_la_SOURCES = $(ALLCOMMON_SRC)
libgnhast_la_CPPFLAGS = -DJSMN_PARENT_LINKS=1 -DJSMN_TOKEN_LINKS=1 \
	-DLOCALSTATEDIR=\"$(localstatedir)\" \
        -DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/jsmn

if NEED_RBTREE
libgnhast_la_SOURCES += $(RBTREE_SRCS)
pkginclude_HEADERS += $(RBTREE_INCLUDES)
endif

