handlerlibexecdir=$(libexecdir)/gnhast
handlerlibexecshelldir=$(handlerlibexecdir)
handlerlibexecshell_SCRIPTS = \
	timer \
	nightlight \
	switchon \
	switchoff
CLEANFILES = $(handlerlibexecshell_SCRIPTS)
EXTRA_DIST = \
	timer.sh \
	nightlight.sh \
	switchon.sh \
	switchoff.sh

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' -e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g'

.SECONDEXPANSION:
${handlerlibexecshell_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
