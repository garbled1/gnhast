## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/insteoncoll \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	@GTK2_CFLAGS@

bin_PROGRAMS = gtk-insteonedit

gtk_insteonedit_SOURCES = \
        $(top_srcdir)/common/common.c \
        $(top_srcdir)/common/devices.c \
        $(top_srcdir)/common/netparser.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/gncoll.c \
	$(top_srcdir)/common/gncoll.h \
	$(top_srcdir)/common/collcmd.c \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/genconn.c \
	$(top_srcdir)/common/genconn.h \
        $(top_srcdir)/common/common.h \
        $(top_srcdir)/common/gnhast.h \
        $(top_srcdir)/common/confuse.h \
        $(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/http_func.c \
	$(top_srcdir)/common/http_func.h \
	$(top_srcdir)/common/serial_common.c \
	$(top_srcdir)/insteoncoll/insteon.h \
	$(top_srcdir)/insteoncoll/insteon_common.c \
	main.c \
	gtk-insteonedit.h \
	support.c support.h \
	interface.c interface.h \
	callbacks.c callbacks.h

if NEED_RBTREE
gtk_insteonedit_SOURCES += \
        $(top_srcdir)/linux/bswap16.c \
        $(top_srcdir)/linux/bswap32.c \
        $(top_srcdir)/linux/bswap64.c \
        $(top_srcdir)/linux/rb.c \
        $(top_srcdir)/linux/queue.h \
        $(top_srcdir)/linux/endian.h \
        $(top_srcdir)/linux/rbtree.h \
        $(top_srcdir)/linux/time.h
endif

gtk_insteonedit_LDADD = @GTK2_LIBS@ $(top_builddir)/libconfuse/libgnconfuse.la
