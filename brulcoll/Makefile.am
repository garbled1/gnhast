# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = brulcoll

brulcoll_SOURCES = \
	../common/common.c \
	../common/serial_common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	collector.c

if NEED_RBTREE
brulcoll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

brulcoll_LDADD = -L../libconfuse -lconfuse
