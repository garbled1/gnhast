AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common

bin_PROGRAMS = insteon_discover insteoncoll insteon_aldb

ALLCOMMON_SRC = \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/genconn.h \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/http_func.h \
	insteon.h collector.h \
	insteon_common.c 

RBTREE_SRCS = \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h

insteon_discover_SOURCES = \
	$(ALLCOMMON_SRC) \
	insteon_discover.c

if NEED_RBTREE
insteon_discover_SOURCES += $(RBTREE_SRCS)
endif

insteoncoll_SOURCES = \
	$(ALLCOMMON_SRC) \
	collector.c

if NEED_RBTREE
insteoncoll_SOURCES += $(RBTREE_SRCS)
endif

insteon_aldb_SOURCES = \
	$(ALLCOMMON_SRC) \
	insteon_aldb.c

if NEED_RBTREE
insteon_aldb_SOURCES += $(RBTREE_SRCS)
endif

dist_sysconf_DATA = insteon.db

insteon_discover_LDADD = $(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
insteon_aldb_LDADD = $(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
insteoncoll_LDADD = $(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
