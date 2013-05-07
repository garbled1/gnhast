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
	collector.c

rrdcoll_LDADD = @RRD_LIBS@ -L../libconfuse -lconfuse
