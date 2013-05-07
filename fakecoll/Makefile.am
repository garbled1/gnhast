# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = fakecoll

fakecoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/gncoll.c \
	../common/confparser.c \
	collector.c

fakecoll_LDADD = -L../libconfuse -lconfuse
