AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common

bin_PROGRAMS = wupwscoll

wupwscoll_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/netparser.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/gncoll.c \
	$(top_srcdir)/common/collcmd.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/collcmd.h \
	wupws.h \
	collector.c

if NEED_RBTREE
wupwscoll_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

confexampledir = $(datarootdir)/gnhast/examples
dist_confexample_DATA = \
	wunderground.conf \
	pwsweather.conf

wupwscoll_LDADD = $(top_builddir)/libconfuse/libconfuse.la
