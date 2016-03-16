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

bin_SCRIPTS = addhandler modhargs venstar_stats start_gnhast stop_gnhast
CLEANFILES = $(bin_SCRIPTS)
EXTRA_DIST = \
	start_gnhast.sh \
	stop_gnhast.sh \
	addhandler.sh \
	modhargs.sh \
	venstar_stats.sh

confexampledir = $(datarootdir)/gnhast/examples
dist_confexample_DATA = \
	gnhastd.run

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' \
	-e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g' \
	-e 's,[@]SYSCONFDIR[@],$(sysconfdir),g' \
	-e 's,[@]BINDIR[@],$(DESTDIR)$(bindir),g' \
	-e 's,[@]CURL[@],$(CURL),g' \
	-e 's,[@]LOCALSTATEDIR[@],$(localstatedir),g' \
	-e 's,[@]RRDTOOL[@],$(RRDTOOL),g'

.SECONDEXPANSION:
${bin_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
