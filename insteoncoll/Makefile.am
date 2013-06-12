# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = insteon_discover insteoncoll insteon_aldb

insteon_discover_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	../common/serial_common.c \
	insteon_common.c \
	insteon_discover.c

if NEED_RBTREE
insteon_discover_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

insteoncoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	../common/netparser.c \
	../common/gncoll.c \
	../common/collcmd.c \
	../common/serial_common.c \
	insteon_common.c \
	collector.c

if NEED_RBTREE
insteoncoll_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

insteon_aldb_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	../common/serial_common.c \
	insteon_common.c \
	insteon_aldb.c

if NEED_RBTREE
insteon_aldb_SOURCES += \
	../linux/bswap16.c \
	../linux/bswap32.c \
	../linux/bswap64.c \
	../linux/rb.c
endif

insteon_discover_LDADD = -L../libconfuse -lconfuse
insteon_aldb_LDADD = -L../libconfuse -lconfuse
insteoncoll_LDADD = -L../libconfuse -lconfuse
