handlerlibexecdir=$(libexecdir)/gnhast
handlerlibexecshelldir=$(handlerlibexecdir)
handlerlibexecshell_SCRIPTS = \
	nightlight \
	switchon \
	switchoff
CLEANFILES = $(handlerlibexecshell_SCRIPTS)
EXTRA_DIST = \
	nightlight.sh \
	switchon.sh \
	switchoff.sh

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g'

${handlerlibexecshell_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
