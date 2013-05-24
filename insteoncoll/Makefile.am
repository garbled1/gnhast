# $Id$

AM_CPPFLAGS = -DLOCALSTATEDIR=\"$(localstatedir)\" -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = insteon_discover insteoncoll insteon_aldb

insteon_discover_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	insteon_common.c \
	insteon_discover.c

insteoncoll_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	../common/netparser.c \
	../common/gncoll.c \
	../common/collcmd.c \
	insteon_common.c \
	collector.c

insteon_aldb_SOURCES = \
	../common/common.c \
	../common/devices.c \
	../common/confparser.c \
	insteon_common.c \
	insteon_aldb.c

insteon_discover_LDADD = -L../libconfuse -lconfuse
insteon_aldb_LDADD = -L../libconfuse -lconfuse
insteoncoll_LDADD = -L../libconfuse -lconfuse
