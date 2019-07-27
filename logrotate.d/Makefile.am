AM_CPPFLAGS= -DLOCALSTATEDIR=\"$(localstatedir)\" \
	     -DSYSCONFDIR=\"$(sysconfdir)\" \
	     -I$(top_srcdir)/common

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' \
	-e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g' \
	-e 's,[@]SYSCONFDIR[@],$(sysconfdir),g' \
	-e 's,[@]BINDIR[@],$(DESTDIR)$(bindir),g' \
	-e 's,[@]CURL[@],$(CURL),g' \
	-e 's,[@]LOCALSTATEDIR[@],$(localstatedir),g' \
	-e 's,[@]RRDTOOL[@],$(RRDTOOL),g'

if WITH_LOGROTATE

logrotatedir = @logrotate_path@
dist_logrotate_DATA = \
	gnhastd \
	moncoll \
	jsoncgicoll \
	ad2usbcoll \
	alarmcoll \
	astrocoll \
	balboacoll \
	brulcoll \
	icaddycoll \
	insteoncoll \
	owsrvcoll \
	rrdcoll \
	urtsicoll \
	venstarcoll \
	wmr918coll \
	wupwscoll

.SECONDEXPANSION:
${dist_logrotate_DATA}: skeleton
	$(do_subst) -e 's,[@]COLLECTOR_NAME[@],$@,g' < $(srcdir)/$? > $@

CLEANFILES = $(dist_logrotate_DATA)

endif
