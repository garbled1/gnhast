AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = wupwscoll

wupwscoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/netparser.c \
	../common/confparser.c \
	../common/gncoll.c \
	../common/collcmd.c \
	collector.c

wupwscoll_LDADD = -L../libconfuse -lconfuse
