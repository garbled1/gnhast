
AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common

bin_PROGRAMS = owsrvcoll

owsrvcoll_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/netparser.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gncoll.c \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/collcmd.c \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	owsrv.h \
	collector.c

if NEED_RBTREE
owsrvcoll_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

owsrvcoll_LDADD = $(top_builddir)/libconfuse/libgnconfuse.la
