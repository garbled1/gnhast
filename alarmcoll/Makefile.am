
AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common
bin_PROGRAMS = alarmcoll
EXTRA_DIST = README

alarmcoll_SOURCES = \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/genconn.h \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/gncoll.h \
	csvparser.c \
	collector.c collector.h

if NEED_RBTREE
alarmcoll_SOURCES += \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

confexampledir = $(datarootdir)/gnhast/examples
dist_confexample_DATA = \
	alarmlist.csv \
	alarmcoll.conf

alarmcoll_LDADD = \
	$(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
