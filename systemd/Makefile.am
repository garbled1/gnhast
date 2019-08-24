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

if WITH_SYSTEMD

# systemdx is for the pre-written systemd unit files

systemddir = @systemd_path@
timerdir = @systemd_path@
systemdxdir = @systemd_path@
dist_systemd_DATA = ad2usbcoll.service \
	alarmcoll.service \
	astrocoll.service \
	balboacoll.service \
	brulcoll.service \
	icaddycoll.service \
	insteoncoll.service \
	owsrvcoll.service \
	rrdcoll.service \
	urtsicoll.service \
	venstarcoll.service \
	wmr918coll.service \
	wupwscoll.service
dist_timer_DATA = ad2usbcoll.timer \
	alarmcoll.timer \
	astrocoll.timer \
	balboacoll.timer \
	brulcoll.timer \
	icaddycoll.timer \
	insteoncoll.timer \
	owsrvcoll.timer \
	rrdcoll.timer \
	urtsicoll.timer \
	venstarcoll.timer \
	wmr918coll.timer \
	wupwscoll.timer \
	moncoll.timer
dist_systemdx_DATA = gnhastd.service \
	gnhastd_all.service \
	moncoll.service

CLEANFILES = $(dist_timer_DATA) $(dist_systemd_DATA) $(dist_systemdx_DATA)

.SECONDEXPANSION:
${dist_systemd_DATA}: collector.service.src
	$(do_subst) -e 's,[@]COLLECTOR_NAME[@],$(strip $(basename $@ .service)),g' < $(srcdir)/$? > $@

${dist_systemdx_DATA}: $$@.src
	$(do_subst) < $(srcdir)/$? > $@

${dist_timer_DATA}: collector.timer.src
	$(do_subst) < $(srcdir)/$? > $@

endif
