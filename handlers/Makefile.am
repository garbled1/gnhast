handlerlibexecdir=$(libexecdir)/gnhast
handlerlibexecshelldir=$(handlerlibexecdir)
handlerlibexecshell_SCRIPTS = \
	timer \
	timer2 \
	functions \
	nightlight \
	switchon \
	switchoff
CLEANFILES = $(handlerlibexecshell_SCRIPTS)
EXTRA_DIST = \
	timer.sh \
	timer2.sh \
	functions.sh \
	nightlight.sh \
	switchon.sh \
	switchoff.sh

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' \
	-e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g' \
	-e 's,[@]SYSCONFDIR[@],$(sysconfdir),g'

.SECONDEXPANSION:
${handlerlibexecshell_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
