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
