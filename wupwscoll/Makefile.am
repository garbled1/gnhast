AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = wupwscoll

wupwscoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	../common/collcmd.c \
	collector.c

if NEED_RBTREE
wupwscoll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

wupwscoll_LDADD = -L../libconfuse -lconfuse
