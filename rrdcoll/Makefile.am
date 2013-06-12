# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = rrdcoll

INCLUDES = @RRD_CFLAGS@

rrdcoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	../common/collcmd.c \
	collector.c

if NEED_RBTREE
rrdcoll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

rrdcoll_LDADD = @RRD_LIBS@ -L../libconfuse -lconfuse
