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

if NEED_RBTREE
fakecoll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

fakecoll_LDADD = -L../libconfuse -lconfuse
