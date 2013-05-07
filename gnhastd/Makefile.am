# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = gnhastd

gnhastd_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	netloop.c \
	cmdhandler.c \
	script_handler.c \
	gnhastd.c

gnhastd_LDADD = -L../libconfuse -lconfuse
