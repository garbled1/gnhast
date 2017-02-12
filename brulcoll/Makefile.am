
AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common
bin_PROGRAMS = brulcoll

brulcoll_SOURCES = \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gncoll.h \
	brultech.h \
	collector.c

if NEED_RBTREE
brulcoll_SOURCES += \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

brulcoll_LDADD = \
	$(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
