AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DJSMN_PARENT_LINKS=1 -DJSMN_TOKEN_LINKS=1 \
	-I$(top_srcdir)/common -I$(top_srcdir)/jsmn

bin_PROGRAMS = icaddycoll

icaddycoll_SOURCES = \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/http_func.h \
	$(top_srcdir)/common/jsmn_func.h \
	$(top_srcdir)/common/genconn.h \
	$(top_srcdir)/jsmn/jsmn.h \
	icaddy.h collector.h \
	collector.c

if NEED_RBTREE
icaddycoll_SOURCES += \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

confexampledir = $(datarootdir)/gnhast/examples
dist_confexample_DATA = \
	icaddy.conf

icaddycoll_LDADD = \
	$(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
