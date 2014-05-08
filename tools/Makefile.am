AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" \
	      -DSYSCONFDIR=\"$(sysconfdir)\" \
              -I$(top_srcdir)/common

bin_PROGRAMS = ssdp_scan notify_listen

ssdp_scan_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/ssdp.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/ssdp.h \
	ssdp_scan.c

notify_listen_SOURCES = \
	$(top_srcdir)/common/common.c \
	$(top_srcdir)/common/ssdp.c \
	$(top_srcdir)/common/common.h \
	$(top_srcdir)/common/ssdp.h \
	notify_listen.c

dist_bin_SCRIPTS = addhandler modhargs
CLEANFILES = $(dist_bin_SCRIPTS)
EXTRA_DIST = \
	addhandler.sh \
	modhargs.sh

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' \
	-e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g' \
	-e 's,[@]SYSCONFDIR[@],$(sysconfdir),g'

.SECONDEXPANSION:
${dist_bin_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
