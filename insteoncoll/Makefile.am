AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common

bin_PROGRAMS = insteon_discover insteoncoll insteon_aldb

insteon_discover_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/serial_common.c \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/genconn.h \
	insteon.h collector.h \
	insteon_common.c \
	insteon_discover.c

if NEED_RBTREE
insteon_discover_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

insteoncoll_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/netparser.c \
	$(top_srcdir)/common/gncoll.c \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/genconn.c \
	$(top_srcdir)/common/genconn.h \
	$(top_srcdir)/common/collcmd.c \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/serial_common.c \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	insteon.h collector.h \
	insteon_common.c \
	collector.c

if NEED_RBTREE
insteoncoll_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

insteon_aldb_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/serial_common.c \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/genconn.h \
	insteon.h collector.h \
	insteon_common.c \
	insteon_aldb.c

if NEED_RBTREE
insteon_aldb_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

dist_sysconf_DATA = insteon.db

insteon_discover_LDADD = $(top_builddir)/libconfuse/libconfuse.la
insteon_aldb_LDADD = $(top_builddir)/libconfuse/libconfuse.la
insteoncoll_LDADD = $(top_builddir)/libconfuse/libconfuse.la
