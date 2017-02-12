AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_srcdir)/common

bin_PROGRAMS = ad2usbcoll
dist_bin_SCRIPTS = mk_rfx_ad2usb

CLEANFILES = $(dist_bin_SCRIPTS)
EXTRA_DIST = mk_rfx_ad2usb.sh

ad2usbcoll_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/serial_common.c \
	$(top_srcdir)/common/devices.c \
	$(top_srcdir)/common/netparser.c \
	$(top_srcdir)/common/confparser.c \
	$(top_srcdir)/common/gncoll.c \
	$(top_srcdir)/common/genconn.c \
	$(top_srcdir)/common/genconn.h \
	$(top_srcdir)/common/collcmd.c \
	$(top_srcdir)/common/collcmd.h \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/gnhast.h \
	$(top_srcdir)/common/confuse.h \
	$(top_srcdir)/common/confparser.h \
	$(top_srcdir)/common/gncoll.h \
	ad2usb.h collector.h \
	collector.c

if NEED_RBTREE
ad2usbcoll_SOURCES += \
	$(top_srcdir)/linux/bswap16.c \
	$(top_srcdir)/linux/bswap32.c \
	$(top_srcdir)/linux/bswap64.c \
	$(top_srcdir)/linux/rb.c \
	$(top_srcdir)/linux/queue.h \
	$(top_srcdir)/linux/endian.h \
	$(top_srcdir)/linux/rbtree.h \
	$(top_srcdir)/linux/time.h
endif

ad2usbcoll_LDADD = $(top_builddir)/libconfuse/libgnconfuse.la

mk_rfx_ad2usb: mk_rfx_ad2usb.sh
	cat < mk_rfx_ad2usb.sh > mk_rfx_ad2usb
	chmod +x mk_rfx_ad2usb
