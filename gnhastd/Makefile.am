AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common
AM_LDFLAGS = -rdynamic
bin_PROGRAMS = gnhastd

gnhastd_SOURCES = \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/commands.h \
	$(top_srcdir)/common/gncoll.h \
	cmds.h \
	gnhastd.h \
	netloop.c \
	cmdhandler.c \
	script_handler.c \
	gnhastd.c

if NEED_RBTREE
gnhastd_SOURCES += \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

gnhastd_LDADD = \
	$(top_builddir)/libconfuse/libgnconfuse.la \
	$(top_builddir)/common/libgnhast.la
