handlerlibexecdir=$(libexecdir)/gnhast
handlerlibexecshelldir=$(handlerlibexecdir)
handlerlibexecshell_SCRIPTS = \
	timer \
	timer2 \
	functions \
	nightlight \
	nightlight2 \
	autowater \
	switchon \
	switchoff

handlerlibexecperldir=$(handlerlibexecdir)
handlerlibexecperl_SCRIPTS = \
	wateruse

CLEANFILES = $(handlerlibexecshell_SCRIPTS) $(handlerlibexecperl_SCRIPTS)
EXTRA_DIST = \
	timer.sh \
	timer2.sh \
	functions.sh \
	nightlight.sh \
	nightlight2.sh \
	autowater.sh \
	wateruse.pl \
	switchon.sh \
	switchoff.sh

do_subst = sed -e 's,[@]NETCAT[@],$(NETCAT),g' \
	-e 's,[@]GNHANDLERDIR[@],$(pkglibexecdir),g' \
	-e 's,[@]LOCALSTATEDIR[@],$(localstatedir),g' \
	-e 's,[@]PERL[@],$(PERL),g' \
	-e 's,[@]SYSCONFDIR[@],$(sysconfdir),g'

.SECONDEXPANSION:
${handlerlibexecshell_SCRIPTS}: $$@.sh
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
${handlerlibexecperl_SCRIPTS}: $$@.pl
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
