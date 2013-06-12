AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = wmr918coll

wmr918coll_SOURCES = \
	../common/common.c \
	../common/serial_common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	collector.c

if NEED_RBTREE
wmr918coll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

wmr918coll_LDADD = -L../libconfuse -lconfuse
